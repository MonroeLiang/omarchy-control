#include "settingsengine.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QRegularExpression>
#include <QTimer>
#include <cmath>
#include <memory>

#include "fileops.h"

QVariantMap ChangePlan::toVariant() const {
    QVariantMap m;
    m["ok"] = ok;
    m["error"] = error;
    m["normalized"] = normalized;
    m["confirmText"] = confirmText;
    m["needsOwnership"] = needsOwnership;
    m["effects"] = effects;
    m["files"] = files;
    m["recovery"] = recovery;
    m["restart"] = restart;
    return m;
}

QJsonObject ChangePlan::toJson() const {
    return QJsonObject::fromVariantMap(toVariant());
}

SettingsEngine::SettingsEngine(const Catalog *catalog, CommandRunner *runner, Capabilities *capabilities,
                               ChangeCoordinator *coordinator, Compat *compat, QObject *parent)
    : QObject(parent), m_catalog(catalog), m_runner(runner), m_capabilities(capabilities), m_coordinator(coordinator), m_compat(compat) {}

QStringList SettingsEngine::expandArgv(const QStringList &argv) const {
    QStringList out;
    for (const auto &arg : argv)
        out << ((arg.startsWith("~/") || arg.contains("{config}") || arg.contains("{state}")) ? m_runner->env().expand(arg) : arg);
    return out;
}

static QString mapped(const QJsonObject &map, const QString &value) {
    const auto it = map.constFind(value);
    return it == map.constEnd() ? value : it.value().toString();
}

QString SettingsEngine::parseHyprOption(const QByteArray &json, bool *ok) {
    const auto object = QJsonDocument::fromJson(json).object();
    *ok = true;
    if (object.contains("bool"))
        return object.value("bool").toBool() ? "true" : "false";
    if (object.contains("int"))
        return QString::number(object.value("int").toInt());
    if (object.contains("float")) {
        QString text = QString::number(object.value("float").toDouble(), 'f', 3);
        while (text.endsWith('0'))
            text.chop(1);
        if (text.endsWith('.'))
            text.chop(1);
        return text;
    }
    if (object.contains("str")) {
        const QString text = object.value("str").toString();
        return text == QLatin1String("[[EMPTY]]") ? QString() : text;
    }
    // Gaps are CSS-style, but Omarchy only ever sets all four sides alike.
    if (object.contains("css"))
        return object.value("css").toString().section(' ', 0, 0);
    *ok = false;
    return {};
}

QString SettingsEngine::parseOutput(const CommandResult &result, const QJsonObject &spec, bool *ok) {
    const QString parse = spec.value("parse").toString("trim");
    *ok = true;
    if (parse == QLatin1String("exitBool")) {
        if (result.failure != CommandResult::None) {
            *ok = false;
            return {};
        }
        return result.exitCode == 0 ? "true" : "false";
    }
    if (result.failure != CommandResult::None || (result.exitCode != 0 && !spec.value("allowFailure").toBool())) {
        *ok = false;
        return {};
    }
    QString value;
    if (spec.value("raw").toBool()) {
        value = QString::fromUtf8(result.out);
        while (value.endsWith('\n'))
            value.chop(1);
    } else if (parse == QLatin1String("int")) {
        static const QRegularExpression number("-?[0-9]+");
        const auto match = number.match(QString::fromUtf8(result.out));
        if (!match.hasMatch()) {
            *ok = false;
            return {};
        }
        value = match.captured();
    } else if (parse.startsWith("json:")) {
        QJsonValue field = QJsonDocument::fromJson(result.out).object();
        for (const auto &part : parse.mid(5).split('.'))
            field = field.toObject().value(part);
        if (field.isUndefined()) {
            *ok = false;
            return {};
        }
        value = field.isBool() ? (field.toBool() ? "true" : "false") : field.isDouble() ? QString::number(field.toDouble()) : field.toString();
    } else if (parse.startsWith("field:")) {
        // key<TAB>value rows, as some Omarchy commands print for the shell.
        const QString wanted = parse.mid(6);
        bool found = false;
        for (const auto &line : QString::fromUtf8(result.out).split('\n')) {
            if (line.section('\t', 0, 0) == wanted) {
                value = line.section('\t', 1).trimmed();
                found = true;
            }
        }
        if (!found)
            *ok = false;
    } else if (parse == QLatin1String("activeRow")) {
        for (const auto &line : QString::fromUtf8(result.out).split('\n')) {
            const auto parts = line.split('\t');
            if (parts.size() >= 2 && parts[1].trimmed() == QLatin1String("1"))
                value = parts[0].trimmed();
        }
        if (value.isEmpty())
            *ok = false;
    } else {
        value = result.outText();
    }
    return mapped(spec.value("map").toObject(), value);
}

