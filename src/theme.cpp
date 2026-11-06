// Theme watching follows Omacut (MIT, David Heinemeier Hansson).
#include "theme.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <cmath>

#include "fileops.h"

Theme::Theme(const Env &env, QObject *parent) : QObject(parent), m_env(env) {
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(80);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_debounce.start(); });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { m_debounce.start(); });
    connect(&m_debounce, &QTimer::timeout, this, &Theme::reload);

    m_rounding.setProcessEnvironment(env.process);
    m_roundingTimeout.setSingleShot(true);
    m_roundingTimeout.setInterval(800);
    connect(&m_roundingTimeout, &QTimer::timeout, &m_rounding, &QProcess::kill);
    connect(&m_rounding, &QProcess::errorOccurred, this, [this] { m_roundingTimeout.stop(); });
    connect(&m_rounding, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        m_roundingTimeout.stop();
        const auto value = QJsonDocument::fromJson(m_rounding.readAllStandardOutput()).object().value("int");
        // Without Hyprland the corners stay sharp, like Omarchy's default.
        if (code != 0 || status != QProcess::NormalExit || !value.isDouble() || value.toInt(-1) < 0)
            return;
        if (value.toInt() != m_radius) {
            m_radius = value.toInt();
            emit changed();
        }
    });
    reload();
}

Theme::~Theme() {
    if (m_rounding.state() != QProcess::NotRunning) {
        m_rounding.kill();
        m_rounding.waitForFinished(1000);
    }
}

void Theme::refreshRounding() {
    if (m_rounding.state() != QProcess::NotRunning)
        return;
    const QStringList argv = m_env.hyprctl({"-j", "getoption", "decoration:rounding"});
    const QString program = m_env.findExecutable(argv.first());
    if (program.isEmpty())
        return;
    m_rounding.start(program, argv.mid(1));
    m_roundingTimeout.start();
}

QHash<QString, QString> Theme::readColors(const QString &path) {
    QHash<QString, QString> colors;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return colors;
    static const QRegularExpression expression(R"re(^\s*([A-Za-z0-9_]+)\s*=\s*["']([^"']+)["'])re",
                                                QRegularExpression::MultilineOption);
    auto matches = expression.globalMatch(QString::fromUtf8(file.readAll()));
    while (matches.hasNext()) {
        const auto match = matches.next();
        colors.insert(match.captured(1), match.captured(2));
    }
    return colors;
}

// Black or white, whichever reads on the accent (WCAG relative luminance).
QString Theme::accentForeground() const {
    const QColor c(accent());
    auto linear = [](double v) { return v <= .04045 ? v / 12.92 : std::pow((v + .055) / 1.055, 2.4); };
    const double luminance = .2126 * linear(c.redF()) + .7152 * linear(c.greenF()) + .0722 * linear(c.blueF());
    return luminance > .179 ? "black" : "white";
}

void Theme::reload() {
    refreshRounding();
    const auto paths = m_watcher.files() + m_watcher.directories();
    if (!paths.isEmpty())
        m_watcher.removePaths(paths);

    // A theme switch replaces symlinks and files, so watch the parents too and
    // re-arm after every change.
    const QString current = m_env.stateHome + "/omarchy/current";
    const QString toggles = m_env.stateHome + "/omarchy/toggles/hypr";
    const QString userToml = m_env.configHome + "/omarchy/shell.toml";
    QString ancestor = current;
    while (!QFileInfo::exists(ancestor) && ancestor != "/" && ancestor != ".")
        ancestor = QFileInfo(ancestor).absolutePath();
    QStringList candidates{ancestor,  QFileInfo(ancestor).absolutePath(), current, current + "/theme",
                           current + "/theme/colors.toml", current + "/theme.name", userToml,
                           QFileInfo(userToml).absolutePath(), toggles};
    candidates.removeDuplicates();
    for (const auto &path : candidates)
        if (QFileInfo::exists(path))
            m_watcher.addPath(path);

    auto colors = readColors(current + "/theme/colors.toml");
    const QString mode = colors.value("mode", "dark");
    const bool dark = mode != "light";

    QByteArray themeToml, overrideToml;
    FileOps::readFile(current + "/theme/shell.toml", &themeToml);
    FileOps::readFile(userToml, &overrideToml);
    QString base = FileOps::tomlGet(overrideToml, "font", "base-size");
    if (base.isEmpty())
        base = FileOps::tomlGet(themeToml, "font", "base-size");
    bool ok = false;
    const int fontSize = qBound(9, base.toInt(&ok), 24);

    const bool animate = !QFileInfo::exists(toggles + "/no-animations.lua");
    QByteArray nameBytes;
    FileOps::readFile(current + "/theme.name", &nameBytes);
    const QString name = QString::fromUtf8(nameBytes).trimmed();

    const bool different = colors != m_colors || dark != m_dark || (ok ? fontSize : 13) != m_fontSize ||
                           animate != m_animate || name != m_name;
    m_colors = colors;
    m_dark = dark;
    m_fontSize = ok ? fontSize : 13;
    m_animate = animate;
    m_name = name;
    if (different)
        emit changed();
}
