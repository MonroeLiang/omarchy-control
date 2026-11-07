#include "pluginsprovider.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QUrl>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTimer>
#include <algorithm>

#include "fileops.h"
#include "iconthemes.h"

PluginsProvider::PluginsProvider(CommandRunner *runner, ChangeCoordinator *coordinator, QObject *parent)
    : QObject(parent), m_runner(runner), m_coordinator(coordinator) {}

bool PluginsProvider::isSafeId(const QString &id) {
    static const QRegularExpression safe("^[A-Za-z0-9][A-Za-z0-9._-]*$");
    return safe.match(id).hasMatch();
}

QVariantList PluginsProvider::parsePlugins(const QByteArray &json) {
    QVariantList rows;
    for (const auto &value : QJsonDocument::fromJson(json).array())
        rows << value.toObject().toVariantMap();
    std::stable_sort(rows.begin(), rows.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value("name").toString().localeAwareCompare(b.toMap().value("name").toString()) < 0;
    });
    return rows;
}

QVariantList PluginsProvider::scanManifests(const Env &env) {
    // Used when the shell is not answering. Manifests say what exists; only the
    // running shell can say what is active, so `enabled` is left unknown.
    QVariantList rows;
    for (const auto &root : {env.omarchyPath + "/shell/plugins", env.configHome + "/omarchy/plugins"}) {
        QDirIterator it(root, {"manifest.json", "*.manifest.json"}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            QByteArray contents;
            FileOps::readFile(it.next(), &contents);
            const auto manifest = QJsonDocument::fromJson(contents).object();
            if (manifest.value("id").toString().isEmpty())
                continue;
            QVariantMap row;
            row["id"] = manifest.value("id").toString();
            row["name"] = manifest.value("name").toString();
            row["kinds"] = manifest.value("kinds").toArray().toVariantList();
            row["firstParty"] = root.startsWith(env.omarchyPath);
            row["canDisable"] = false;
            row["unknownState"] = true;
            rows << row;
        }
    }
    return rows;
}

QVariantMap PluginsProvider::parseLayout(const QByteArray &shellJson) {
    const auto layout = QJsonDocument::fromJson(FileOps::stripJsonComments(shellJson)).object().value("bar").toObject().value("layout").toObject();
    QVariantMap sections;
    for (const char *section : {"left", "centerBegin", "center", "centerEnd", "right", "tray"}) {
        QVariantList widgets;
        for (const auto &value : layout.value(QLatin1String(section)).toArray())
            // Omarchy accepts a bare id as well as an object with options.
            widgets << (value.isString() ? value.toString() : value.toObject().value("id").toString());
        sections.insert(QLatin1String(section), widgets);
    }
    return sections;
}

QVariantMap PluginsProvider::resolveDockApp(const Env &env, const QString &desktopId, const QString &iconTheme) {
    QVariantMap app{{"id", desktopId}, {"name", QString()}, {"icon", QString()}, {"installed", false}};
    if (!IconThemes::isValidId(desktopId))
        return app;
    QByteArray entry;
    QStringList roots{env.dataHome};
    roots += env.dataDirs;
    // The first one found is the one the desktop uses, so a user's copy hides the system's.
    for (const QString &root : std::as_const(roots)) {
        if (FileOps::readFile(root + "/applications/" + desktopId + ".desktop", &entry)) {
            app["installed"] = true;
            break;
        }
    }
    if (!app.value("installed").toBool())
        return app;
    const QString group = QStringLiteral("Desktop Entry");
    const QString locale = QLocale::system().name();
    for (const QString &key : {"Name[" + locale + "]", "Name[" + locale.section('_', 0, 0) + "]", QStringLiteral("Name")}) {
        const FileOps::IniValue name = FileOps::iniGet(entry, group, key);
        if (name.found && !name.value.isEmpty()) {
            app["name"] = name.value;
            break;
        }
    }
    app["icon"] = IconThemes::appIconUrl(env, iconTheme, {FileOps::iniGet(entry, group, QStringLiteral("Icon")).value}, 96);
    return app;
}

void PluginsProvider::resolveDockApps() {
    const Env &env = m_runner->env();
    const QString theme = m_iconTheme.isEmpty() ? IconThemes::desktopDefault(env) : m_iconTheme;
    m_dockApps.clear();
    for (const QString &pin : std::as_const(m_pins))
        m_dockApps << resolveDockApp(env, pin, theme);
}

void PluginsProvider::loadConfig() {
    QByteArray contents;
    if (!FileOps::readFile(m_runner->env().configHome + "/omarchy/shell.json", &contents) || contents.trimmed().isEmpty())
        FileOps::readFile(m_runner->env().omarchyPath + "/config/omarchy/shell.json", &contents);
    m_layout = parseLayout(contents);
    m_activeBar = QJsonDocument::fromJson(FileOps::stripJsonComments(contents)).object().value("bar").toObject().value("id").toString();
    m_pins.clear();
    QJsonValue pins = FileOps::jsonGet(contents, {"dock", "pinned"});
    // A config without pins of its own uses the default ones, as omarchy-dock does.
    if (!pins.isArray()) {
        QByteArray defaults;
        FileOps::readFile(m_runner->env().omarchyPath + "/config/omarchy/shell.json", &defaults);
        pins = FileOps::jsonGet(defaults, {"dock", "pinned"});
    }
    for (const auto &value : pins.toArray())
        m_pins << value.toString();
    resolveDockApps();
}

