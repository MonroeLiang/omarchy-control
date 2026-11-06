#include "shortcutsprovider.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <algorithm>

#include "hypromodule.h"

ShortcutsProvider::ShortcutsProvider(CommandRunner *runner, ChangeCoordinator *coordinator, QObject *parent)
    : QObject(parent), m_runner(runner), m_coordinator(coordinator) {}

QString ShortcutsProvider::modsToText(int mask) {
    QStringList mods;
    if (mask & 64) mods << "SUPER";
    if (mask & 4) mods << "CTRL";
    if (mask & 8) mods << "ALT";
    if (mask & 1) mods << "SHIFT";
    return mods.join(" + ");
}

QVariantList ShortcutsProvider::parseBinds(const QByteArray &json) {
    QVariantList rows;
    for (const auto &value : QJsonDocument::fromJson(json).array()) {
        const auto bind = value.toObject();
        const QString key = bind.value("key").toString();
        if (key.isEmpty() || bind.value("catch_all").toBool())
            continue;
        const QString mods = modsToText(bind.value("modmask").toInt());
        QVariantMap row;
        row["keys"] = mods.isEmpty() ? key.toUpper() : mods + " + " + key.toUpper();
        row["description"] = bind.value("description").toString();
        row["submap"] = bind.value("submap").toString();
        row["mouse"] = bind.value("mouse").toBool();
        rows << row;
    }
    std::stable_sort(rows.begin(), rows.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value("description").toString().localeAwareCompare(b.toMap().value("description").toString()) < 0;
    });
    return rows;
}

QString ShortcutsProvider::canonicalKeys(const QString &keys) {
    QStringList parts;
    for (const auto &part : keys.split('+', Qt::SkipEmptyParts))
        parts << part.trimmed().toUpper();
    if (parts.isEmpty())
        return {};
    const QString key = parts.takeLast();
    QStringList ordered;
    for (const char *mod : {"SUPER", "CTRL", "ALT", "SHIFT"})
        if (parts.contains(QLatin1String(mod)))
            ordered << QLatin1String(mod);
    ordered << key;
    return ordered.join(" + ");
}

bool ShortcutsProvider::isValidKeys(const QString &keys) {
    // A chord needs a modifier, or an ordinary letter would stop typing anywhere.
    static const QRegularExpression valid(R"(^((SUPER|CTRL|ALT|SHIFT) \+ )+[A-Z0-9_]{1,24}$)");
    return valid.match(keys).hasMatch();
}

QVariantList ShortcutsProvider::ownedBinds() const {
    HyprModule module(m_runner->env());
    QVariantList rows;
    if (!module.load())
        return rows;
    for (const auto &value : module.binds())
        rows << value.toObject().toVariantMap();
    return rows;
}

void ShortcutsProvider::refresh() {
    m_runner->run(m_runner->env().hyprctl({"-j", "binds"}), CommandOptions{4000}, this, [this](const CommandResult &result) {
        m_status = result.ok() ? "ok" : "unavailable";
        m_binds = parseBinds(result.out);
        emit changed();
    });
}

QVariantMap ShortcutsProvider::checkKeys(const QString &keys) const {
    QVariantMap result;
    const QString canonical = canonicalKeys(keys);
    result["keys"] = canonical;
    result["valid"] = isValidKeys(canonical);
    QStringList conflicts;
    for (const auto &bind : m_binds) {
        const auto map = bind.toMap();
        if (map.value("keys").toString() == canonical)
            conflicts << map.value("description").toString();
    }
    result["conflicts"] = conflicts;
    // Without these there is no keyboard path back to any window or menu.
    static const QStringList critical{"SUPER + SPACE", "SUPER + RETURN", "SUPER + ALT + SPACE"};
    result["critical"] = critical.contains(canonical);
    return result;
}

void ShortcutsProvider::submit(const QString &label, const QString &after, std::function<bool(HyprModule &, QString *)> edit,
                               std::function<bool(const QVariantList &)> verified, const QString &successText) {
    HyprModule probe(m_runner->env());
    ChangeCoordinator::Request request;
    request.settingId = "shortcuts";
    request.label = label;
    request.after = after;
    request.files << probe.path() << probe.hyprlandPath();
    request.apply = [this, edit](ChangeCoordinator::Done done) {
        HyprModule module(m_runner->env());
        QString error;
        if (!module.load(&error) || !edit(module, &error) || !module.save(&error) || !module.installImport(&error)) {
            done(false, error);
            return;
        }
        CommandOptions options;
        options.mutation = true;
        m_runner->run(m_runner->env().hyprctl({"reload"}), options, this, [done](const CommandResult &result) { done(result.ok(), result.describe("Hyprland")); });
    };
    request.verify = [this, verified](ChangeCoordinator::Done done) {
        m_runner->run(m_runner->env().hyprctl({"-j", "binds"}), CommandOptions{4000}, this, [this, verified, done](const CommandResult &result) {
            done(result.ok() && verified(parseBinds(result.out)), tr("Hyprland did not pick up the shortcut."));
        });
    };
    request.rollback = [this](ChangeCoordinator::Done done) {
        CommandOptions options;
        options.mutation = true;
        m_runner->run(m_runner->env().hyprctl({"reload"}), options, this, [done](const CommandResult &) { done(true, {}); });
    };
    m_coordinator->submit(std::move(request), [this, successText](const Transaction &tx) {
        emit message(tx.phase == QLatin1String("committed") ? successText : tx.message, tx.phase != QLatin1String("committed"));
        refresh();
    });
}

void ShortcutsProvider::addBinding(const QString &keys, const QString &description, const QString &command, bool replaceExisting) {
    const auto check = checkKeys(keys);
    const QString canonical = check.value("keys").toString();
    if (!check.value("valid").toBool()) {
        emit message(tr("A shortcut needs at least one modifier and one key."), true);
        return;
    }
    if (command.trimmed().isEmpty()) {
        emit message(tr("Enter the command this shortcut should run."), true);
        return;
    }
    if (!check.value("conflicts").toStringList().isEmpty() && !replaceExisting) {
        emit message(tr("%1 is already in use. Choose to replace it, or pick other keys.").arg(canonical), true);
        return;
    }
    const QJsonObject bind{{"keys", canonical}, {"description", description.trimmed()}, {"command", command.trimmed()}, {"replaces", replaceExisting}};
    submit(tr("Shortcut %1").arg(canonical), canonical,
           [bind, canonical](HyprModule &module, QString *) {
               QJsonArray binds;
               for (const auto &value : module.binds())
                   if (value.toObject().value("keys").toString() != canonical)
                       binds.append(value);
               binds.append(bind);
               module.setBinds(binds);
               return true;
           },
           [canonical](const QVariantList &rows) {
               for (const auto &row : rows)
                   if (row.toMap().value("keys").toString() == canonical)
                       return true;
               return false;
           },
           tr("Shortcut saved"));
}

void ShortcutsProvider::removeBinding(const QString &keys) {
    const QString canonical = canonicalKeys(keys);
    submit(tr("Shortcut %1").arg(canonical), "removed",
           [canonical](HyprModule &module, QString *) {
               QJsonArray binds;
               for (const auto &value : module.binds())
                   if (value.toObject().value("keys").toString() != canonical)
                       binds.append(value);
               module.setBinds(binds);
               return true;
           },
           [](const QVariantList &) { return true; }, tr("Shortcut removed"));
}
