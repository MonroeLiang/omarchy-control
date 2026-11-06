#pragma once

#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QProcess>
#include <QTimer>

#include "env.h"

// Follows the active Omarchy theme live: the whole palette rather than only the
// accent, light or dark mode, text size, corners, and whether motion is off. It
// reads the theme's files directly, so the app keeps its look while the shell
// is restarting or not running.
class Theme : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString accent READ accent NOTIFY changed)
    Q_PROPERTY(QString accentForeground READ accentForeground NOTIFY changed)
    Q_PROPERTY(QString window READ window NOTIFY changed)
    Q_PROPERTY(QString sidebar READ sidebar NOTIFY changed)
    Q_PROPERTY(QString deep READ deep NOTIFY changed)
    Q_PROPERTY(QString raised READ raised NOTIFY changed)
    Q_PROPERTY(QString foreground READ foreground NOTIFY changed)
    Q_PROPERTY(QString muted READ muted NOTIFY changed)
    Q_PROPERTY(QString divider READ divider NOTIFY changed)
    Q_PROPERTY(QString danger READ danger NOTIFY changed)
    Q_PROPERTY(QString good READ good NOTIFY changed)
    Q_PROPERTY(QString warning READ warning NOTIFY changed)
    Q_PROPERTY(bool dark READ dark NOTIFY changed)
    Q_PROPERTY(int radius READ radius NOTIFY changed)
    Q_PROPERTY(int fontSize READ fontSize NOTIFY changed)
    Q_PROPERTY(bool animate READ animate NOTIFY changed)
    Q_PROPERTY(QString themeName READ themeName NOTIFY changed)

public:
    explicit Theme(const Env &env, QObject *parent = nullptr);
    ~Theme() override;

    QString accent() const { return color("accent", "#FFD60A"); }
    QString accentForeground() const;
    QString window() const { return color("background", "#1c1c1e"); }
    QString sidebar() const { return color("dark_background", "#161618"); }
    QString deep() const { return color("darker_background", "#0e0e10"); }
    QString raised() const { return color("lighter_background", "#2c2c2e"); }
    QString foreground() const { return color("foreground", "#f5f5f7"); }
    QString muted() const { return color("dark_foreground", "#98989d"); }
    QString divider() const { return color("selection", "#3a3a3c"); }
    QString danger() const { return color("red", "#ff5f4a"); }
    QString good() const { return color("green", "#5fc97f"); }
    QString warning() const { return color("yellow", "#ffc27a"); }
    bool dark() const { return m_dark; }
    int radius() const { return m_radius; }
    int fontSize() const { return m_fontSize; }
    bool animate() const { return m_animate; }
    QString themeName() const { return m_name; }

    // Corners change with Hyprland's config rather than a file this app can
    // watch, so the window asks again whenever it regains focus.
    Q_INVOKABLE void refreshRounding();

    static QHash<QString, QString> readColors(const QString &path);

signals:
    void changed();

private:
    QString color(const QString &key, const QString &fallback) const { return m_colors.value(key, fallback); }
    void reload();

    Env m_env;
    QHash<QString, QString> m_colors;
    bool m_dark = true;
    int m_radius = 0;
    int m_fontSize = 13;
    bool m_animate = true;
    QString m_name;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QProcess m_rounding;
    QTimer m_roundingTimeout;
};
