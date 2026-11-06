#include "displayprovider.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QThread>
#include <QUuid>
#include <numeric>

#include "fileops.h"
#include "hypromodule.h"

static constexpr int PreviewSeconds = 15;
static constexpr int CheckSeconds = 3;
// Far enough to show up in Hyprland's answer, too small to matter for three seconds.
static constexpr int CheckNudgePixels = 8;

bool DisplayProvider::isSafeOutputName(const QString &name) {
    // The output name is written into Lua that Hyprland evaluates, and EDID
    // data can influence it, so only plain connector names may pass.
    static const QRegularExpression safe("^[A-Za-z0-9._-]+$");
    return safe.match(name).hasMatch();
}

double DisplayProvider::cleanScale(double requested, int width, int height) {
    // Hyprland accepts a scale only when the mode divides into whole logical
    // pixels in 1/120 steps, the same rule omarchy-hyprland-monitor-scaling uses.
    if (width <= 0 || height <= 0)
        return requested;
    const long long g = std::gcd<long long, long long>(width * 120LL, height * 120LL);
    long long k = qRound64(requested * 120);
    if (k > g)
        k = g;
    if (k < 1)
        k = 1;
    while (g % k != 0)
        ++k;
    return double(k) / 120.0;
}

QVariantList DisplayProvider::parseMonitors(const QByteArray &json) {
    QVariantList rows;
    for (const auto &value : QJsonDocument::fromJson(json).array()) {
        const auto m = value.toObject();
        QVariantMap row;
        row["output"] = m.value("name").toString();
        row["description"] = m.value("description").toString();
        row["make"] = m.value("make").toString();
        row["model"] = m.value("model").toString();
        row["width"] = m.value("width").toInt();
        row["height"] = m.value("height").toInt();
        row["refresh"] = m.value("refreshRate").toDouble();
        row["x"] = m.value("x").toInt();
        row["y"] = m.value("y").toInt();
        row["scale"] = m.value("scale").toDouble();
        row["transform"] = m.value("transform").toInt();
        row["focused"] = m.value("focused").toBool();
        row["enabled"] = !m.value("disabled").toBool();
        row["vrr"] = m.value("vrr").toBool();
        row["mirrorOf"] = m.value("mirrorOf").toString();
        row["internal"] = m.value("name").toString().contains(QRegularExpression("^(eDP|LVDS|DSI)-"));
        QStringList modes;
        for (const auto &mode : m.value("availableModes").toArray())
            modes << mode.toString().remove("Hz");
        row["modes"] = modes;
        row["mode"] = QString("%1x%2@%3").arg(m.value("width").toInt()).arg(m.value("height").toInt()).arg(m.value("refreshRate").toDouble(), 0, 'f', 2);
        rows << row;
    }
    return rows;
}

QString DisplayProvider::monitorLua(const QJsonObject &m) {
    const QString output = m.value("output").toString();
    if (m.value("disabled").toBool())
        return QString("hl.monitor({ output = \"%1\", disabled = true })").arg(output);
    QString lua = QString("hl.monitor({ output = \"%1\", mode = \"%2\", position = \"%3\", scale = %4")
                      .arg(output, m.value("mode").toString(), m.value("position").toString(), QString::number(m.value("scale").toDouble(), 'g', 8));
    if (m.value("transform").toInt() != 0)
        lua += QString(", transform = %1").arg(m.value("transform").toInt());
    if (!m.value("mirror").toString().isEmpty())
        lua += QString(", mirror = \"%1\"").arg(m.value("mirror").toString());
    if (m.value("vrr").toBool())
        lua += ", vrr = 1";
    return lua + " })";
}

DisplayProvider::DisplayProvider(CommandRunner *runner, ChangeCoordinator *coordinator, QObject *parent)
    : QObject(parent), m_runner(runner), m_coordinator(coordinator) {
    m_countdown.setInterval(1000);
    connect(&m_countdown, &QTimer::timeout, this, [this] {
        --m_previewSeconds;
        emit previewChanged();
        if (m_previewSeconds > 0)
            return;
        m_countdown.stop();
        // The watchdog owns the timeout; here we only wait to hear it finish.
        waitForWatchdog(m_resultFile, "restored", 80, [this](bool restored) {
            m_previewing = false;
            emit previewChanged();
            if (m_isCheck) {
                finishCheck();
                return;
            }
            emit message(restored ? tr("Kept the previous display layout.") : tr("The display layout could not be confirmed. Check your displays."), !restored);
            refresh();
        });
    });
}