void SettingsEngine::readFiles(const CatalogEntry &entry, ReadCallback callback) {
    const Env &env = m_runner->env();
    const QString kind = entry.read.value("kind").toString();
    const QString file = env.expand(entry.read.value("file").toString());
    ReadResult result;
    result.status = "ok";
    if (kind == QLatin1String("exists")) {
        const bool exists = QFileInfo::exists(file);
        result.value = exists != entry.read.value("invert").toBool() ? "true" : "false";
    } else if (kind == QLatin1String("filetext")) {
        QByteArray contents;
        if (FileOps::readFile(file, &contents))
            result.value = QString::fromUtf8(contents).section('\n', 0, 0).trimmed();
        else
            result.value = entry.read.value("default").toString();
        result.value = mapped(entry.read.value("map").toObject(), result.value);
    } else if (kind == QLatin1String("symlink")) {
        const QFileInfo info(file);
        if (info.isSymLink())
            result.value = info.canonicalFilePath();
        else {
            result.status = "unknown";
            result.reason = QCoreApplication::translate("SettingsEngine", "No current value was found.");
        }
    } else if (kind == QLatin1String("json")) {
        QStringList path;
        for (const auto &part : entry.read.value("path").toArray())
            path << part.toString();
        QByteArray contents;
        QJsonValue value(QJsonValue::Undefined);
        if (FileOps::readFile(file, &contents)) {
            value = FileOps::jsonGet(contents, path);
            result.source = file;
        }
        if (value.isUndefined() && entry.read.contains("fallbackFile")) {
            QByteArray fallback;
            if (FileOps::readFile(env.expand(entry.read.value("fallbackFile").toString()), &fallback)) {
                value = FileOps::jsonGet(fallback, path);
                result.source = QCoreApplication::translate("SettingsEngine", "Omarchy default");
            }
        }
        if (value.isUndefined()) {
            result.value = entry.read.value("default").toString();
            result.source = QCoreApplication::translate("SettingsEngine", "Omarchy default");
        } else if (value.isDouble()) {
            result.value = QString::number(value.toDouble(), 'g', 10);
            result.isOverride = result.source == file;
        } else if (value.isBool()) {
            result.value = value.toBool() ? "true" : "false";
            result.isOverride = result.source == file;
        } else {
            result.value = value.toString();
            result.isOverride = result.source == file;
        }
    } else if (kind == QLatin1String("toml")) {
        QByteArray contents;
        FileOps::readFile(file, &contents);
        result.value = FileOps::tomlGet(contents, entry.read.value("section").toString(), entry.read.value("key").toString());
        result.isOverride = !result.value.isEmpty();
        if (result.value.isEmpty() && entry.read.contains("fallbackFile")) {
            QByteArray fallback;
            FileOps::readFile(env.expand(entry.read.value("fallbackFile").toString()), &fallback);
            result.value = FileOps::tomlGet(fallback, entry.read.value("section").toString(), entry.read.value("key").toString());
        }
        if (result.value.isEmpty())
            result.value = entry.read.value("default").toString();
        if (result.value.isEmpty()) {
            result.status = "unknown";
            result.reason = QCoreApplication::translate("SettingsEngine", "No current value was found.");
        }
    }
    callback(result);
}

void SettingsEngine::read(const QString &id, ReadCallback callback) {
    const CatalogEntry *entry = m_catalog->find(id);
    if (!entry || entry->read.isEmpty()) {
        ReadResult result;
        result.reason = QCoreApplication::translate("SettingsEngine", "This setting has no way to read its value.");
        callback(result);
        return;
    }
    const auto availability = m_capabilities->availability(*entry);
    if (!availability.available) {
        ReadResult result;
        result.status = "unavailable";
        result.reason = availability.reason;
        callback(result);
        return;
    }
    if (entry->read.value("kind").toString() == QLatin1String("icons")) {
        if (!m_icons) {
            ReadResult result;
            result.status = "unavailable";
            callback(result);
            return;
        }
        m_icons->readState([callback](const IconThemeProvider::State &state) {
            ReadResult result;
            result.extra = state.toVariant();
            if (!state.known) {
                result.reason = state.error;
                callback(result);
                return;
            }
            result.status = "ok";
            result.value = state.value();
            result.reason = state.reason();
            result.isOverride = state.managed && state.fixed;
            result.source = state.effective;
            callback(result);
        });
        return;
    }
    const QString kind = entry->read.value("kind").toString();
    if (kind == QLatin1String("hypr")) {
        const QStringList argv = m_runner->env().hyprctl({"-j", "getoption", entry->read.value("option").toString()});
        const CatalogEntry *e = entry;
        m_runner->run(argv, CommandOptions{3000}, this, [this, e, callback](const CommandResult &command) {
            ReadResult result;
            bool ok = false;
            const QString value = command.ok() ? parseHyprOption(command.out, &ok) : QString();
            if (!ok) {
                result.reason = command.describe("Hyprland");
                callback(result);
                return;
            }
            result.status = "ok";
            result.value = value;
            HyprModule module(m_runner->env());
            QStringList path;
            for (const auto &part : e->write.value("path").toArray())
                path << part.toString();
            const auto moduleState = module.state();
            if (moduleState == HyprModule::ModifiedByUser) {
                result.status = "custom";
                result.source = QCoreApplication::translate("SettingsEngine", "Custom configuration");
                result.reason = QCoreApplication::translate("SettingsEngine", "omarchy-control.lua was edited by hand, so changes here are paused.");
            } else if (module.load() && module.hasOption(path)) {
                result.source = QCoreApplication::translate("SettingsEngine", "Set here");
                result.isOverride = true;
            } else {
                result.source = QCoreApplication::translate("SettingsEngine", "Omarchy default or your config");
            }
            callback(result);
        });
        return;
    }
    if (kind == QLatin1String("cmd")) {
        QStringList argv;
        for (const auto &part : entry->read.value("argv").toArray())
            argv << part.toString();
        const QString target = Catalog::targetOf(id);
        argv = targeted(argv, *entry, target);
        CommandOptions options;
        options.timeoutMs = entry->read.value("timeout").toInt(8000);
        const QJsonObject spec = entry->read;
        m_runner->run(expandArgv(argv), options, this, [callback, spec, argv, target](const CommandResult &command) {
            ReadResult result;
            bool ok = false;
            const QString value = SettingsEngine::parseOutput(command, spec, &ok);
            if (ok) {
                result.status = "ok";
                result.value = value;
            } else if (!target.isEmpty() && spec.contains("unsupported") && command.failure == CommandResult::None) {
                // The command ran and said no: this device has no such control, which is
                // a fact about the hardware rather than a failure to read it.
                result.status = "unavailable";
                result.reason = QCoreApplication::translate("catalog", spec.value("unsupported").toString().toUtf8().constData()).replace("{target}", target);
            } else {
                result.reason = command.describe(argv.value(0));
            }
            callback(result);
        });
        return;
    }
    readFiles(*entry, std::move(callback));
}

