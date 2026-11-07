#include "iconthemes.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QPainter>
#include <QImageReader>
#include <QSet>
#include <QSvgRenderer>
#include <QUrl>
#include <algorithm>

#include "fileops.h"

namespace {

constexpr qint64 MaxIndexBytes = 1024 * 1024;
constexpr qint64 MaxSvgBytes = 2 * 1024 * 1024;
constexpr int MaxRasterSide = 1024;
constexpr int MaxInheritance = 12;
constexpr int MaxDirectories = 5000;

int intValue(const QHash<QByteArray, QByteArray> &group, const char *key, int fallback) {
    bool ok = false;
    const int value = group.value(key).toInt(&ok);
    return ok ? value : fallback;
}

QStringList splitList(const QByteArray &value) {
    QStringList items;
    for (const QByteArray &part : value.split(',')) {
        const QString item = QString::fromUtf8(part).trimmed();
        if (!item.isEmpty() && !items.contains(item))
            items << item;
    }
    return items;
}

QString localized(const QHash<QByteArray, QByteArray> &group, const char *key, const QString &language) {
    const QString locale = language.isEmpty() ? QLocale::system().name() : language;
    for (const QString &candidate : {locale, locale.section('_', 0, 0)}) {
        const QByteArray full = QByteArray(key) + "[" + candidate.toUtf8() + "]";
        if (!candidate.isEmpty() && group.contains(full))
            return QString::fromUtf8(group.value(full)).trimmed();
    }
    return QString::fromUtf8(group.value(key)).trimmed();
}

// How far down a pack's folders to look: the nearest size first, so one lookup does not
// stat every folder of a large pack before it reaches a good match.
QList<IconPack::Directory> bySuitability(QList<IconPack::Directory> directories, int size) {
    std::stable_sort(directories.begin(), directories.end(), [size](const auto &a, const auto &b) {
        return a.distance(size) < b.distance(size);
    });
    return directories;
}

QString iconIn(const IconPack &pack, const QStringList &names, int size) {
    const auto directories = bySuitability(pack.directories, size);
    // Pack first and then names, as the spec's fallback lookup does: a pack's own icon
    // says more about it than an exact name it inherited from somewhere else.
    for (const QString &name : names) {
        for (const auto &directory : directories) {
            for (const QString &location : pack.locations) {
                const QString base = location + "/" + directory.subdir + "/" + name;
                for (const char *extension : {".svg", ".png"}) {
                    const QString path = base + QLatin1String(extension);
                    if (QFileInfo::exists(path))
                        return path;
                }
            }
        }
    }
    return {};
}

void collectChain(const Env &env, const QString &id, const QString &language, QStringList *seen, QList<IconPack> *chain, int depth) {
    if (depth > MaxInheritance || seen->contains(id) || !IconThemes::isValidId(id))
        return;
    *seen << id;
    const IconPack pack = IconThemes::load(env, id, language);
    if (!pack.found)
        return;
    chain->append(pack);
    for (const QString &parent : pack.inherits)
        collectChain(env, parent, language, seen, chain, depth + 1);
}

}  // namespace

int IconPack::Directory::distance(int wanted) const {
    if (type == QLatin1String("Scalable")) {
        const int low = minSize > 0 ? minSize : size;
        const int high = maxSize > 0 ? maxSize : size;
        // A vector drawn at any size inside its range is exactly right.
        return wanted < low ? (low - wanted) : wanted > high ? (wanted - high) : 0;
    }
    if (type == QLatin1String("Fixed"))
        return std::abs(size * scale - wanted);
    return std::max(0, std::abs(size * scale - wanted) - threshold);
}