QString DisplayProvider::runDirectory() const {
    return m_runner->env().runtimeDir + "/omarchy-control";
}

QString DisplayProvider::recoveryCheckPath() const {
    return m_runner->env().appStateDir + "/display-recovery-check.json";
}

bool DisplayProvider::editingUnlocked() const {
    QByteArray contents;
    if (!FileOps::readFile(recoveryCheckPath(), &contents))
        return false;
    const auto record = QJsonDocument::fromJson(contents).object();
    return record.value("passed").toBool() && !m_hyprlandVersion.isEmpty() && record.value("hyprland").toString() == m_hyprlandVersion;
}

void DisplayProvider::refresh() {
    const Env &env = m_runner->env();
    m_runner->run(env.hyprctl({"-j", "monitors", "all"}), CommandOptions{3000}, this, [this](const CommandResult &result) {
        if (!result.ok()) {
            m_status = "unavailable";
            emit changed();
            return;
        }
        m_monitors = parseMonitors(result.out);
        m_status = "ok";
        emit changed();
    });
    m_runner->run(env.hyprctl({"-j", "version"}), CommandOptions{3000}, this, [this](const CommandResult &result) {
        const QString version = QJsonDocument::fromJson(result.out).object().value("version").toString();
        if (version != m_hyprlandVersion) {
            m_hyprlandVersion = version;
            emit changed();
        }
    });
}

QString DisplayProvider::validateLayout(const QVariantList &layout) const {
    bool anyEnabled = false;
    for (const auto &value : layout) {
        const auto row = value.toMap();
        const QString output = row.value("output").toString();
        if (!isSafeOutputName(output))
            return tr("A display has a name that cannot be used safely.");
        bool known = false;
        QStringList modes;
        QString current;
        for (const auto &monitor : m_monitors) {
            const auto m = monitor.toMap();
            if (m.value("output").toString() == output) {
                known = true;
                modes = m.value("modes").toStringList();
                current = m.value("mode").toString();
            }
        }
        if (!known)
            return tr("%1 is no longer connected.").arg(output);
        if (row.value("enabled").toBool()) {
            anyEnabled = true;
            // What the display is doing right now is always acceptable, listed or not:
            // nested outputs and some custom modes are not in the list.
            if (!modes.contains(row.value("mode").toString()) && row.value("mode").toString() != current)
                return tr("%1 does not offer that mode.").arg(output);
            const double scale = row.value("scale").toDouble();
            if (scale < 0.25 || scale > 8)
                return tr("That scale is outside what %1 can use.").arg(output);
            const int transform = row.value("transform").toInt();
            if (transform < 0 || transform > 7)
                return tr("That rotation is not valid.");
        }
    }
    if (!anyEnabled)
        return tr("At least one display must stay on.");
    return {};
}

static QJsonObject monitorRule(const QVariantMap &row) {
    QJsonObject rule;
    rule["output"] = row.value("output").toString();
    if (!row.value("enabled").toBool()) {
        rule["disabled"] = true;
        return rule;
    }
    const QString mode = row.value("mode").toString();
    const auto size = mode.section('@', 0, 0).split('x');
    rule["mode"] = mode;
    rule["position"] = QString("%1x%2").arg(row.value("x").toInt()).arg(row.value("y").toInt());
    rule["scale"] = DisplayProvider::cleanScale(row.value("scale").toDouble(), size.value(0).toInt(), size.value(1).toInt());
    rule["transform"] = row.value("transform").toInt();
    // A mirrored display has to come back mirrored; dropping this would break it.
    const QString mirror = row.value("mirrorOf").toString();
    if (!mirror.isEmpty() && mirror != QLatin1String("none") && DisplayProvider::isSafeOutputName(mirror))
        rule["mirror"] = mirror;
    // Same for variable refresh rate, which a rule without it would turn off.
    if (row.value("vrr").toBool())
        rule["vrr"] = true;
    return rule;
}

void DisplayProvider::waitForWatchdog(const QString &file, const QString &expected, int attempts, std::function<void(bool)> done) {
    QByteArray contents;
    FileOps::readFile(file, &contents);
    if (contents.contains(expected.toUtf8())) {
        done(true);
        return;
    }
    if (attempts <= 0) {
        done(false);
        return;
    }
    QTimer::singleShot(100, this, [this, file, expected, attempts, done] { waitForWatchdog(file, expected, attempts - 1, done); });
}