void SettingsEngine::readChoices(const QString &id, ChoicesCallback callback) {
    const CatalogEntry *entry = m_catalog->find(id);
    if (!entry) {
        callback({});
        return;
    }
    QVariantList choices;
    for (const auto &value : entry->choices) {
        const auto o = value.toObject();
        // A default-app setter installs whatever is missing, so a missing app is
        // not a choice here; installing stays a separate, explicit action.
        if (o.contains("requiresCommand") && !m_capabilities->hasCommand(o.value("requiresCommand").toString()))
            continue;
        choices << QVariantMap{{"value", o.value("value").toString()}, {"label", o.value("label").toString()}};
    }
    const QString kind = entry->choicesFrom.value("kind").toString();
    if (kind == QLatin1String("icons")) {
        if (!m_icons) {
            callback(choices);
            return;
        }
        m_icons->readChoices([callback, choices](const QVariantList &packs) mutable {
            callback(choices + packs);
        });
        return;
    }
    if (kind == QLatin1String("cmd")) {
        QStringList argv;
        for (const auto &part : entry->choicesFrom.value("argv").toArray())
            argv << part.toString();
        const QJsonObject labels = entry->choicesFrom.value("labels").toObject();
        m_runner->run(argv, CommandOptions{5000}, this, [callback, choices, labels](const CommandResult &result) mutable {
            if (result.ok()) {
                for (const auto &line : QString::fromUtf8(result.out).split('\n', Qt::SkipEmptyParts)) {
                    const QString value = line.trimmed();
                    choices << QVariantMap{{"value", value}, {"label", labels.value(value).toString(value)}};
                }
            }
            callback(choices);
        });
        return;
    }
    if (kind == QLatin1String("dir")) {
        const Env &env = m_runner->env();
        QByteArray nameBytes;
        FileOps::readFile(env.stateHome + "/omarchy/current/theme.name", &nameBytes);
        const QString theme = QString::fromUtf8(nameBytes).trimmed();
        QStringList filters;
        for (const auto &extension : entry->choicesFrom.value("extensions").toArray())
            filters << "*." + extension.toString();
        QStringList seen;
        for (const auto &dirValue : entry->choicesFrom.value("dirs").toArray()) {
            QString path = env.expand(dirValue.toString());
            path.replace("{theme}", theme);
            const QDir dir(path);
            for (const auto &file : dir.entryInfoList(filters, QDir::Files | QDir::Readable, QDir::Name)) {
                const QString canonical = file.canonicalFilePath();
                if (seen.contains(canonical))
                    continue;
                seen << canonical;
                choices << QVariantMap{{"value", canonical}, {"label", file.completeBaseName()}};
            }
        }
    } else if (kind == QLatin1String("locales")) {
        QByteArray contents;
        FileOps::readFile(m_runner->env().omarchyPath + "/default/i18n/locales.json", &contents);
        const auto object = QJsonDocument::fromJson(contents).object();
        for (auto it = object.begin(); it != object.end(); ++it) {
            const auto locale = it.value().toObject();
            choices << QVariantMap{{"value", it.key()}, {"label", QString("%1 (%2)").arg(locale.value("name").toString(), locale.value("englishName").toString())}};
        }
    }
    callback(choices);
}

