// Theme watching follows Omacut (MIT, David Heinemeier Hansson).
#include "theme.h"

#include <QUrl>

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
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

    // Inter for the interface; numbers and values stay in the desktop's own monospace.
    const int font = QFontDatabase::addApplicationFont(":/fonts/InterVariable.ttf");
    const QStringList families = font >= 0 ? QFontDatabase::applicationFontFamilies(font) : QStringList();
    m_uiFont = families.isEmpty() ? QStringLiteral("sans-serif") : families.first();

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

static double relativeLuminance(const QColor &c) {
    const auto linear = [](double v) { return v <= .04045 ? v / 12.92 : std::pow((v + .055) / 1.055, 2.4); };
    return .2126 * linear(c.redF()) + .7152 * linear(c.greenF()) + .0722 * linear(c.blueF());
}

// Black or white, whichever reads on the accent (WCAG relative luminance).
QString Theme::accentForeground() const {
    return relativeLuminance(QColor(accent())) > .179 ? "black" : "white";
}

QString Theme::mix(const QString &from, const QString &to, double amount) {
    const QColor a(from), b(to);
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * amount,
                            a.greenF() + (b.greenF() - a.greenF()) * amount,
                            a.blueF() + (b.blueF() - a.blueF()) * amount).name();
}

QString Theme::withAlpha(const QString &color, double alpha) {
    QColor c(color);
    c.setAlphaF(alpha);
    return c.name(QColor::HexArgb);
}

double Theme::contrast(const QString &a, const QString &b) {
    const double first = relativeLuminance(QColor(a)), second = relativeLuminance(QColor(b));
    return (qMax(first, second) + .05) / (qMin(first, second) + .05);
}

QString Theme::readable(const QString &color, const QString &foreground, const QStringList &surfaces, double ratio) {
    const QColor from(color), to(foreground);
    if (!from.isValid() || !to.isValid())
        return color;
    const auto enoughOn = [&](const QString &candidate) {
        for (const auto &surface : surfaces)
            if (contrast(candidate, surface) < ratio)
                return false;
        return true;
    };
    if (enoughOn(color))
        return color;
    // Lighten or darken first, so a warning stays amber instead of drifting to gray.
    float hue, saturation, lightness;
    from.getHslF(&hue, &saturation, &lightness);
    const float target = to.lightnessF() < lightness ? 0.0f : 1.0f;
    for (int step = 1; step <= 20; ++step) {
        const QString shifted = QColor::fromHslF(hue, saturation, lightness + (target - lightness) * step / 20.0f).name();
        if (enoughOn(shifted))
            return shifted;
    }
    // A palette with no room left for that color: fall back toward its own text color.
    QString candidate = color;
    for (int step = 0; step <= 20; ++step) {
        const double t = step / 20.0;
        candidate = QColor::fromRgbF(from.redF() + (to.redF() - from.redF()) * t,
                                     from.greenF() + (to.greenF() - from.greenF()) * t,
                                     from.blueF() + (to.blueF() - from.blueF()) * t).name();
        if (enoughOn(candidate))
            break;
    }
    return candidate;
}

QString Theme::thumbnail(const QString &picture, int radius) const {
    if (picture.isEmpty())
        return {};
    const QString path = picture.startsWith(QLatin1String("file:")) ? QUrl(picture).toLocalFile() : picture;
    return "image://icons/thumb/" + QString::number(radius) + "/" + QString::fromUtf8(QUrl::toPercentEncoding(path));
}

QVariantMap Theme::preview(const QString &themeName) const {
    QString slug = themeName.trimmed().toLower();
    slug.replace(' ', '-');
    QVariantMap result;
    if (slug.isEmpty() || slug.contains('/') || slug.startsWith('.'))
        return result;
    // A theme in the user's folder wins over one of the same name that Omarchy ships.
    for (const QString &directory : {m_env.configHome + "/omarchy/themes/" + slug, m_env.omarchyPath + "/themes/" + slug}) {
        const auto colors = readColors(directory + "/colors.toml");
        if (colors.isEmpty())
            continue;
        result["background"] = colors.value("background", "#1c1c1e");
        result["surface"] = colors.value("dark_background", colors.value("background", "#1c1c1e"));
        result["foreground"] = colors.value("foreground", "#f5f5f7");
        result["accent"] = colors.value("accent", "#FFD60A");
        result["line"] = colors.value("selection", colors.value("foreground", "#f5f5f7"));
        if (QFileInfo::exists(directory + "/preview.png")) {
            result["image"] = QUrl::fromLocalFile(directory + "/preview.png").toString();
        } else {
            // A theme with no preview still has its wallpapers, which say as much.
            const auto stills = QDir(directory + "/backgrounds").entryList({"*.png", "*.jpg", "*.jpeg", "*.webp"}, QDir::Files | QDir::Readable, QDir::Name);
            if (!stills.isEmpty())
                result["image"] = QUrl::fromLocalFile(directory + "/backgrounds/" + stills.first()).toString();
        }
        break;
    }
    return result;
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

    const QString backgroundFile = QFileInfo(current + "/background").canonicalFilePath();
    static const QRegularExpression still("\\.(png|jpe?g|webp)$", QRegularExpression::CaseInsensitiveOption);
    const QString wallpaper = still.match(backgroundFile).hasMatch() ? QUrl::fromLocalFile(backgroundFile).toString() : QString();

    const bool different = colors != m_colors || dark != m_dark || (ok ? fontSize : 13) != m_fontSize ||
                           animate != m_animate || name != m_name || wallpaper != m_wallpaper;
    m_wallpaper = wallpaper;
    m_colors = colors;
    m_dark = dark;
    m_fontSize = ok ? fontSize : 13;
    m_animate = animate;
    m_name = name;
    if (different)
        emit changed();
}