void DisplayProvider::applyLayout(const QVariantList &layout, int index, std::function<void(bool, const QString &)> done) {
    if (index >= layout.size()) {
        done(true, {});
        return;
    }
    const QString lua = monitorLua(monitorRule(layout[index].toMap()));
    CommandOptions options;
    options.mutation = true;
    options.timeoutMs = 5000;
    m_runner->run(m_runner->env().hyprctl({"eval", lua}), options, this, [this, layout, index, done](const CommandResult &result) {
        if (!result.ok()) {
            done(false, result.describe("Hyprland"));
            return;
        }
        applyLayout(layout, index + 1, done);
    });
}

void DisplayProvider::startPreview(const QVariantList &layout, int seconds, bool isCheck) {
    if (m_previewing || m_starting) {
        emit message(tr("A display preview is already running."), true);
        return;
    }
    // Every output name goes into Lua below, whichever path brought it here.
    for (const auto &row : layout) {
        if (!isSafeOutputName(row.toMap().value("output").toString())) {
            failStart(tr("A display has a name that cannot be used safely."), isCheck);
            return;
        }
    }
    const QString dir = runDirectory();
    QDir().mkpath(dir);
    QFile::setPermissions(dir, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    const QString token = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    m_planPath = dir + "/display-" + token + ".json";
    m_keepFile = dir + "/display-" + token + ".keep";
    m_revertFile = dir + "/display-" + token + ".revert";
    m_resultFile = dir + "/display-" + token + ".result";

    QJsonArray previous;
    for (const auto &monitor : std::as_const(m_monitors)) {
        const auto row = monitor.toMap();
        previous.append(monitorRule(row));
    }
    QJsonObject plan;
    plan["previous"] = previous;
    plan["seconds"] = seconds;
    plan["keepFile"] = m_keepFile;
    plan["revertFile"] = m_revertFile;
    plan["resultFile"] = m_resultFile;
    QString error;
    if (!FileOps::atomicWrite(m_planPath, QJsonDocument(plan).toJson(), &error)) {
        failStart(error, isCheck);
        return;
    }

    // Recovery is registered before the first mutation; if it does not start,
    // nothing below ever runs.
    if (!m_runner->launchDetached({m_runner->env().selfBinary, "display-watchdog", m_planPath}, &error)) {
        failStart(tr("The display safety timer could not start, so nothing was changed. %1").arg(error), isCheck);
        return;
    }
    m_pendingLayout = layout;
    m_isCheck = isCheck;
    m_starting = true;
    HyprModule module(m_runner->env());
    m_startFingerprints.clear();
    m_startFingerprints.insert(module.path(), FileOps::fingerprint(module.path()));
    m_startFingerprints.insert(module.hyprlandPath(), FileOps::fingerprint(module.hyprlandPath()));
    waitForWatchdog(m_resultFile, "armed", 30, [this, layout, seconds, isCheck](bool armed) {
        if (!armed) {
            m_starting = false;
            failStart(tr("The display safety timer did not respond, so nothing was changed."), isCheck);
            return;
        }
        // Keep and Restore only appear once every row has been applied, so neither can
        // race a modeset that is still going on.
        applyLayout(layout, 0, [this, seconds, isCheck](bool ok, const QString &detail) {
            m_starting = false;
            if (!ok) {
                const QString problem = tr("Hyprland refused the layout: %1").arg(detail);
                // The watchdog has to be told, and it may have rows to put back.
                QFile revertFile(m_revertFile);
                if (revertFile.open(QIODevice::WriteOnly))
                    revertFile.close();
                waitForWatchdog(m_resultFile, "restored", 50, [this, problem, isCheck](bool) {
                    failStart(problem, isCheck);
                    refresh();
                });
                return;
            }
            m_previewing = true;
            m_previewSeconds = seconds;
            emit previewChanged();
            m_countdown.start();
            refresh();
            if (isCheck) {
                // The nudge has to be visible in Hyprland's answer. If it is not, the
                // compositor ignored the change, and a restore would prove nothing.
                m_runner->run(m_runner->env().hyprctl({"-j", "monitors", "all"}), CommandOptions{3000}, this, [this](const CommandResult &query) {
                    m_monitors = parseMonitors(query.out);
                    m_checkMoved = query.ok() && layoutSignature() != m_checkOriginal;
                    emit changed();
                });
            }
        });
    });
}

void DisplayProvider::failStart(const QString &text, bool isCheck) {
    if (isCheck) {
        m_checkState = "failed";
        emit changed();
        emit checkFinished(false, text);
    } else {
        emit message(text, true);
    }
}

void DisplayProvider::preview(const QVariantList &layout) {
    if (!editingUnlocked()) {
        emit message(tr("Run the display recovery check before arranging displays here."), true);
        return;
    }
    const QString problem = validateLayout(layout);
    if (!problem.isEmpty()) {
        emit message(problem, true);
        return;
    }
    startPreview(layout, PreviewSeconds, false);
}

void DisplayProvider::keep() {
    if (!m_previewing)
        return;
    m_countdown.stop();
    QFile keepFile(m_keepFile);
    if (keepFile.open(QIODevice::WriteOnly))
        keepFile.close();
    const QVariantList layout = m_pendingLayout;
    waitForWatchdog(m_resultFile, "kept", 50, [this, layout](bool kept) {
        m_previewing = false;
        emit previewChanged();
        if (!kept) {
            // The watchdog may already have restored; report what is true now.
            emit message(tr("The preview had already ended. Check the current layout."), true);
            refresh();
            return;
        }
        // Persist only now that the layout is confirmed to work.
        HyprModule module(m_runner->env());
        ChangeCoordinator::Request request;
        request.settingId = "displays.layout";
        request.label = tr("Display layout");
        request.after = "custom";
        request.files << module.path() << module.hyprlandPath();
        // A hand edit made while the preview ran is not ours to overwrite.
        request.expectedFingerprints = m_startFingerprints;
        request.apply = [this, layout](ChangeCoordinator::Done done) {
            HyprModule target(m_runner->env());
            QString error;
            if (!target.load(&error)) {
                done(false, error);
                return;
            }
            QJsonArray rules;
            for (const auto &row : layout)
                rules.append(monitorRule(row.toMap()));
            target.setMonitors(rules);
            done(target.save(&error) && target.installImport(&error), error);
        };
        m_coordinator->submit(std::move(request), [this](const Transaction &tx) {
            emit message(tx.phase == QLatin1String("committed") ? tr("Display layout saved.")
                                                                : tr("The layout is active but was not saved: %1").arg(tx.message),
                         tx.phase != QLatin1String("committed"));
            refresh();
        });
    });
}

void DisplayProvider::revert() {
    if (!m_previewing)
        return;
    m_countdown.stop();
    QFile revertFile(m_revertFile);
    if (revertFile.open(QIODevice::WriteOnly))
        revertFile.close();
    waitForWatchdog(m_resultFile, "restored", 50, [this](bool restored) {
        m_previewing = false;
        emit previewChanged();
        emit message(restored ? tr("Restored the previous display layout.") : tr("The previous layout could not be confirmed. Check your displays."), !restored);
        refresh();
    });
}

QStringList DisplayProvider::layoutSignature() const {
    QStringList signature;
    for (const auto &monitor : m_monitors) {
        const auto m = monitor.toMap();
        signature << QString("%1 %2 %3x%4 %5 %6").arg(m.value("output").toString(), m.value("mode").toString()).arg(m.value("x").toInt()).arg(m.value("y").toInt()).arg(m.value("scale").toDouble()).arg(m.value("transform").toInt());
    }
    return signature;
}

void DisplayProvider::runRecoveryCheck() {
    if (m_status != QLatin1String("ok") || m_monitors.isEmpty()) {
        emit checkFinished(false, tr("No display information is available."));
        return;
    }
    m_checkState = "running";
    m_checkMoved = false;
    m_checkOriginal = layoutSignature();
    emit changed();
    // A real change, or the check would prove nothing: re-applying the same layout
    // looks identical whether the restore worked or not. So the first display moves
    // a few pixels, Hyprland has to report the move, and then the watchdog has to put
    // it back unprompted. A display is only nudged in space, so nothing resizes.
    QVariantList nudged;
    bool moved = false;
    for (const auto &monitor : m_monitors) {
        auto row = monitor.toMap();
        if (!moved && row.value("enabled").toBool()) {
            row["x"] = row.value("x").toInt() + CheckNudgePixels;
            moved = true;
        }
        nudged << row;
    }
    startPreview(nudged, CheckSeconds, true);
}

void DisplayProvider::finishCheck() {
    QByteArray result;
    FileOps::readFile(m_resultFile, &result);
    const bool restored = result.contains("restored");
    m_runner->run(m_runner->env().hyprctl({"-j", "monitors", "all"}), CommandOptions{3000}, this, [this, restored](const CommandResult &query) {
        const auto monitors = parseMonitors(query.out);
        m_monitors = monitors;
        const bool same = layoutSignature() == m_checkOriginal;
        const bool passed = restored && same && query.ok() && m_checkMoved;
        QJsonObject record;
        record["passed"] = passed;
        record["hyprland"] = m_hyprlandVersion;
        record["at"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        record["monitors"] = monitors.size();
        FileOps::atomicWrite(recoveryCheckPath(), QJsonDocument(record).toJson(), nullptr, 0600);
        m_checkState = passed ? "passed" : "failed";
        emit changed();
        emit checkFinished(passed, passed ? tr("The safety timer restored the layout correctly. Display editing is unlocked.")
                                          : tr("The safety timer did not restore the layout correctly, so display editing stays locked."));
    });
}

int DisplayProvider::runWatchdog(const Env &env, const QString &planPath) {
    QByteArray contents;
    if (!FileOps::readFile(planPath, &contents))
        return 1;
    const auto plan = QJsonDocument::fromJson(contents).object();
    const QString keepFile = plan.value("keepFile").toString();
    const QString revertFile = plan.value("revertFile").toString();
    const QString resultFile = plan.value("resultFile").toString();
    const int seconds = plan.value("seconds").toInt(PreviewSeconds);

    auto report = [&](const QByteArray &text) { FileOps::atomicWrite(resultFile, text + "\n"); };
    report("armed");

    // Plain blocking calls: this process has one job, no window to keep alive,
    // and must not depend on anything else being healthy.
    auto hyprctl = [&](const QStringList &arguments, QByteArray *out = nullptr) {
        const QStringList argv = env.hyprctl(arguments);
        QProcess process;
        process.setProcessEnvironment(env.process);
        process.start(env.findExecutable(argv.first()), argv.mid(1));
        if (!process.waitForStarted(2000) || !process.waitForFinished(5000)) {
            process.kill();
            process.waitForFinished(500);
            return false;
        }
        if (out)
            *out = process.readAllStandardOutput();
        return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    };

    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + seconds * 1000LL;
    bool kept = false;
    while (QDateTime::currentMSecsSinceEpoch() < deadline) {
        if (QFile::exists(keepFile)) {
            kept = true;
            break;
        }
        if (QFile::exists(revertFile))
            break;
        QThread::msleep(100);
    }
    if (kept) {
        report("kept");
    } else {
        // Hot-unplug: only outputs still present can be restored, and at least
        // one must stay on or the session would have no screen at all.
        QByteArray current;
        QStringList present;
        if (hyprctl({"-j", "monitors", "all"}, &current))
            for (const auto &value : QJsonDocument::fromJson(current).array())
                present << value.toObject().value("name").toString();

        bool anyRestoredOn = false;
        bool allOk = true;
        QJsonArray previous = plan.value("previous").toArray();
        // Enabled monitors first, so there is never a moment with no output.
        for (int pass = 0; pass < 2; ++pass) {
            for (const auto &value : previous) {
                const auto rule = value.toObject();
                const bool disabled = rule.value("disabled").toBool();
                if (disabled != (pass == 1))
                    continue;
                const QString output = rule.value("output").toString();
                if (!DisplayProvider::isSafeOutputName(output) || (!present.isEmpty() && !present.contains(output)))
                    continue;
                if (disabled && !anyRestoredOn)
                    continue;
                const bool ok = hyprctl({"eval", monitorLua(rule)});
                allOk = allOk && ok;
                if (!disabled && ok)
                    anyRestoredOn = true;
            }
        }
        bool lastResort = false;
        if (!anyRestoredOn) {
            for (const auto &name : present) {
                if (DisplayProvider::isSafeOutputName(name) && hyprctl({"eval", QString("hl.monitor({ output = \"%1\", mode = \"preferred\", position = \"auto\", scale = 1 })").arg(name)})) {
                    lastResort = true;
                    break;
                }
            }
        }
        report(allOk && (anyRestoredOn || lastResort) ? "restored" : "restore-failed");
    }
    QFile::remove(planPath);
    QFile::remove(keepFile);
    QFile::remove(revertFile);
    return 0;
}
