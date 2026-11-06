#pragma once

#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QVariantMap>

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
    Q_PROPERTY(QString accentText READ accentText NOTIFY changed)
    Q_PROPERTY(QString dangerText READ dangerText NOTIFY changed)
    Q_PROPERTY(QString warningText READ warningText NOTIFY changed)
    Q_PROPERTY(int rowHeight READ rowHeight NOTIFY changed)
    Q_PROPERTY(QString canvas READ canvas NOTIFY changed)
    Q_PROPERTY(QString rail READ rail NOTIFY changed)
    Q_PROPERTY(QString surfaceTop READ surfaceTop NOTIFY changed)
    Q_PROPERTY(QString surfaceBottom READ surfaceBottom NOTIFY changed)
    Q_PROPERTY(QString hairline READ hairline NOTIFY changed)
    Q_PROPERTY(QString controlBorder READ controlBorder NOTIFY changed)
    Q_PROPERTY(QString goodText READ goodText NOTIFY changed)
    Q_PROPERTY(int panelRadius READ panelRadius NOTIFY changed)
    Q_PROPERTY(int controlRadius READ controlRadius NOTIFY changed)
    Q_PROPERTY(bool rounded READ rounded NOTIFY changed)
    Q_PROPERTY(QString uiFont READ uiFont CONSTANT)
    Q_PROPERTY(QString wallpaper READ wallpaper NOTIFY changed)
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
    QString muted() const { return onSurface(color("dark_foreground", "#98989d")); }
    QString divider() const { return color("selection", "#3a3a3c"); }
    QString danger() const { return color("red", "#ff5f4a"); }
    QString good() const { return color("green", "#5fc97f"); }
    QString warning() const { return color("yellow", "#ffc27a"); }
    // Themes are not all written with text in mind, so anything drawn as text goes
    // through these: the theme's own color, moved toward the foreground only as far as
    // it takes to stay readable on this window and its panels.
    QString accentText() const { return onSurface(accent()); }
    QString dangerText() const { return onSurface(danger()); }
    QString warningText() const { return onSurface(warning()); }
    QString goodText() const { return onSurface(good()); }
    // The surfaces come from the theme and take a trace of its accent, so a warm theme
    // reads warm and a cold one reads cold, with nothing hard-coded to either.
    QString canvas() const { return mix(m_dark ? mix(window(), deep(), 0.55) : window(), accent(), 0.02); }
    QString rail() const { return mix(m_dark ? deep() : sidebar(), accent(), 0.015); }
    QString surfaceTop() const { return mix(canvas(), m_dark ? foreground() : QStringLiteral("#ffffff"), m_dark ? 0.055 : 0.55); }
    QString surfaceBottom() const { return mix(canvas(), m_dark ? foreground() : QStringLiteral("#ffffff"), m_dark ? 0.025 : 0.4); }
    QString hairline() const { return withAlpha(foreground(), m_dark ? 0.12 : 0.16); }
    QString controlBorder() const { return withAlpha(foreground(), m_dark ? 0.24 : 0.32); }
    // The desktop decides: square stays square everywhere, and rounding scales from
    // whatever Hyprland uses for its windows.
    bool rounded() const { return m_radius > 0; }
    int panelRadius() const { return qMin(m_radius, 14); }
    int controlRadius() const { return m_radius > 0 ? qBound(4, m_radius * 2 / 3, 8) : 0; }
    QString uiFont() const { return m_uiFont; }
    // The picture actually on the desktop, when it is a still image.
    QString wallpaper() const { return m_wallpaper; }
    // The height every ordinary row shares, whatever is in it.
    int rowHeight() const { return qRound(m_fontSize * 3.65); }
    bool dark() const { return m_dark; }
    int radius() const { return m_radius; }
    int fontSize() const { return m_fontSize; }
    bool animate() const { return m_animate; }
    QString themeName() const { return m_name; }

    // Corners change with Hyprland's config rather than a file this app can
    // watch, so the window asks again whenever it regains focus.
    Q_INVOKABLE void refreshRounding();
    // What a theme looks like before choosing it: its preview image, if it ships one,
    // and its colors.
    Q_INVOKABLE QVariantMap preview(const QString &themeName) const;
    // The address of a picture cut to size with rounded corners. Takes a path or file URL.
    Q_INVOKABLE QString thumbnail(const QString &picture, int radius) const;

    static QHash<QString, QString> readColors(const QString &path);
    static double contrast(const QString &a, const QString &b);
    static QString mix(const QString &from, const QString &to, double amount);
    static QString withAlpha(const QString &color, double alpha);
    static QString readable(const QString &color, const QString &foreground, const QStringList &surfaces, double ratio = 4.5);

signals:
    void changed();

private:
    QString color(const QString &key, const QString &fallback) const { return m_colors.value(key, fallback); }
    QString onSurface(const QString &value) const { return readable(value, foreground(), {canvas(), surfaceTop()}); }
    void reload();

    Env m_env;
    QHash<QString, QString> m_colors;
    bool m_dark = true;
    int m_radius = 0;
    int m_fontSize = 13;
    bool m_animate = true;
    QString m_name;
    QString m_uiFont;
    QString m_wallpaper;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QProcess m_rounding;
    QTimer m_roundingTimeout;
};
