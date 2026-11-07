#include "iconthemeprovider.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <memory>

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include "fileops.h"

namespace {

const char Schema[] = "org.gnome.desktop.interface";
const char Key[] = "icon-theme";
const char DconfPath[] = "/org/gnome/desktop/interface/icon-theme";
constexpr int SamplePixels = 48;
// A theme change another manager overrides this many times in a row is a standoff, and
// correcting it forever would be two tools fighting over the same setting.
constexpr int ConflictLimit = 3;

// Advisory, and waited on only briefly: a lock someone holds for too long is not worth
// holding up a theme change for.
class ScopedFlock {
public:
    ScopedFlock(const QString &path, int operation, int timeoutMs, bool create) {
        m_fd = ::open(path.toLocal8Bit().constData(), (create ? O_CREAT : 0) | O_RDWR | O_CLOEXEC, 0600);
        if (m_fd < 0)
            return;
        QElapsedTimer timer;
        timer.start();
        while (::flock(m_fd, operation | LOCK_NB) != 0) {
            if (timer.elapsed() > timeoutMs) {
                ::close(m_fd);
                m_fd = -1;
                return;
            }
            QThread::msleep(25);
        }
        m_held = true;
    }
    ~ScopedFlock() {
        if (m_fd >= 0)
            ::close(m_fd);
    }
    bool held() const { return m_held; }

private:
    int m_fd = -1;
    bool m_held = false;
};

QJsonObject effectiveJson(const QString &before, bool beforeUnset, const QString &after, bool afterUnset) {
    QJsonObject object;
    object["before"] = before;
    object["beforeUnset"] = beforeUnset;
    object["after"] = after;
    object["afterUnset"] = afterUnset;
    return object;
}

}  // namespace

QString IconThemeProvider::State::value() const {
    if (managed)
        return fixed ? QString::fromLatin1(PackPrefix) + selected : QString::fromLatin1(FollowTheme);
    // With no choice saved, Omarchy is already setting whatever its theme asks for, and
    // that is following it. Anything else was set elsewhere, and is shown as it is.
    if (effective == desktopDefault)
        return QString::fromLatin1(FollowTheme);
    return QString::fromLatin1(PackPrefix) + effective;
}

bool IconThemeProvider::State::conflict() const {
    if (!managed || !known)
        return false;
    return fixed ? (selectedInstalled && effective != selected) : effective != desktopDefault;
}

QStringList IconThemeProvider::State::disagreeing() const {
    QStringList labels;
    for (const Consumer &consumer : consumers)
        if (consumer.blocked.isEmpty() && consumer.value != effective)
            labels << consumer.label;
    return labels;
}

QString IconThemeProvider::State::reason() const {
    if (suspended)
        return IconThemeProvider::tr("Another icon manager keeps changing this, so your choice is no longer reapplied after theme changes. Choose a pack here to take it back.");
    if (unreadablePolicy)
        return IconThemeProvider::tr("Your saved icon choice could not be read, so it is being ignored.");
    if (managed && fixed && !selectedInstalled)
        return IconThemeProvider::tr("Your chosen pack, %1, is not installed any more. Your choice is kept. Pick another pack, or follow the desktop theme.").arg(selected);
    if (known && !effectiveInstalled)
        return IconThemeProvider::tr("%1 is in use but is not installed, so apps fall back to other icons.").arg(effective);
    if (conflict())
        return fixed ? IconThemeProvider::tr("Something else changed the icons to %1. Your choice of %2 is still saved. Choose it again to bring it back.").arg(effective, selected)
                     : IconThemeProvider::tr("Something else changed the icons to %1, and the desktop theme uses %2. Choose it again to bring it back.").arg(effective, desktopDefault);
    const QStringList differ = disagreeing();
    if (known && !differ.isEmpty())
        return IconThemeProvider::tr("Not everything agrees: %1 uses a different pack from %2. Choosing a pack brings them into line.").arg(differ.join(", "), effective);
    if (managed && hook == QLatin1String("edited"))
        return IconThemeProvider::tr("The hook that keeps this across theme changes was edited, so it was left alone. A theme change may not keep your choice.");
    if (managed && hook == QLatin1String("absent"))
        return IconThemeProvider::tr("The hook that keeps this across theme changes is missing. Choose a pack again to restore it.");
    return {};
}