ChangePlan SettingsEngine::plan(const QString &id, const QString &rawValue, const QVariantList &knownChoices) const {
    ChangePlan plan;
    const CatalogEntry *entry = m_catalog->find(id);
    const auto tr = [](const char *text) { return QCoreApplication::translate("SettingsEngine", text); };
    if (!entry) {
        plan.error = tr("Unknown setting.");
        return plan;
    }
    if (!entry->isEditable()) {
        plan.error = tr("This cannot be changed here.");
        return plan;
    }
    const auto availability = m_capabilities->availability(*entry);
    if (!availability.available) {
        plan.error = availability.reason;
        return plan;
    }

    // Reading is always fine. A write stays paused if the command behind it has
    // changed since it was reviewed, until someone has looked.
    if (m_compat) {
        const QString changed = m_compat->changedFor(*entry, m_capabilities->versions().value("hyprland").toString());
        if (!changed.isEmpty()) {
            plan.error = tr("Omarchy's %1 changed since this was tested, so changes here are paused. Review it under Advanced.").arg(changed);
            return plan;
        }
    }

    QString value = rawValue.trimmed();
    if (entry->type == QLatin1String("text") || value != rawValue)
        value = entry->type == QLatin1String("choice") ? rawValue : value;
    const bool iconPack = entry->write.value("kind").toString() == QLatin1String("icons");
    if (iconPack)
        value = IconThemeProvider::normalizeChoice(value);
    if (entry->type == QLatin1String("toggle")) {
        if (value != QLatin1String("true") && value != QLatin1String("false")) {
            plan.error = tr("Expected on or off.");
            return plan;
        }
    } else if (entry->type == QLatin1String("int") || entry->type == QLatin1String("slider")) {
        bool ok = false;
        const int number = value.toInt(&ok);
        if (!ok || (entry->hasRange && (number < entry->min || number > entry->max))) {
            plan.error = tr("Choose a whole number from %1 to %2.").arg(entry->min).arg(entry->max);
            return plan;
        }
    } else if (entry->type == QLatin1String("float")) {
        bool ok = false;
        const double number = value.toDouble(&ok);
        if (!ok || std::isnan(number) || (entry->hasRange && (number < entry->min || number > entry->max))) {
            plan.error = tr("Choose a number from %1 to %2.").arg(entry->min).arg(entry->max);
            return plan;
        }
    } else if (entry->type == QLatin1String("choice")) {
        bool known = false;
        for (const auto &choice : entry->choices) {
            const auto object = choice.toObject();
            if (object.value("value").toString() != value)
                continue;
            // A default-app setter opens an installer when the app is missing, so it
            // is refused here for the CLI and for an undo, not just hidden in the list.
            if (object.contains("requiresCommand") && !m_capabilities->hasCommand(object.value("requiresCommand").toString())) {
                plan.error = tr("That app is not installed. Install it first under Apps & defaults.");
                return plan;
            }
            known = true;
        }
        for (const auto &choice : knownChoices)
            if (choice.toMap().value("value").toString() == value)
                known = true;
        const bool liveList = !entry->choicesFrom.isEmpty();
        if (!known && !entry->allowCustomValue && !(liveList && knownChoices.isEmpty())) {
            plan.error = tr("That is not one of the available choices.");
            return plan;
        }
        // Omarchy's own setters run sed over these values, so a live-list value
        // whose list could not be read is refused rather than passed along.
        if (!known && liveList && knownChoices.isEmpty() && !entry->allowCustomValue) {
            plan.error = tr("The list of choices could not be read, so this was not changed.");
            return plan;
        }
    }
    if (!entry->pattern.isEmpty()) {
        const QRegularExpression pattern(entry->pattern);
        if (!pattern.match(value).hasMatch()) {
            plan.error = tr("That value is not in the expected format.");
            return plan;
        }
    }
    // Control characters are never part of a real value and could confuse the
    // text formats Omarchy's scripts edit. Stacked clock formats need newlines.
    for (const QChar c : value) {
        if (c.category() == QChar::Other_Control && !(c == QLatin1Char('\n') && id == QLatin1String("desktop.clock.vertical"))) {
            plan.error = tr("That value contains characters that are not allowed.");
            return plan;
        }
    }

    plan.normalized = value;
    plan.effects = entry->effects;
    plan.recovery = entry->recovery;
    plan.restart = entry->restart;
    for (const auto &file : entry->files)
        plan.files << m_runner->env().expand(file);
    if (iconPack && m_icons) {
        plan.files = m_icons->plannedFiles();
        plan.effects += m_icons->plannedEffects();
    }

    if (!entry->confirmAlways.isEmpty())
        plan.confirmText = entry->confirmAlways;
    else if (entry->confirmWhen.contains(value))
        plan.confirmText = entry->confirmWhen.value(value).toString();

    if (id == QLatin1String("displays.scale")) {
        // A saved layout loads after monitors.lua, where this command writes, so the
        // change would quietly vanish at the next reload.
        HyprModule module(m_runner->env());
        if (module.load() && !module.monitors().isEmpty()) {
            plan.error = tr("Your saved display layout sets the scale. Change it under Arrangement.");
            return plan;
        }
    }

    if (entry->write.value("kind").toString() == QLatin1String("hypr")) {
        HyprModule module(m_runner->env());
        plan.files << module.path();
        if (module.state() == HyprModule::ModifiedByUser) {
            plan.error = tr("omarchy-control.lua was edited by hand, so this is paused. Edit it directly under Advanced.");
            return plan;
        }
        if (!module.importInstalled()) {
            if (!module.canInstallImport()) {
                plan.error = tr("hyprland.lua does not have Omarchy's stock layout, so this can only be changed by editing it directly under Advanced.");
                return plan;
            }
            plan.needsOwnership = true;
            plan.files << module.hyprlandPath();
            plan.confirmText = tr("To save this, Omarchy Control adds one marked line to hyprland.lua and keeps your choices in omarchy-control.lua. It loads after your own files and before Omarchy's toggles. Nothing else in your config changes.");
        }
    }
    plan.ok = true;
    return plan;
}

bool SettingsEngine::valuesMatch(const CatalogEntry &entry, const QString &a, const QString &b) {
    if (a == b)
        return true;
    bool okA = false, okB = false;
    const double numberA = a.toDouble(&okA), numberB = b.toDouble(&okB);
    if (!okA || !okB)
        return false;
    if (entry.type == QLatin1String("slider"))
        return std::abs(numberA - numberB) <= 2;
    return std::abs(numberA - numberB) < 0.0005;
}

QStringList SettingsEngine::targeted(const QStringList &argv, const CatalogEntry &entry, const QString &target) {
    QStringList out;
    for (const auto &part : argv) {
        if (part != QLatin1String("{targetArgs}")) {
            out << part;
            continue;
        }
        if (target.isEmpty())
            continue;
        for (QString arg : entry.targetArgs)
            out << arg.replace("{target}", target);
    }
    return out;
}

