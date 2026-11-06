#include "servicesprovider.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <algorithm>

#include "fileops.h"

ServicesProvider::ServicesProvider(CommandRunner *runner, ChangeCoordinator *coordinator, QObject *parent)
    : QObject(parent), m_runner(runner), m_coordinator(coordinator) {}

bool ServicesProvider::isSafeUnit(const QString &unit) {
    static const QRegularExpression safe(R"(^[A-Za-z0-9:_.@\\-]+\.service$)");
    return safe.match(unit).hasMatch();
}

QVariantList ServicesProvider::mergeUnits(const QByteArray &units, const QByteArray &files) {
    QHash<QString, QString> enablement;
    for (const auto &value : QJsonDocument::fromJson(files).array()) {
        const auto file = value.toObject();
        enablement.insert(file.value("unit_file").toString(), file.value("state").toString());
    }
    QVariantList rows;
    QStringList seen;
    for (const auto &value : QJsonDocument::fromJson(units).array()) {
        const auto unit = value.toObject();
        const QString name = unit.value("unit").toString();
        if (!name.endsWith(".service"))
            continue;
        seen << name;
        QVariantMap row;
        row["unit"] = name;
        row["description"] = unit.value("description").toString();
        row["active"] = unit.value("active").toString();
        row["sub"] = unit.value("sub").toString();
        row["enabled"] = enablement.value(name);
        row["failed"] = unit.value("active").toString() == QLatin1String("failed");
        rows << row;
    }
    // Installed but never started this boot; it can still be enabled from here.
    for (auto it = enablement.constBegin(); it != enablement.constEnd(); ++it) {
        if (seen.contains(it.key()) || it.key().contains('@') || (it.value() != QLatin1String("enabled") && it.value() != QLatin1String("disabled")))
            continue;
        QVariantMap row;
        row["unit"] = it.key();
        row["description"] = QString();
        row["active"] = QStringLiteral("inactive");
        row["sub"] = QStringLiteral("dead");
        row["enabled"] = it.value();
        row["failed"] = false;
        rows << row;
    }
    std::sort(rows.begin(), rows.end(), [](const QVariant &a, const QVariant &b) {
        const auto ma = a.toMap(), mb = b.toMap();
        if (ma.value("failed").toBool() != mb.value("failed").toBool())
            return ma.value("failed").toBool();
        const bool activeA = ma.value("active").toString() == QLatin1String("active");
        const bool activeB = mb.value("active").toString() == QLatin1String("active");
        if (activeA != activeB)
            return activeA;
        return ma.value("unit").toString() < mb.value("unit").toString();
    });
    return rows;
}

int ServicesProvider::failedCount() const {
    int count = 0;
    for (const auto *rows : {&m_user, &m_system})
        for (const auto &row : *rows)
            if (row.toMap().value("failed").toBool())
                ++count;
    return count;
}

void ServicesProvider::loadScope(bool system) {
    const QStringList scope = system ? QStringList{"--system"} : QStringList{"--user"};
    m_runner->run(QStringList{"systemctl"} + scope + QStringList{"list-units", "--type=service", "--all", "-o", "json", "--no-pager"}, CommandOptions{6000}, this,
                  [this, system, scope](const CommandResult &units) {
        m_runner->run(QStringList{"systemctl"} + scope + QStringList{"list-unit-files", "--type=service", "-o", "json", "--no-pager"}, CommandOptions{6000}, this,
                      [this, system, units](const CommandResult &files) {
            const auto rows = mergeUnits(units.out, files.out);
            (system ? m_system : m_user) = rows;
            m_status = units.ok() ? "ok" : "unavailable";
            emit changed();
        });
    });
}

void ServicesProvider::refresh() {
    loadScope(false);
    loadScope(true);
    m_startup = readStartup(m_runner->env());
    emit changed();
}

void ServicesProvider::serviceAction(const QString &unit, const QString &action, bool system) {
    static const QStringList allowed{"start", "stop", "restart", "enable", "disable"};
    if (!isSafeUnit(unit) || !allowed.contains(action)) {
        emit message(tr("That service action is not available."), true);
        return;
    }
    const QString scope = system ? "--system" : "--user";
    ChangeCoordinator::Request request;
    request.settingId = "services." + unit;
    request.label = unit;
    request.after = action;
    request.apply = [this, scope, action, unit](ChangeCoordinator::Done done) {
        CommandOptions options;
        options.mutation = true;
        // Long enough for someone to answer the desktop's authorization prompt.
        options.timeoutMs = 90000;
        m_runner->run({"systemctl", scope, action, unit}, options, this, [done](const CommandResult &result) { done(result.ok(), result.describe("systemctl")); });
    };
    const bool wantsActive = action == QLatin1String("start") || action == QLatin1String("restart");
    const bool wantsInactive = action == QLatin1String("stop");
    if (wantsActive || wantsInactive) {
        request.verify = [this, scope, unit, wantsActive](ChangeCoordinator::Done done) {
            m_runner->run({"systemctl", scope, "show", "-p", "ActiveState", "-p", "Result", unit}, CommandOptions{4000}, this, [this, wantsActive, done](const CommandResult &result) {
                if (!result.ok()) {
                    done(false, tr("The service's state could not be confirmed."));
                    return;
                }
                const QString text = QString::fromUtf8(result.out);
                const bool active = text.contains("ActiveState=active") || text.contains("ActiveState=activating");
                // A one-shot service that ran and finished is inactive, and also fine.
                const bool ranFine = !active && text.contains("Result=success");
                if (wantsActive)
                    done(active || ranFine, tr("The service did not start."));
                else
                    done(!active, tr("The service is still running."));
            });
        };
    }
    m_coordinator->submit(std::move(request), [this, unit, action](const Transaction &tx) {
        if (tx.phase == QLatin1String("committed"))
            emit message(tr("%1: %2").arg(unit, action), false);
        else
            emit message(tx.message, true);
        loadScope(false);
        loadScope(true);
    });
}