QVariantMap IconThemeProvider::State::toVariant() const {
    QVariantList rows;
    for (const Consumer &consumer : consumers)
        rows << QVariantMap{{"id", consumer.id}, {"label", consumer.label}, {"path", consumer.path}, {"value", consumer.value}, {"blocked", consumer.blocked}};
    return {{"mode", !managed ? "unmanaged" : fixed ? "fixed" : "follow-theme"},
            {"selected", selected},
            {"selectedInstalled", selectedInstalled},
            {"effective", effective},
            {"effectiveInstalled", effectiveInstalled},
            {"desktopDefault", desktopDefault},
            {"hook", hook},
            {"suspended", suspended},
            {"conflict", conflict()},
            {"mixed", !disagreeing().isEmpty()},
            {"consumers", rows},
            {"reconcile", suspended || conflict() || !disagreeing().isEmpty() || (managed && hook == QLatin1String("absent"))}};
}

IconThemeProvider::IconThemeProvider(CommandRunner *runner, ChangeCoordinator *coordinator, QObject *parent)
    : QObject(parent), m_runner(runner), m_coordinator(coordinator) {}

QString IconThemeProvider::policyPath() const {
    return m_runner->env().appConfigDir + "/icons.json";
}

QString IconThemeProvider::hookPath() const {
    return m_runner->env().configHome + "/omarchy/hooks/theme-set.d/90-omarchy-control-icons";
}

QString IconThemeProvider::runtimeStatePath() const {
    return m_runner->env().appStateDir + "/icons-state.json";
}

QByteArray IconThemeProvider::hookScript() {
    return "#!/bin/bash\n"
           "# Installed by Omarchy Control. Keeps your icon pack choice after a theme change.\n"
           "# Undoing that choice, or `omarchy-control icons remove-integration`, removes it.\n"
           "if command -v omarchy-control >/dev/null; then\n"
           "  omarchy-control icons reapply \"$1\"\n"
           "fi\n"
           "exit 0\n";
}

QString IconThemeProvider::normalizeChoice(const QString &value) {
    if (value == QLatin1String(FollowTheme) || value.startsWith(QLatin1String(PackPrefix)))
        return value;
    return QString::fromLatin1(PackPrefix) + value;
}

IconThemeProvider::Policy IconThemeProvider::loadPolicy() const {
    Policy policy;
    QByteArray contents;
    if (!FileOps::readFile(policyPath(), &contents))
        return policy;
    policy.exists = true;
    const QJsonObject object = QJsonDocument::fromJson(contents).object();
    const QString mode = object.value("mode").toString();
    if (object.value("version").toInt() != 1)
        return policy;
    if (mode == QLatin1String(FollowTheme)) {
        policy.valid = true;
    } else if (mode == QLatin1String("fixed") && IconThemes::isValidId(object.value("theme").toString())) {
        policy.valid = true;
        policy.fixed = true;
        policy.theme = object.value("theme").toString();
    }
    return policy;
}

QByteArray IconThemeProvider::policyBytes(const QString &choice) const {
    QJsonObject object;
    object["version"] = 1;
    if (choice == QLatin1String(FollowTheme)) {
        object["mode"] = QString::fromLatin1(FollowTheme);
    } else {
        object["mode"] = QStringLiteral("fixed");
        object["theme"] = choice.mid(int(strlen(PackPrefix)));
    }
    return QJsonDocument(object).toJson();
}

QString IconThemeProvider::hookState() const {
    QByteArray contents;
    if (!FileOps::readFile(hookPath(), &contents))
        return QStringLiteral("absent");
    return contents == hookScript() ? QStringLiteral("owned") : QStringLiteral("edited");
}