QStringList SettingsEngine::substitute(const QJsonArray &argv, const QString &value, const CatalogEntry *entry, const QString &target) const {
    // Paths expand before the value goes in, so text someone typed stays literal.
    QStringList templates;
    for (const auto &part : argv)
        templates << part.toString();
    if (entry)
        templates = targeted(templates, *entry, target);
    QStringList out = expandArgv(templates);
    for (auto &text : out)
        text.replace("{value}", value);
    return out;
}

void SettingsEngine::verifyValue(const QString &id, const QString &expected, int attempts, ChangeCoordinator::Done done) {
    read(id, [this, id, expected, attempts, done](const ReadResult &result) {
        const CatalogEntry *entry = m_catalog->find(id);
        if (entry && (result.status == QLatin1String("ok") || result.status == QLatin1String("custom"))) {
            bool near = false;
            // Hyprland rounds a scale to one the mode can show, so the value it settles
            // on is the true result, as long as it is close to what was asked for.
            if (entry->write.value("verify").toString() == QLatin1String("near"))
                near = std::abs(result.value.toDouble() - expected.toDouble()) <= 0.2;
            if (near || valuesMatch(*entry, result.value, expected)) {
                done(true, {});
                return;
            }
        }
        if (attempts <= 1) {
            if (result.status != QLatin1String("ok") && !result.reason.isEmpty()) {
                done(false, QCoreApplication::translate("SettingsEngine", "The result could not be confirmed: %1").arg(result.reason));
                return;
            }
            done(false, QCoreApplication::translate("SettingsEngine", "The value did not change. Another part of your configuration may set it."));
            return;
        }
        QTimer::singleShot(250, this, [this, id, expected, attempts, done] { verifyValue(id, expected, attempts - 1, done); });
    });
}

void SettingsEngine::applyCommand(const CatalogEntry &entry, const QJsonObject &write, const QString &value, const QString &before,
                                  const QString &target, ChangeCoordinator::Done done) {
    QString mappedValue = mapped(write.value("map").toObject(), value);
    QJsonArray argv = write.value("argv").toArray();
    if (value.isEmpty() && write.contains("clear"))
        argv = write.value("clear").toArray();
    // Some Omarchy commands only toggle: run one only when it would move toward
    // the requested state, which is the read-then-act race the plan calls out.
    if (write.value("toggleOnly").toBool() && before == value) {
        done(true, {});
        return;
    }
    CommandOptions options;
    options.timeoutMs = write.value("timeout").toInt(15000);
    options.mutation = true;
    const QStringList command = substitute(argv, mappedValue, &entry, target);
    const QJsonArray after = write.value("after").toArray();
    const QString label = entry.label;
    m_runner->run(command, options, this, [this, done, after, command](const CommandResult &result) {
        if (!result.ok()) {
            done(false, result.describe(command.value(0)));
            return;
        }
        if (!after.isEmpty()) {
            CommandOptions best;
            best.mutation = true;
            best.timeoutMs = 5000;
            // Telling the shell to look again is best effort; verification decides.
            m_runner->run(substitute(after, {}), best, this, [done](const CommandResult &) { done(true, {}); });
            return;
        }
        done(true, {});
    });
}