namespace IconThemes {

QStringList searchRoots(const Env &env) {
    QStringList roots{env.dataHome + "/icons", env.home + "/.icons"};
    for (const QString &directory : env.dataDirs)
        roots << directory + "/icons";
    roots.removeDuplicates();
    return roots;
}

bool isValidId(const QString &id) {
    if (id.isEmpty() || id.size() > 255 || id.startsWith('.') || id.contains('/'))
        return false;
    for (const QChar c : id)
        if (c.category() == QChar::Other_Control)
            return false;
    return true;
}

IconPack parseIndex(const QByteArray &contents, const QString &id, const QString &language) {
    IconPack pack;
    pack.id = id;
    QHash<QByteArray, QHash<QByteArray, QByteArray>> groups;
    QByteArray current;
    for (const QByteArray &raw : contents.split('\n')) {
        const QByteArray line = raw.trimmed();
        if (line.isEmpty() || line.startsWith('#'))
            continue;
        if (line.startsWith('[') && line.endsWith(']')) {
            current = line.mid(1, line.size() - 2);
            groups[current];
            continue;
        }
        const qsizetype equals = line.indexOf('=');
        if (equals > 0 && !current.isEmpty())
            groups[current].insert(line.left(equals).trimmed(), line.mid(equals + 1).trimmed());
    }
    if (!groups.contains("Icon Theme")) {
        pack.problem = QStringLiteral("index.theme has no [Icon Theme] section");
        return pack;
    }
    const auto &main = groups["Icon Theme"];
    pack.found = true;
    pack.name = localized(main, "Name", language);
    if (pack.name.isEmpty())
        pack.name = id;
    pack.comment = localized(main, "Comment", language);
    pack.hidden = main.value("Hidden").trimmed().toLower() == "true";
    for (const QString &parent : splitList(main.value("Inherits")))
        pack.inherits << parent;
    QStringList subdirs = splitList(main.value("Directories"));
    for (const QString &scaled : splitList(main.value("ScaledDirectories")))
        if (!subdirs.contains(scaled))
            subdirs << scaled;
    for (const QString &subdir : std::as_const(subdirs)) {
        // A pack that names a folder outside itself is not going to be looked in there.
        if (pack.directories.size() >= MaxDirectories || subdir.startsWith('/') || subdir.split('/').contains(QStringLiteral("..")))
            continue;
        const auto group = groups.constFind(subdir.toUtf8());
        if (group == groups.constEnd() || !group->contains("Size"))
            continue;
        IconPack::Directory directory;
        directory.subdir = subdir;
        directory.size = intValue(*group, "Size", 0);
        directory.scale = std::max(1, intValue(*group, "Scale", 1));
        directory.type = QString::fromUtf8(group->value("Type", "Threshold"));
        directory.minSize = intValue(*group, "MinSize", directory.size);
        directory.maxSize = intValue(*group, "MaxSize", directory.size);
        directory.threshold = intValue(*group, "Threshold", 2);
        pack.directories << directory;
    }
    return pack;
}

IconPack load(const Env &env, const QString &id, const QString &language) {
    IconPack pack;
    pack.id = id;
    if (!isValidId(id)) {
        pack.problem = QStringLiteral("not a usable theme name");
        return pack;
    }
    QStringList locations;
    QString index;
    for (const QString &root : searchRoots(env)) {
        const QString location = root + "/" + id;
        if (!QFileInfo(location).isDir())
            continue;
        locations << location;
        // The first descriptor in lookup order speaks for the whole theme.
        if (index.isEmpty() && QFileInfo::exists(location + "/index.theme"))
            index = location + "/index.theme";
    }
    if (index.isEmpty()) {
        pack.problem = locations.isEmpty() ? QStringLiteral("not installed") : QStringLiteral("no index.theme");
        pack.locations = locations;
        return pack;
    }
    QFile file(index);
    if (file.size() > MaxIndexBytes || !file.open(QIODevice::ReadOnly)) {
        pack.problem = QStringLiteral("index.theme could not be read");
        pack.locations = locations;
        pack.indexPath = index;
        return pack;
    }
    pack = parseIndex(file.readAll(), id, language);
    pack.locations = locations;
    pack.indexPath = index;
    return pack;
}

QList<IconPack> discover(const Env &env, const QString &alwaysInclude, QStringList *diagnostics) {
    QStringList ids;
    for (const QString &root : searchRoots(env)) {
        const QDir directory(root);
        for (const QString &name : directory.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
            if (!ids.contains(name))
                ids << name;
    }
    QList<IconPack> packs;
    for (const QString &id : std::as_const(ids)) {
        if (!isValidId(id))
            continue;
        const IconPack pack = load(env, id);
        // A folder with no descriptor is ordinary here, such as loose app icons.
        if (!pack.found) {
            if (diagnostics && !pack.problem.isEmpty() && !pack.indexPath.isEmpty())
                diagnostics->append(id + ": " + pack.problem);
            continue;
        }
        if (!pack.hasIcons() || (pack.hidden && id != alwaysInclude))
            continue;
        if (diagnostics) {
            for (const QString &parent : pack.inherits)
                if (parent != QLatin1String("hicolor") && !load(env, parent).found)
                    diagnostics->append(id + ": inherits " + parent + ", which is not installed");
        }
        packs << pack;
    }
    std::stable_sort(packs.begin(), packs.end(), [](const IconPack &a, const IconPack &b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    return packs;
}

QString findIcon(const Env &env, const QString &id, const QStringList &names, int size, QString *fromTheme) {
    QStringList seen;
    QList<IconPack> chain;
    collectChain(env, id, {}, &seen, &chain, 0);
    // The spec makes hicolor the last resort for every theme, named or not.
    collectChain(env, QStringLiteral("hicolor"), {}, &seen, &chain, 0);
    for (const IconPack &pack : std::as_const(chain)) {
        const QString path = iconIn(pack, names, size);
        if (!path.isEmpty()) {
            if (fromTheme)
                *fromTheme = pack.id;
            return path;
        }
    }
    return {};
}

QString desktopDefault(const Env &env) {
    QByteArray contents;
    if (FileOps::readFile(env.stateHome + "/omarchy/current/theme/icons.theme", &contents)) {
        const QString id = QString::fromUtf8(contents).trimmed();
        if (isValidId(id))
            return id;
    }
    return QStringLiteral("Yaru-blue");
}

QStringList sampleKeys() {
    return {QStringLiteral("folder"), QStringLiteral("document"), QStringLiteral("terminal"), QStringLiteral("browser")};
}

QStringList sampleNames(const QString &key) {
    if (key == QLatin1String("folder"))
        return {QStringLiteral("folder"), QStringLiteral("inode-directory"), QStringLiteral("folder-documents")};
    if (key == QLatin1String("document"))
        return {QStringLiteral("text-x-generic"), QStringLiteral("x-office-document"), QStringLiteral("application-x-generic"), QStringLiteral("document")};
    if (key == QLatin1String("terminal"))
        return {QStringLiteral("utilities-terminal"), QStringLiteral("org.gnome.Terminal"), QStringLiteral("terminal"), QStringLiteral("system-run")};
    if (key == QLatin1String("browser"))
        return {QStringLiteral("web-browser"), QStringLiteral("internet-web-browser"), QStringLiteral("applications-internet"), QStringLiteral("firefox"), QStringLiteral("preferences-system")};
    return {};
}

QString parseGVariantString(const QByteArray &output, bool *ok) {
    const QString text = QString::fromUtf8(output).trimmed();
    *ok = false;
    if (text.size() < 2)
        return {};
    const QChar quote = text.front();
    if ((quote != '\'' && quote != '"') || text.back() != quote)
        return {};
    QString value;
    for (qsizetype i = 1; i < text.size() - 1; ++i) {
        if (text[i] == '\\' && i + 1 < text.size() - 1)
            ++i;
        value += text[i];
    }
    *ok = true;
    return value;
}

}  // namespace IconThemes

IconPackImageProvider::IconPackImageProvider(const Env &env)
    : QQuickImageProvider(QQuickImageProvider::Image, QQuickImageProvider::ForceAsynchronousImageLoading), m_env(env) {}

QImage IconPackImageProvider::render(const QString &path, const QSize &target) {
    QImage image(target, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    if (path.endsWith(QLatin1String(".svg"))) {
        if (QFileInfo(path).size() > MaxSvgBytes)
            return image;
        QSvgRenderer renderer(path);
        if (!renderer.isValid())
            return image;
        renderer.setAspectRatioMode(Qt::KeepAspectRatio);
        QPainter painter(&image);
        renderer.render(&painter);
        return image;
    }
    QImageReader reader(path);
    const QSize original = reader.size();
    if (!original.isValid() || original.width() > MaxRasterSide || original.height() > MaxRasterSide)
        return image;
    reader.setScaledSize(original.scaled(target, Qt::KeepAspectRatio));
    const QImage picture = reader.read();
    if (picture.isNull())
        return image;
    QPainter painter(&image);
    painter.drawImage((target.width() - picture.width()) / 2, (target.height() - picture.height()) / 2, picture);
    return image;
}

QImage IconPackImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize) {
    const QString pack = QUrl::fromPercentEncoding(id.section('/', 0, 0).toUtf8());
    const QString sample = id.section('/', 1, 1);
    // Bounded whatever a caller asks for, since decoding happens at this size.
    const int side = requestedSize.isValid() && !requestedSize.isEmpty() ? std::clamp(std::max(requestedSize.width(), requestedSize.height()), 16, 192) : 48;
    const QSize target(side, side);
    if (size)
        *size = target;
    // The address names a pack and a sample rather than a file, so the lookup stays
    // scoped to that pack's own chain however the address was made.
    const QString path = IconThemes::findIcon(m_env, pack, IconThemes::sampleNames(sample), side);
    if (path.isEmpty()) {
        QImage empty(target, QImage::Format_ARGB32_Premultiplied);
        empty.fill(Qt::transparent);
        return empty;
    }
    return render(path, target);
}