QList<IconThemeProvider::Consumer> IconThemeProvider::consumers() const {
    const Env &env = m_runner->env();
    const QString config = env.process.value("XDG_CONFIG_HOME", env.configHome);
    const QString platform = env.process.value("QT_QPA_PLATFORMTHEME");
    struct Candidate {
        const char *id;
        QString label, path;
        const char *group, *key;
        bool active;
    };
    const QList<Candidate> candidates{
        {"gtk3", tr("GTK 3 settings.ini"), config + "/gtk-3.0/settings.ini", "Settings", "gtk-icon-theme-name", true},
        {"gtk4", tr("GTK 4 settings.ini"), config + "/gtk-4.0/settings.ini", "Settings", "gtk-icon-theme-name", true},
        // These tools only have a say when Qt is told to use them.
        {"qt5ct", QStringLiteral("qt5ct"), config + "/qt5ct/qt5ct.conf", "Appearance", "icon_theme", platform.contains(QLatin1String("qt5ct"))},
        {"qt6ct", QStringLiteral("qt6ct"), config + "/qt6ct/qt6ct.conf", "Appearance", "icon_theme", platform.contains(QLatin1String("qt6ct"))},
        {"kde", QStringLiteral("KDE"), config + "/kdeglobals", "Icons", "Theme", true},
    };
    QList<Consumer> rows;
    for (const Candidate &candidate : candidates) {
        if (!candidate.active)
            continue;
        QByteArray contents;
        if (!FileOps::readFile(candidate.path, &contents))
            continue;
        const FileOps::IniValue value = FileOps::iniGet(contents, QString::fromLatin1(candidate.group), QString::fromLatin1(candidate.key));
        // A file that does not name an icon theme leaves it to GSettings, and is left alone.
        if (!value.found)
            continue;
        Consumer consumer{QString::fromLatin1(candidate.id), candidate.label, candidate.path, QString::fromLatin1(candidate.group), QString::fromLatin1(candidate.key), value.value, {}};
        if (value.ambiguous)
            consumer.blocked = tr("%1 names an icon theme more than once, so it was left as it is.").arg(candidate.label);
        else if (!QFileInfo(candidate.path).isWritable())
            consumer.blocked = tr("%1 is read-only, so it was left as it is.").arg(candidate.label);
        rows << consumer;
    }
    return rows;
}

IconThemeProvider::Reapplied IconThemeProvider::loadReapplied() const {
    QByteArray contents;
    FileOps::readFile(runtimeStatePath(), &contents);
    const QJsonObject object = QJsonDocument::fromJson(contents).object();
    Reapplied state;
    state.conflicts = object.value("conflicts").toInt();
    state.suspended = object.value("suspended").toBool();
    state.lastApplied = object.value("lastApplied").toString();
    return state;
}

void IconThemeProvider::saveReapplied(const Reapplied &state) const {
    QJsonObject object;
    object["conflicts"] = state.conflicts;
    object["suspended"] = state.suspended;
    object["lastApplied"] = state.lastApplied;
    FileOps::atomicWrite(runtimeStatePath(), QJsonDocument(object).toJson(), nullptr, 0600);
}

bool IconThemeProvider::patchConsumers(const QString &target, QString *error, const QString &from) const {
    for (const Consumer &consumer : consumers()) {
        if (!consumer.blocked.isEmpty() || consumer.value == target)
            continue;
        if (!from.isEmpty() && consumer.value != from)
            continue;
        QByteArray contents;
        if (!FileOps::readFile(consumer.path, &contents, error))
            return false;
        if (!FileOps::iniSet(&contents, consumer.group, consumer.key, target, error) || !FileOps::atomicWrite(consumer.path, contents, error))
            return false;
    }
    return true;
}