void SettingsEngine::apply(const QString &id, const QString &value, ApplyCallback callback, const QString &kind) {
    const CatalogEntry *entry = m_catalog->find(id);
    if (!entry) {
        Transaction tx;
        tx.phase = "failed";
        tx.message = QCoreApplication::translate("SettingsEngine", "Unknown setting.");
        callback(tx);
        return;
    }
    // The value is rechecked against a fresh list on every apply, including one
    // from the CLI, so a stale plan cannot pass something no longer offered.
    readChoices(id, [this, id, value, callback, kind, entry](const QVariantList &choices) {
        m_lastChoices.insert(id, choices);
        const ChangePlan plan = this->plan(id, value, choices);
        if (!plan.ok) {
            Transaction tx;
            tx.settingId = id;
            tx.label = entry->label;
            tx.phase = "failed";
            tx.message = plan.error;
            callback(tx);
            return;
        }
        read(id, [this, id, entry, plan, callback, kind](const ReadResult &current) {
            const bool known = current.status == QLatin1String("ok") || current.status == QLatin1String("custom");
            const QString writeKind = entry->write.value("kind").toString();
            if (writeKind == QLatin1String("icons")) {
                if (m_icons)
                    m_icons->submit(plan.normalized, kind, entry->label, callback);
                return;
            }
            if (writeKind == QLatin1String("hypr")) {
                const QString after = plan.normalized;
                const bool remove = after.isEmpty();
                submitHypr(*entry, after, current.value, !current.isOverride, remove, kind, callback);
                return;
            }
            if (writeKind == QLatin1String("json")) {
                submitJson(*entry, plan.normalized, current.value, !current.isOverride, false, kind, callback);
                return;
            }
            // Offering undo means promising the old value can be set again. Some cannot:
            // a custom DNS list, an app since removed, a preference never saved.
            const bool restorable = known && this->plan(id, current.value, m_lastChoices.value(id)).ok;
            ChangeCoordinator::Request request;
            request.settingId = id;
            request.label = entry->label;
            request.kind = kind;
            request.before = known ? current.value : QString();
            request.after = plan.normalized;
            request.recovery = restorable ? entry->recovery : QStringLiteral("no-undo");
            request.beforeWasDefault = !current.isOverride && !entry->reset.isEmpty();
            for (const auto &file : plan.files)
                request.files << file;
            const QJsonObject write = entry->write;
            const QString value = plan.normalized;
            const QString before = current.value;
            const CatalogEntry *e = entry;
            request.apply = [this, id, e, write, value, before](ChangeCoordinator::Done done) {
                applyCommand(*e, write, value, before, Catalog::targetOf(id), [this, id, e, value, before, done](bool ok, const QString &error) {
                    if (ok || valuesMatch(*e, before, value)) {
                        done(ok, error);
                        return;
                    }
                    // Exit status is not the whole story. Several Omarchy scripts end by
                    // sending a notification, so they exit 1 after doing their work when
                    // the shell is down. Only the value itself can say what happened.
                    read(id, [e, value, error, done](const ReadResult &now) {
                        done(now.status == QLatin1String("ok") && valuesMatch(*e, now.value, value), error);
                    });
                });
            };
            request.verify = [this, id, value](ChangeCoordinator::Done done) { verifyValue(id, value, 12, done); };
            if (restorable && before != value && entry->recovery != QLatin1String("no-undo")) {
                request.rollback = [this, id, e, write, before](ChangeCoordinator::Done done) {
                    // Look first: the failed attempt may not have changed anything, and
                    // a toggle-only command would then flip it the wrong way.
                    read(id, [this, id, e, write, before, done](const ReadResult &now) {
                        if (now.status == QLatin1String("ok") && valuesMatch(*e, now.value, before)) {
                            done(true, {});
                            return;
                        }
                        // Going back through the same command keeps Omarchy's own side
                        // effects consistent, and only a read-back proves it worked.
                        applyCommand(*e, write, before, now.value, Catalog::targetOf(id), [this, id, before, done](bool ok, const QString &error) {
                            // Same reasoning as above: a nonzero exit is not the last word here
                            // either, and the read-back below decides.
                            Q_UNUSED(ok);
                            verifyValue(id, before, 8, [error, done](bool restored, const QString &verifyError) {
                                done(restored, restored ? QString() : (error.isEmpty() ? verifyError : error));
                            });
                        });
                    });
                };
            }
            if (entry->write.value("verify").toString() == QLatin1String("near")) {
                auto settled = std::make_shared<QString>(plan.normalized);
                const auto check = request.verify;
                request.verify = [this, id, check, settled](ChangeCoordinator::Done done) {
                    check([this, id, settled, done](bool ok, const QString &error) {
                        if (!ok) {
                            done(false, error);
                            return;
                        }
                        read(id, [settled, done](const ReadResult &now) {
                            if (now.status == QLatin1String("ok"))
                                *settled = now.value;
                            done(true, {});
                        });
                    });
                };
                request.settled = [settled] { return *settled; };
            }
            m_coordinator->submit(std::move(request), callback);
        });
    });
}

static QJsonValue typedValue(const CatalogEntry &entry, const QString &value) {
    if (entry.type == QLatin1String("toggle"))
        return value == QLatin1String("true");
    bool ok = false;
    const qlonglong integer = value.toLongLong(&ok);
    if (ok)
        return double(integer);
    const double number = value.toDouble(&ok);
    if (ok)
        return number;
    return value;
}

void SettingsEngine::submitJson(const CatalogEntry &entry, const QString &value, const QString &before, bool beforeWasDefault, bool remove,
                                const QString &kind, ApplyCallback callback) {
    const Env &env = m_runner->env();
    const QString file = env.expand(entry.write.value("file").toString());
    QStringList path;
    for (const auto &part : entry.write.value("path").toArray())
        path << part.toString();
    ChangeCoordinator::Request request;
    request.settingId = entry.id;
    request.label = entry.label;
    request.kind = kind;
    request.before = before;
    request.after = value;
    request.recovery = entry.recovery;
    request.beforeWasDefault = beforeWasDefault;
    request.files << file;
    const QJsonObject write = entry.write;
    const QString fallback = entry.read.value("fallbackFile").toString();
    const QString id = entry.id;
    const QJsonValue typed = write.value("valueType").toString() == QLatin1String("int") ? QJsonValue(value.toDouble()) : QJsonValue(value);
    request.apply = [this, file, path, typed, write, remove, fallback](ChangeCoordinator::Done done) {
        QByteArray document;
        const Env &env = m_runner->env();
        // A missing or empty config starts from Omarchy's defaults, exactly as
        // its own setters do, so the new key does not shadow everything else.
        if ((!FileOps::readFile(file, &document) || document.trimmed().isEmpty()) && !fallback.isEmpty())
            FileOps::readFile(env.expand(fallback), &document);
        QString error;
        const bool edited = remove ? FileOps::jsonRemove(&document, path, &error) : FileOps::jsonSet(&document, path, typed, &error);
        if (!edited || !FileOps::atomicWrite(file, document, &error)) {
            done(false, error);
            return;
        }
        const QJsonArray after = write.value("after").toArray();
        if (after.isEmpty()) {
            done(true, {});
            return;
        }
        CommandOptions options;
        options.mutation = true;
        options.timeoutMs = 5000;
        m_runner->run(substitute(after, {}), options, this, [done](const CommandResult &) { done(true, {}); });
    };
    auto settled = std::make_shared<QString>(value);
    if (!remove) {
        request.verify = [this, id, value](ChangeCoordinator::Done done) { verifyValue(id, value, 4, done); };
    } else {
        request.verify = [this, id, settled](ChangeCoordinator::Done done) { confirmRead(id, settled, done); };
        request.settled = [settled] { return *settled; };
    }
    const QJsonArray after = write.value("after").toArray();
    request.rollback = [this, after](ChangeCoordinator::Done done) {
        if (after.isEmpty()) {
            done(true, {});
            return;
        }
        CommandOptions options;
        options.mutation = true;
        options.timeoutMs = 5000;
        m_runner->run(substitute(after, {}), options, this, [done](const CommandResult &) { done(true, {}); });
    };
    m_coordinator->submit(std::move(request), callback);
}