static QHash<QString, QString> parseDesktop(const QByteArray &contents) {
    QHash<QString, QString> values;
    bool inEntry = false;
    for (const auto &raw : QString::fromUtf8(contents).split('\n')) {
        const QString line = raw.trimmed();
        if (line.startsWith('[')) {
            inEntry = line == QLatin1String("[Desktop Entry]");
            continue;
        }
        const int equals = line.indexOf('=');
        if (inEntry && equals > 0)
            values.insert(line.left(equals).trimmed(), line.mid(equals + 1).trimmed());
    }
    return values;
}

QVariantList ServicesProvider::readStartup(const Env &env) {
    QHash<QString, QVariantMap> rows;
    const QStringList directories{env.sys("/etc/xdg/autostart"), env.configHome + "/autostart"};
    for (const auto &directory : directories) {
        const bool user = directory.startsWith(env.configHome);
        for (const auto &file : QDir(directory).entryInfoList({"*.desktop"}, QDir::Files | QDir::Readable)) {
            QByteArray contents;
            FileOps::readFile(file.absoluteFilePath(), &contents);
            const auto values = parseDesktop(contents);
            QVariantMap row;
            const auto existing = rows.value(file.fileName());
            row["id"] = file.fileName();
            row["name"] = values.value("Name", existing.value("name", file.baseName()).toString());
            row["comment"] = values.value("Comment", existing.value("comment").toString());
            row["exec"] = values.value("Exec", existing.value("exec").toString());
            row["enabled"] = values.value("Hidden") != QLatin1String("true") && values.value("X-GNOME-Autostart-enabled") != QLatin1String("false");
            row["source"] = user ? (existing.isEmpty() ? "user" : "override") : "system";
            rows.insert(file.fileName(), row);
        }
    }
    QVariantList list;
    for (const auto &row : std::as_const(rows))
        list << row;
    std::sort(list.begin(), list.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value("name").toString().localeAwareCompare(b.toMap().value("name").toString()) < 0;
    });
    return list;
}

QByteArray ServicesProvider::withHidden(const QByteArray &entry, bool hidden) {
    QList<QByteArray> lines = entry.split('\n');
    bool found = false;
    int entryLine = -1;
    for (int i = 0; i < lines.size(); ++i) {
        if (lines[i].trimmed() == "[Desktop Entry]")
            entryLine = i;
        if (lines[i].startsWith("Hidden=")) {
            lines[i] = hidden ? "Hidden=true" : "Hidden=false";
            found = true;
        }
        // The other way to switch an item off; it would keep this one from turning on.
        if (!hidden && lines[i].startsWith("X-GNOME-Autostart-enabled="))
            lines[i] = "X-GNOME-Autostart-enabled=true";
    }
    if (!found) {
        if (entryLine < 0) {
            lines.prepend("[Desktop Entry]");
            entryLine = 0;
        }
        lines.insert(entryLine + 1, hidden ? "Hidden=true" : "Hidden=false");
    }
    return lines.join('\n');
}

void ServicesProvider::setStartupEnabled(const QString &id, bool enabled) {
    const Env &env = m_runner->env();
    if (id.contains('/') || !id.endsWith(".desktop")) {
        emit message(tr("That login item is not valid."), true);
        return;
    }
    const QString userFile = env.configHome + "/autostart/" + id;
    const QString systemFile = env.sys("/etc/xdg/autostart/") + id;
    ChangeCoordinator::Request request;
    request.settingId = "startup." + id;
    request.label = id;
    request.before = enabled ? "false" : "true";
    request.after = enabled ? "true" : "false";
    // The freedesktop way: a user file with Hidden=true masks a system entry
    // without touching the original.
    request.files << userFile;
    request.apply = [userFile, systemFile, enabled](ChangeCoordinator::Done done) {
        QByteArray contents;
        if (!FileOps::readFile(userFile, &contents) && !FileOps::readFile(systemFile, &contents)) {
            done(false, QObject::tr("This login item no longer exists."));
            return;
        }
        QString error;
        done(FileOps::atomicWrite(userFile, withHidden(contents, !enabled), &error, 0644), error);
    };
    request.verify = [this, id, enabled](ChangeCoordinator::Done done) {
        const auto rows = readStartup(m_runner->env());
        for (const auto &row : rows)
            if (row.toMap().value("id").toString() == id && row.toMap().value("enabled").toBool() == enabled) {
                done(true, {});
                return;
            }
        done(false, tr("The login item did not change."));
    };
    m_coordinator->submit(std::move(request), [this](const Transaction &tx) {
        if (tx.phase != QLatin1String("committed"))
            emit message(tx.message, true);
        m_startup = readStartup(m_runner->env());
        emit changed();
    });
}