bool IconThemeProvider::consumersMatch(const QString &target, QString *which) const {
    for (const Consumer &consumer : consumers()) {
        if (consumer.blocked.isEmpty() && consumer.value != target) {
            if (which)
                *which = consumer.label;
            return false;
        }
    }
    return true;
}

void IconThemeProvider::readEffective(std::function<void(bool, const QString &, const QString &)> callback) {
    m_runner->run({"gsettings", "get", Schema, Key}, CommandOptions{5000}, this, [callback](const CommandResult &result) {
        if (!result.ok()) {
            callback(false, {}, result.describe(QStringLiteral("gsettings")));
            return;
        }
        bool ok = false;
        const QString value = IconThemes::parseGVariantString(result.out, &ok);
        callback(ok, value, ok ? QString() : IconThemeProvider::tr("GSettings gave an answer that could not be read."));
    });
}

void IconThemeProvider::confirmUnchanged(const QString &expected, std::function<void()> proceed, ChangeCoordinator::Done done) {
    readEffective([expected, proceed, done](bool ok, const QString &now, const QString &error) {
        if (!ok) {
            done(false, error);
            return;
        }
        if (now != expected) {
            done(false, IconThemeProvider::tr("Something else changed the icons to %1 in the meantime, so they were left as they are.").arg(now));
            return;
        }
        proceed();
    });
}

// A change that went through settles any standoff with another icon manager, and is what
// later automatic runs compare against. A failed one must leave all of that as it was.
void IconThemeProvider::settle(const QString &target) {
    Reapplied state;
    state.lastApplied = target;
    saveReapplied(state);
}

// A key no one ever set reads back as its schema default, which gsettings cannot tell from
// the same value set on purpose. Going back exactly means knowing which it was. Only dconf
// can say, so with another backend, or without dconf, the value is restored as a set value.
void IconThemeProvider::readUnset(std::function<void(bool)> callback) {
    const Env &env = m_runner->env();
    const QString backend = env.process.value("GSETTINGS_BACKEND");
    if ((!backend.isEmpty() && backend != QLatin1String("dconf")) || env.findExecutable("dconf").isEmpty()) {
        callback(false);
        return;
    }
    m_runner->run({"dconf", "read", DconfPath}, CommandOptions{5000}, this, [callback](const CommandResult &result) {
        callback(result.ok() && result.outText().isEmpty());
    });
}

void IconThemeProvider::writeEffective(const QString &value, bool unset, ChangeCoordinator::Done done) {
    CommandOptions options;
    options.mutation = true;
    options.timeoutMs = 8000;
    const QStringList argv = unset ? QStringList{"gsettings", "reset", Schema, Key} : QStringList{"gsettings", "set", Schema, Key, value};
    m_runner->run(argv, options, this, [done](const CommandResult &result) {
        done(result.ok(), result.ok() ? QString() : result.describe(QStringLiteral("gsettings")));
    });
}

void IconThemeProvider::verifyEffective(const QString &expected, int attempts, ChangeCoordinator::Done done) {
    readEffective([this, expected, attempts, done](bool ok, const QString &value, const QString &error) {
        if (ok && value == expected) {
            QString which;
            if (!consumersMatch(expected, &which)) {
                done(false, tr("%1 did not take the new icon pack.").arg(which));
                return;
            }
            done(true, {});
            return;
        }
        if (attempts <= 1) {
            done(false, ok ? tr("The icons did not change. GSettings still says %1.").arg(value) : error);
            return;
        }
        QTimer::singleShot(100, this, [this, expected, attempts, done] { verifyEffective(expected, attempts - 1, done); });
    });
}

// Only puts back what this change wrote: a different value is someone else's by now.
void IconThemeProvider::restoreEffective(const QString &ours, const QString &value, bool unset, ChangeCoordinator::Done done) {
    readEffective([this, ours, value, unset, done](bool ok, const QString &now, const QString &error) {
        if (!ok) {
            done(false, error);
            return;
        }
        // Already there, unless it is only there because this change set it explicitly
        // over a key that had never been set.
        if (now == value && (!unset || ours != value)) {
            done(true, {});
            return;
        }
        if (now != ours) {
            done(false, tr("Something else changed the icons to %1 in the meantime, so they were left as they are.").arg(now));
            return;
        }
        writeEffective(value, unset, done);
    });
}

