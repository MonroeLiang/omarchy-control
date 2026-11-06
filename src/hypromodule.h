#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "env.h"

// The one Lua file this application owns: ~/.config/hypr/omarchy-control.lua.
// It holds only values someone explicitly chose here, loads after the user's
// override files and before Omarchy's toggles, and is regenerated whole from the
// data on its first line. An edit by anything else is detected rather than
// overwritten, and the app never executes the user's Lua to learn what it does.
class HyprModule {
public:
    explicit HyprModule(const Env &env);

    QString path() const;
    QString hyprlandPath() const;

    enum State { Absent, Ours, ModifiedByUser };
    State state() const;

    QJsonObject options() const { return m_data.value("options").toObject(); }
    QJsonArray monitors() const { return m_data.value("monitors").toArray(); }
    QJsonArray binds() const { return m_data.value("binds").toArray(); }
    QJsonArray rules() const { return m_data.value("rules").toArray(); }

    bool load(QString *error = nullptr);
    void setOption(const QStringList &path, const QJsonValue &value);
    void removeOption(const QStringList &path);
    bool hasOption(const QStringList &path) const;
    QJsonValue option(const QStringList &path) const;
    void setMonitors(const QJsonArray &monitors) { m_data.insert("monitors", monitors); }
    void setBinds(const QJsonArray &binds) { m_data.insert("binds", binds); }
    void setRules(const QJsonArray &rules) { m_data.insert("rules", rules); }

    // Rendered text for the current data; pure, and tested without a compositor.
    QByteArray render() const;
    bool save(QString *error = nullptr);

    // Whether the marked import line is in place, and adding or removing it. The
    // anchor is Omarchy's stock require("hypr.autostart") line; a config without
    // one stays untouched and the setting offers advanced access instead.
    bool importInstalled() const;
    bool canInstallImport() const;
    bool installImport(QString *error = nullptr);
    bool removeImport(QString *error = nullptr);

    static QString luaString(const QString &text);
    static bool isSafeKey(const QString &key);

private:
    Env m_env;
    QJsonObject m_data;
};