void SettingsEngine::confirmRead(const QString &id, std::shared_ptr<QString> settled, ChangeCoordinator::Done done) {
    // A return to default has no expected value, but it must still read back cleanly,
    // and undo needs to know what it left behind.
    read(id, [settled, done](const ReadResult &now) {
        if (now.status != QLatin1String("ok") && now.status != QLatin1String("custom")) {
            done(false, QCoreApplication::translate("SettingsEngine", "The result could not be confirmed: %1").arg(now.reason));
            return;
        }
        *settled = now.value;
        done(true, {});
    });
}

void SettingsEngine::reloadHyprland(ChangeCoordinator::Done done, const QStringList &baseline, bool checkErrors) {
    CommandOptions options;
    options.mutation = true;
    options.timeoutMs = 5000;
    m_runner->run(m_runner->env().hyprctl({"reload"}), options, this, [this, done, baseline, checkErrors](const CommandResult &reload) {
        if (!reload.ok()) {
            done(false, reload.describe("Hyprland"));
            return;
        }
        if (!checkErrors) {
            done(true, {});
            return;
        }
        // Compare against the errors that already existed, so someone with an
        // unrelated config problem can still change a setting.
        QTimer::singleShot(150, this, [this, done, baseline] {
            m_runner->run(m_runner->env().hyprctl({"-j", "configerrors"}), CommandOptions{3000}, this, [done, baseline](const CommandResult &errors) {
                QStringList introduced;
                for (const auto &value : QJsonDocument::fromJson(errors.out).array()) {
                    const QString text = value.toString().trimmed();
                    if (!text.isEmpty() && !baseline.contains(text))
                        introduced << text;
                }
                if (!introduced.isEmpty()) {
                    done(false, QCoreApplication::translate("SettingsEngine", "Hyprland reported a config error: %1").arg(introduced.first().left(240)));
                    return;
                }
                done(true, {});
            });
        });
    });
}

void SettingsEngine::submitHypr(const CatalogEntry &entry, const QString &value, const QString &before, bool beforeWasDefault, bool remove,
                                const QString &kind, ApplyCallback callback) {
    QStringList path;
    for (const auto &part : entry.write.value("path").toArray()) {
        path << part.toString();
        // Keys come from the reviewed catalog, but this file is executable Lua,
        // so the check also sits at the point of generating it.
        if (!HyprModule::isSafeKey(part.toString())) {
            Transaction tx;
            tx.phase = "failed";
            tx.message = QStringLiteral("Unsafe option key");
            callback(tx);
            return;
        }
    }
    HyprModule probe(m_runner->env());
    ChangeCoordinator::Request request;
    request.settingId = entry.id;
    request.label = entry.label;
    request.kind = kind;
    request.before = before;
    request.after = value;
    request.recovery = entry.recovery;
    request.beforeWasDefault = beforeWasDefault;
    request.files << probe.path() << probe.hyprlandPath();
    const CatalogEntry *e = &entry;
    const QString id = entry.id;

    auto baseline = std::make_shared<QStringList>();
    request.apply = [this, e, path, value, remove, baseline](ChangeCoordinator::Done done) {
        m_runner->run(m_runner->env().hyprctl({"-j", "configerrors"}), CommandOptions{3000}, this, [this, e, path, value, remove, baseline, done](const CommandResult &errors) {
            for (const auto &item : QJsonDocument::fromJson(errors.out).array())
                if (!item.toString().trimmed().isEmpty())
                    baseline->append(item.toString().trimmed());
            HyprModule module(m_runner->env());
            QString error;
            if (!module.load(&error)) {
                done(false, error);
                return;
            }
            if (remove)
                module.removeOption(path);
            else
                module.setOption(path, typedValue(*e, value));
            // Removing a value needs no new ownership. Only setting one adds the import.
            if (!module.save(&error) || (!remove && !module.installImport(&error))) {
                done(false, error);
                return;
            }
            reloadHyprland(done, *baseline, true);
        });
    };
    if (!remove) {
        request.verify = [this, id, value](ChangeCoordinator::Done done) {
            verifyValue(id, value, 8, [done](bool ok, const QString &error) {
                if (ok) {
                    done(true, {});
                    return;
                }
                done(false, error.isEmpty() ? QCoreApplication::translate("SettingsEngine", "Another part of your Hyprland config sets this value.") : error);
            });
        };
    } else {
        auto settled = std::make_shared<QString>();
        request.verify = [this, id, settled](ChangeCoordinator::Done done) { confirmRead(id, settled, done); };
        request.settled = [settled] { return *settled; };
    }
    request.rollback = [this](ChangeCoordinator::Done done) {
        // The coordinator restored the files already; bring the compositor along.
        reloadHyprland(done, {}, false);
    };
    m_coordinator->submit(std::move(request), callback);
}