void IconThemeProvider::readState(StateCallback callback) {
    readEffective([this, callback](bool ok, const QString &value, const QString &error) {
        const Env &env = m_runner->env();
        State state;
        state.known = ok;
        state.error = error;
        state.effective = value;
        state.desktopDefault = IconThemes::desktopDefault(env);
        const Policy policy = loadPolicy();
        state.managed = policy.valid;
        state.unreadablePolicy = policy.exists && !policy.valid;
        state.fixed = policy.fixed;
        state.selected = policy.theme;
        state.hook = hookState();
        state.suspended = policy.valid && loadReapplied().suspended;
        state.consumers = consumers();
        if (policy.fixed)
            state.selectedInstalled = IconThemes::load(env, policy.theme).found;
        if (ok)
            state.effectiveInstalled = IconThemes::load(env, value).found;
        callback(state);
    });
}

void IconThemeProvider::readChoices(ChoicesCallback callback) {
    // Hidden packs are not offered, unless one is what is in use: that has to be visible,
    // and it stays possible to come back to it.
    readEffective([this, callback](bool, const QString &value, const QString &) {
        QVariantList choices;
        for (const IconPack &pack : IconThemes::discover(m_runner->env(), value))
            choices << QVariantMap{{"value", QString::fromLatin1(PackPrefix) + pack.id}, {"label", pack.name}, {"id", pack.id}};
        callback(choices);
    });
}

QStringList IconThemeProvider::plannedFiles() const {
    QStringList files{policyPath(), hookPath()};
    for (const Consumer &consumer : consumers())
        if (consumer.blocked.isEmpty() && !files.contains(consumer.path))
            files << consumer.path;
    return files;
}

QStringList IconThemeProvider::plannedEffects() const {
    QStringList effects;
    if (hookState() == QLatin1String("absent"))
        effects << tr("Adds a theme hook, so your choice survives a theme change with this app closed. Undoing this removes it again.");
    for (const Consumer &consumer : consumers())
        effects << (consumer.blocked.isEmpty() ? tr("Updates the icon theme named in %1.").arg(consumer.label) : consumer.blocked);
    return effects;
}

Transaction IconThemeProvider::failure(const QString &label, const QString &message) const {
    Transaction tx;
    tx.settingId = QStringLiteral("appearance.icons");
    tx.label = label;
    tx.phase = QStringLiteral("failed");
    tx.message = message;
    return tx;
}