void PluginsProvider::refresh() {
    loadConfig();
    emit changed();
    // Pins are drawn from the pack in use. Without an answer the theme's own is close enough.
    m_runner->run({"gsettings", "get", "org.gnome.desktop.interface", "icon-theme"}, CommandOptions{4000}, this, [this](const CommandResult &result) {
        bool ok = false;
        const QString theme = result.ok() ? IconThemes::parseGVariantString(result.out, &ok) : QString();
        if (ok && theme != m_iconTheme) {
            m_iconTheme = theme;
            resolveDockApps();
            emit changed();
        }
    });
    m_runner->run({"omarchy", "plugin", "list", "--json"}, CommandOptions{4000}, this, [this](const CommandResult &result) {
        if (result.ok()) {
            m_plugins = parsePlugins(result.out);
            m_status = "ok";
        } else {
            m_plugins = scanManifests(m_runner->env());
            m_status = "shell-down";
        }
        emit changed();
    });
}

void PluginsProvider::runChange(const QString &label, const QString &after, const QStringList &argv, std::function<void(ChangeCoordinator::Done)> verify, const QString &success) {
    const QString config = m_runner->env().configHome + "/omarchy/shell.json";
    ChangeCoordinator::Request request;
    request.settingId = "plugins";
    request.label = label;
    request.after = after;
    request.files << config;
    request.apply = [this, argv](ChangeCoordinator::Done done) {
        CommandOptions options;
        options.mutation = true;
        options.timeoutMs = 20000;
        m_runner->run(argv, options, this, [done, argv](const CommandResult &result) { done(result.ok(), result.describe(argv.value(0))); });
    };
    request.verify = verify;
    ++m_changes;
    emit changed();
    m_coordinator->submit(std::move(request), [this, success](const Transaction &tx) {
        --m_changes;
        emit message(tx.phase == QLatin1String("committed") ? success : tx.message, tx.phase != QLatin1String("committed"));
        refresh();
    });
}

void PluginsProvider::setEnabled(const QString &id, bool enabled) {
    if (!isSafeId(id))
        return;
    runChange(id, enabled ? "enabled" : "disabled", {"omarchy", "plugin", enabled ? "enable" : "disable", id},
              [this, id, enabled](ChangeCoordinator::Done done) {
                  m_runner->run({"omarchy", "plugin", "list", "--json"}, CommandOptions{4000}, this, [this, id, enabled, done](const CommandResult &result) {
                      for (const auto &row : parsePlugins(result.out))
                          if (row.toMap().value("id").toString() == id) {
                              done(row.toMap().value("enabled").toBool() == enabled, tr("The plugin did not change state."));
                              return;
                          }
                      done(false, tr("The plugin is no longer available."));
                  });
              },
              enabled ? tr("%1 enabled").arg(id) : tr("%1 disabled").arg(id));
}

void PluginsProvider::clonePlugin(const QString &id) {
    if (!isSafeId(id))
        return;
    runChange(id, "cloned", {"omarchy", "plugin", "clone", id}, nullptr, tr("Cloned %1 into your config").arg(id));
}

void PluginsProvider::removePlugin(const QString &id) {
    if (!isSafeId(id))
        return;
    runChange(id, "removed", {"omarchy", "plugin", "remove", id}, nullptr, tr("Removed %1").arg(id));
}

void PluginsProvider::moveWidget(const QString &id, const QString &section, int index, const QString &fromSection, int fromIndex) {
    static const QStringList sections{"left", "centerBegin", "center", "centerEnd", "right", "tray"};
    if (!isSafeId(id) || !sections.contains(section) || !sections.contains(fromSection) || index < 0 || fromIndex < 0)
        return;
    const QStringList argv{"omarchy", "bar", "move", id, "--section", section, "--index", QString::number(index),
                           "--from-section", fromSection, "--from-index", QString::number(fromIndex)};
    runChange(tr("Bar widget %1").arg(id), section, argv,
              [this, id, section, index](ChangeCoordinator::Done done) { verifyMoved(id, section, index, 8, done); },
              tr("Moved %1").arg(id));
}

void PluginsProvider::verifyMoved(const QString &id, const QString &section, int index, int attempts, ChangeCoordinator::Done done) {
    QByteArray contents;
    FileOps::readFile(m_runner->env().configHome + "/omarchy/shell.json", &contents);
    // The exact place, not just the section, or a reorder could never fail this check.
    if (parseLayout(contents).value(section).toList().value(index).toString() == id) {
        done(true, {});
        return;
    }
    if (attempts <= 1) {
        done(false, tr("The widget did not move."));
        return;
    }
    // A failed check restores the file, so give the write a moment to land first.
    QTimer::singleShot(150, this, [this, id, section, index, attempts, done] { verifyMoved(id, section, index, attempts - 1, done); });
}

void PluginsProvider::pin(const QString &desktopId) {
    if (!isSafeId(desktopId))
        return;
    runChange(tr("Dock"), desktopId, {"omarchy", "dock", "pin", desktopId}, nullptr, tr("Pinned to the dock"));
}

void PluginsProvider::unpin(const QString &desktopId) {
    if (!isSafeId(desktopId))
        return;
    runChange(tr("Dock"), desktopId, {"omarchy", "dock", "unpin", desktopId}, nullptr, tr("Removed from the dock"));
}