void SettingsEngine::reset(const QString &id, ApplyCallback callback) {
    const CatalogEntry *entry = m_catalog->find(id);
    Transaction failed;
    failed.settingId = id;
    failed.phase = "failed";
    if (!entry) {
        failed.message = QCoreApplication::translate("SettingsEngine", "Unknown setting.");
        callback(failed);
        return;
    }
    failed.label = entry->label;
    const QString writeKind = entry->write.value("kind").toString();
    if (writeKind == QLatin1String("icons")) {
        apply(id, QString::fromLatin1(IconThemeProvider::FollowTheme), std::move(callback), QStringLiteral("reset"));
        return;
    }
    read(id, [this, id, entry, writeKind, callback, failed](const ReadResult &current) {
        if (writeKind == QLatin1String("hypr")) {
            HyprModule module(m_runner->env());
            if (module.state() == HyprModule::ModifiedByUser) {
                Transaction tx = failed;
                tx.message = QCoreApplication::translate("SettingsEngine", "omarchy-control.lua was edited by hand, so it was left alone.");
                callback(tx);
                return;
            }
            QStringList path;
            for (const auto &part : entry->write.value("path").toArray())
                path << part.toString();
            if (!module.load() || !module.hasOption(path)) {
                // Nothing to remove, and no reason to touch the user's config.
                Transaction tx = failed;
                tx.message = QCoreApplication::translate("SettingsEngine", "This is already using the default.");
                callback(tx);
                return;
            }
            submitHypr(*entry, {}, current.value, false, true, "reset", callback);
            return;
        }
        if (writeKind == QLatin1String("json")) {
            submitJson(*entry, {}, current.value, false, true, "reset", callback);
            return;
        }
        if (entry->reset.isEmpty()) {
            Transaction tx = failed;
            tx.message = QCoreApplication::translate("SettingsEngine", "This setting has no separate default to return to.");
            callback(tx);
            return;
        }
        ChangeCoordinator::Request request;
        request.settingId = id;
        request.label = entry->label;
        request.kind = "reset";
        request.before = current.value;
        request.recovery = entry->recovery;
        QStringList argv;
        for (const auto &part : entry->reset)
            argv << part.toString();
        request.apply = [this, argv](ChangeCoordinator::Done done) {
            CommandOptions options;
            options.mutation = true;
            options.timeoutMs = 20000;
            m_runner->run(argv, options, this, [done, argv](const CommandResult &result) { done(result.ok(), result.describe(argv.value(0))); });
        };
        auto settled = std::make_shared<QString>();
        request.verify = [this, id, settled](ChangeCoordinator::Done done) { confirmRead(id, settled, done); };
        request.settled = [settled] { return *settled; };
        m_coordinator->submit(std::move(request), callback);
    });
}

void SettingsEngine::undo(const QString &transactionId, ApplyCallback callback) {
    const Transaction original = m_coordinator->transaction(transactionId);
    Transaction failed;
    failed.settingId = original.settingId;
    failed.label = original.label;
    failed.phase = "failed";
    if (original.id.isEmpty() || !original.undoable()) {
        failed.message = QCoreApplication::translate("SettingsEngine", "This change cannot be undone automatically.");
        callback(failed);
        return;
    }
    const CatalogEntry *entry = m_catalog->find(original.settingId);
    if (!entry) {
        failed.message = QCoreApplication::translate("SettingsEngine", "This setting no longer exists.");
        callback(failed);
        return;
    }
    if (entry->write.value("kind").toString() == QLatin1String("icons")) {
        if (!m_icons) {
            failed.message = QCoreApplication::translate("SettingsEngine", "This change cannot be undone automatically.");
            callback(failed);
            return;
        }
        m_icons->undo(original, [this, callback, transactionId](const Transaction &result) {
            if (result.phase == QLatin1String("committed"))
                m_coordinator->markUndoneBy(transactionId, result.id);
            callback(result);
        });
        return;
    }
    // Undo only goes back from the state this change left. If anything has
    // edited the value since, that later edit is not ours to overwrite.
    read(original.settingId, [this, entry, original, callback, failed, transactionId](const ReadResult &current) {
        if (!valuesMatch(*entry, current.value, original.after)) {
            Transaction tx = failed;
            tx.message = QCoreApplication::translate("SettingsEngine", "The value changed after this, so undoing it could overwrite that edit.");
            callback(tx);
            return;
        }
        auto finish = [this, callback, transactionId](const Transaction &result) {
            if (result.phase == QLatin1String("committed"))
                m_coordinator->markUndoneBy(transactionId, result.id);
            callback(result);
        };
        if (original.beforeWasDefault && (entry->owned || entry->write.value("kind").toString() == QLatin1String("json") || !entry->reset.isEmpty()))
            reset(original.settingId, finish);
        else
            apply(original.settingId, original.before, finish, "undo");
    });
}

QString SettingsEngine::displayValue(const CatalogEntry &entry, const QString &value, const QVariantList &choices) const {
    const auto it = entry.valueMap.constFind(value);
    if (it != entry.valueMap.constEnd())
        return QCoreApplication::translate("catalog", it.value().toString().toUtf8().constData());
    for (const auto &choice : choices) {
        const auto map = choice.toMap();
        if (map.value("value").toString() == value)
            return QCoreApplication::translate("catalog", map.value("label").toString().toUtf8().constData());
    }
    if (entry.type == QLatin1String("toggle"))
        return value == QLatin1String("true") ? QCoreApplication::translate("SettingsEngine", "On") : QCoreApplication::translate("SettingsEngine", "Off");
    if (value.isEmpty() && !entry.placeholder.isEmpty())
        return QCoreApplication::translate("catalog", entry.placeholder.toUtf8().constData());
    return entry.unit.isEmpty() ? value : value + " " + entry.unit;
}