void IconThemeProvider::submit(const QString &choice, const QString &kind, const QString &label, Finished finished) {
    readState([this, choice, kind, label, finished](const State &state) {
        if (!state.known) {
            finished(failure(label, tr("The current icons could not be read, so nothing was changed: %1").arg(state.error)));
            return;
        }
        readUnset([this, choice, kind, label, finished, state](bool unset) {
            const Env &env = m_runner->env();
            const QString target = choice == QLatin1String(FollowTheme) ? IconThemes::desktopDefault(env) : choice.mid(int(strlen(PackPrefix)));
            if (choice != QLatin1String(FollowTheme) && !IconThemes::load(env, target).found) {
                finished(failure(label, tr("%1 is not installed, so nothing was changed.").arg(target)));
                return;
            }
            ChangeCoordinator::Request request;
            request.settingId = QStringLiteral("appearance.icons");
            request.label = label;
            request.kind = kind;
            request.before = state.value();
            request.after = choice;
            request.recovery = QStringLiteral("auto-undo");
            request.files = plannedFiles();
            request.extra["gsettings"] = effectiveJson(state.effective, unset, target, false);
            for (const QString &path : std::as_const(request.files))
                request.expectedFingerprints.insert(path, FileOps::fingerprint(path));
            const QByteArray policy = policyBytes(choice);
            const QString before = state.effective;
            request.apply = [this, policy, target, before](ChangeCoordinator::Done done) {
                confirmUnchanged(before, [this, policy, target, done] {
                    QString error;
                    if (!FileOps::atomicWrite(policyPath(), policy, &error, 0644)) {
                        done(false, error);
                        return;
                    }
                    // An edited hook is someone's work. It stays, and the row says what that means.
                    if (hookState() == QLatin1String("absent")) {
                        QDir().mkpath(QFileInfo(hookPath()).absolutePath());
                        if (!FileOps::atomicWrite(hookPath(), hookScript(), &error, 0755)) {
                            done(false, error);
                            return;
                        }
                    }
                    writeEffective(target, false, [this, target, done](bool ok, const QString &writeError) {
                        if (!ok) {
                            done(false, writeError);
                            return;
                        }
                        QString patchError;
                        if (!patchConsumers(target, &patchError)) {
                            done(false, patchError);
                            return;
                        }
                        done(true, {});
                    });
                }, done);
            };
            request.verify = [this, target](ChangeCoordinator::Done done) {
                verifyEffective(target, 8, [this, target, done](bool ok, const QString &error) {
                    if (ok)
                        settle(target);
                    done(ok, error);
                });
            };
            // The coordinator puts the files back; GSettings is this step's to put back.
            request.rollback = [this, target, before, unset](ChangeCoordinator::Done done) { restoreEffective(target, before, unset, done); };
            m_coordinator->submit(std::move(request), finished);
        });
    });
}

void IconThemeProvider::undo(const Transaction &original, Finished finished) {
    const QJsonObject effective = original.extra.value("gsettings").toObject();
    if (effective.isEmpty()) {
        finished(failure(original.label, tr("This change cannot be undone automatically.")));
        return;
    }
    QString before = effective.value("before").toString();
    bool beforeUnset = effective.value("beforeUnset").toBool();
    // Going back to following means following what the theme asks for now. The pack it
    // asked for back then may belong to a theme that has since been replaced.
    const QString desktop = IconThemes::desktopDefault(m_runner->env());
    if (original.before == QLatin1String(FollowTheme) && desktop != before) {
        before = desktop;
        beforeUnset = false;
    }
    const QString after = effective.value("after").toString();
    const bool afterUnset = effective.value("afterUnset").toBool();
    readEffective([=, this](bool ok, const QString &now, const QString &error) {
        if (!ok) {
            finished(failure(original.label, error));
            return;
        }
        const QString changedSince = tr("The value changed after this, so undoing it could overwrite that edit.");
        if (now != after) {
            finished(failure(original.label, changedSince));
            return;
        }
        struct Restore {
            QString path;
            bool wasMissing;
            QByteArray contents;
        };
        QList<Restore> restores;
        QStringList paths;
        for (auto it = original.files.begin(); it != original.files.end(); ++it) {
            const QJsonObject entry = it.value().toObject();
            if (FileOps::fingerprint(it.key()) != entry.value("after").toString()) {
                // Someone's edit to the hook is theirs to keep, and is no reason to refuse
                // the rest. Their copy stays, as it would if they had added it themselves.
                if (it.key() == hookPath())
                    continue;
                finished(failure(original.label, changedSince));
                return;
            }
            paths << it.key();
            const bool wasMissing = entry.value("before").toString() == QLatin1String("missing");
            Restore restore{it.key(), wasMissing, {}};
            if (!wasMissing && !FileOps::readFile(entry.value("backup").toString(), &restore.contents)) {
                finished(failure(original.label, tr("The saved copy of %1 is gone, so this cannot be undone.").arg(QFileInfo(it.key()).fileName())));
                return;
            }
            restores << restore;
        }
        ChangeCoordinator::Request request;
        request.settingId = original.settingId;
        request.label = original.label;
        request.kind = QStringLiteral("undo");
        request.before = original.after;
        request.after = original.before;
        request.recovery = QStringLiteral("auto-undo");
        request.files = paths;
        request.extra["gsettings"] = effectiveJson(after, afterUnset, before, beforeUnset);
        for (const QString &path : std::as_const(paths))
            request.expectedFingerprints.insert(path, FileOps::fingerprint(path));
        request.apply = [this, restores, before, beforeUnset, after](ChangeCoordinator::Done done) {
            confirmUnchanged(after, [this, restores, before, beforeUnset, done] {
                for (const Restore &restore : restores) {
                    QString writeError;
                    if (restore.wasMissing) {
                        // Only reached with the file unchanged since this change made it, so this
                        // removes what was added here and nothing someone else wrote.
                        if (!QFile::remove(restore.path) && QFileInfo::exists(restore.path)) {
                            done(false, tr("%1 could not be removed.").arg(restore.path));
                            return;
                        }
                    } else if (!FileOps::atomicWrite(restore.path, restore.contents, &writeError)) {
                        done(false, writeError);
                        return;
                    }
                }
                writeEffective(before, beforeUnset, done);
            }, done);
        };
        request.verify = [this, before](ChangeCoordinator::Done done) {
            readEffective([this, before, done](bool good, const QString &value, const QString &readError) {
                if (!good) {
                    done(false, readError);
                    return;
                }
                if (value == before)
                    settle(before);
                done(value == before, value == before ? QString() : IconThemeProvider::tr("The icons did not change. GSettings still says %1.").arg(value));
            });
        };
        request.rollback = [this, before, after, afterUnset](ChangeCoordinator::Done done) { restoreEffective(before, after, afterUnset, done); };
        m_coordinator->submit(std::move(request), finished);
    });
}

int IconThemeProvider::reapply(QString *message, const QString &forTheme) {
    const Env &env = m_runner->env();
    if (!loadPolicy().valid) {
        // Nothing was chosen here, so what Omarchy just set stands.
        return 0;
    }
    const auto staleHook = [&env, &forTheme] {
        if (forTheme.isEmpty())
            return false;
        QByteArray name;
        return FileOps::readFile(env.stateHome + "/omarchy/current/theme.name", &name) && QString::fromUtf8(name).trimmed() != forTheme;
    };
    if (staleHook())
        return 0;
    // Quick theme switches leave several of these running. One at a time, each reading the
    // current state once it has its turn.
    ScopedFlock self(env.runtimeDir + "/omarchy-control-icons.lock", LOCK_EX, 15000, true);
    QLockFile changeLock(env.appStateDir + "/change.lock");
    changeLock.setStaleLockTime(5 * 60 * 1000);
    if (!changeLock.tryLock(20000)) {
        *message = tr("Another change is still running, so the icon pack was not reapplied.");
        return 1;
    }
    // Both can have changed while waiting for those locks.
    const Policy policy = loadPolicy();
    if (!policy.valid || staleHook())
        return 0;
    Reapplied state = loadReapplied();
    if (state.suspended) {
        *message = tr("Not reapplying: another icon manager keeps changing this.");
        return 0;
    }
    QString desktop;
    {
        // The theme folder is briefly absent while Omarchy swaps it. Omarchy holds this
        // lock for that swap, and reading through it would look like a theme with no icons.
        ScopedFlock swap(env.runtimeDir + "/omarchy-theme-set.lock", LOCK_SH, 3000, false);
        desktop = IconThemes::desktopDefault(env);
    }
    const QString target = policy.fixed ? policy.theme : desktop;
    if (policy.fixed && !IconThemes::load(env, target).found) {
        *message = tr("Not reapplying: %1 is not installed. Your choice is kept.").arg(target);
        return 0;
    }
    CommandOptions read;
    read.timeoutMs = 5000;
    const CommandResult current = m_runner->runBlocking({"gsettings", "get", Schema, Key}, read);
    bool parsed = false;
    const QString now = current.ok() ? IconThemes::parseGVariantString(current.out, &parsed) : QString();
    if (!parsed) {
        *message = current.describe(QStringLiteral("gsettings"));
        return 1;
    }
    // Omarchy writes the theme's pack just before this runs, and this app wrote the last
    // one. Anything else came from somewhere else.
    if (now != target && now != desktop && now != state.lastApplied) {
        ++state.conflicts;
        if (state.conflicts >= ConflictLimit) {
            state.suspended = true;
            saveReapplied(state);
            *message = tr("Not reapplying: another icon manager keeps changing this.");
            return 0;
        }
    } else {
        state.conflicts = 0;
    }
    if (now != target) {
        CommandOptions write;
        write.mutation = true;
        write.timeoutMs = 8000;
        const CommandResult set = m_runner->runBlocking({"gsettings", "set", Schema, Key, target}, write);
        if (!set.ok()) {
            *message = set.describe(QStringLiteral("gsettings"));
            return 1;
        }
    }
    QString error;
    // A file holding anything but this app's last value was edited by someone, and the row
    // reports the disagreement. Only a deliberate choice overrides that.
    if (!patchConsumers(target, &error, state.lastApplied.isEmpty() ? now : state.lastApplied)) {
        *message = error;
        return 1;
    }
    const CommandResult check = m_runner->runBlocking({"gsettings", "get", Schema, Key}, read);
    bool confirmed = false;
    if (!check.ok() || IconThemes::parseGVariantString(check.out, &confirmed) != target || !confirmed) {
        *message = tr("The icons did not change. GSettings still says %1.").arg(check.outText());
        return 1;
    }
    state.lastApplied = target;
    saveReapplied(state);
    return 0;
}

int IconThemeProvider::removeIntegration(QString *message) {
    QStringList removed;
    if (hookState() == QLatin1String("owned") && QFile::remove(hookPath()))
        removed << hookPath();
    else if (hookState() == QLatin1String("edited"))
        *message = tr("The hook was edited, so it was left alone: %1").arg(hookPath()) + "\n";
    if (QFile::remove(policyPath()))
        removed << policyPath();
    QFile::remove(runtimeStatePath());
    *message += removed.isEmpty() ? tr("Nothing to remove.") : tr("Removed %1. The current icons were left as they are.").arg(removed.join(", "));
    return 0;
}

QVariantMap IconThemeProvider::samples(const QString &id) const {
    const Env &env = m_runner->env();
    QVariantList rows;
    QString borrowedFrom;
    bool anyOwn = false;
    for (const QString &key : IconThemes::sampleKeys()) {
        QString from;
        const QString path = IconThemes::findIcon(env, id, IconThemes::sampleNames(key), SamplePixels, &from);
        QVariantMap row{{"key", key}, {"found", !path.isEmpty()}};
        if (!path.isEmpty()) {
            // The file's age is in the address, so a changed pack draws fresh, not from cache.
            const QString version = QString::number(QFileInfo(path).lastModified().toMSecsSinceEpoch());
            row["url"] = "image://iconpacks/" + QString::fromUtf8(QUrl::toPercentEncoding(id)) + "/" + key + "/" + version;
            row["from"] = from;
            if (from == id)
                anyOwn = true;
            else if (borrowedFrom.isEmpty())
                borrowedFrom = from;
        }
        rows << row;
    }
    return {{"samples", rows}, {"borrowedFrom", anyOwn ? QString() : borrowedFrom}};
}

QVariantList IconThemeProvider::listJson(QStringList *diagnostics) const {
    QVariantList rows;
    for (const IconPack &pack : IconThemes::discover(m_runner->env(), {}, diagnostics))
        rows << QVariantMap{{"id", pack.id}, {"name", pack.name}, {"locations", pack.locations}, {"inherits", pack.inherits}, {"comment", pack.comment}};
    return rows;
}
