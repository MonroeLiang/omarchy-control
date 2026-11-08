#include "iconarchive.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>

#include <algorithm>

#include <archive.h>
#include <archive_entry.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "iconthemes.h"

namespace {

// The same order as Iconic's limits, counted from the data itself.
constexpr qint64 MaxEntries = 300000;
constexpr qint64 MaxTotalBytes = qint64(2) * 1024 * 1024 * 1024;
constexpr qint64 MaxFileBytes = qint64(64) * 1024 * 1024;
constexpr int MaxDepth = 24;
constexpr qint64 MaxIndexBytes = 1024 * 1024;

QString tr(const char *text) {
    return QCoreApplication::translate("IconArchive", text);
}

QByteArray local(const QString &path) {
    return QFile::encodeName(path);
}

const QStringList &suffixes() {
    // Longest first, so .tar.gz is not taken for .gz.
    static const QStringList list{".tar.gz", ".tar.xz", ".tar.bz2", ".tar.zst", ".tar.lz4", ".tar.lzma", ".tgz", ".txz", ".tbz2", ".tbz", ".tzst", ".tar", ".zip", ".7z"};
    return list;
}

// A path inside the archive, relative and checked: nothing absolute, nothing that climbs.
bool cleanPath(const QString &raw, QString *out) {
    if (raw.startsWith('/'))
        return false;
    QStringList parts;
    for (const QString &part : raw.split('/')) {
        if (part.isEmpty() || part == QLatin1String("."))
            continue;
        if (part == QLatin1String(".."))
            return false;
        parts << part;
    }
    if (parts.size() > MaxDepth)
        return false;
    *out = parts.join('/');
    return true;
}

// Makes the folders a path needs, refusing any that is not a real folder: a link the
// archive made earlier could otherwise send a later file anywhere on the disk.
bool makeParents(const QString &root, const QString &relative, QString *error) {
    QString path = root;
    const QStringList parts = relative.split('/');
    for (qsizetype i = 0; i + 1 < parts.size(); ++i) {
        path += "/" + parts[i];
        struct stat info;
        if (::lstat(local(path).constData(), &info) == 0) {
            if (!S_ISDIR(info.st_mode)) {
                *error = tr("The archive writes through a link at %1, so nothing was imported.").arg(parts.mid(0, i + 1).join('/'));
                return false;
            }
            continue;
        }
        if (::mkdir(local(path).constData(), 0755) != 0) {
            *error = tr("%1 could not be written.").arg(relative);
            return false;
        }
    }
    return true;
}

// A later entry for the same path replaces an earlier file, as tar does. Anything else in
// the way is a conflict the archive should not have.
bool clearForFile(const QString &path, const QString &relative, QString *error) {
    struct stat info;
    if (::lstat(local(path).constData(), &info) != 0)
        return true;
    if ((S_ISREG(info.st_mode) || S_ISLNK(info.st_mode)) && ::unlink(local(path).constData()) == 0)
        return true;
    *error = tr("The archive has %1 more than once, in ways that conflict, so nothing was imported.").arg(relative);
    return false;
}

bool writeAll(int fd, const char *data, qint64 size, const QString &relative, QString *error) {
    qint64 written = 0;
    while (written < size) {
        const ssize_t wrote = ::write(fd, data + written, size - written);
        if (wrote <= 0) {
            *error = errno == ENOSPC ? tr("There is not enough disk space to import this pack.") : tr("%1 could not be written.").arg(relative);
            return false;
        }
        written += wrote;
    }
    return true;
}

struct Found {
    QString folder, id;
    QStringList inherits;
};

// Packs others in the same archive build on come first, so links and inheritance between
// them find their target already installed.
QStringList ordered(const QList<Found> &packs) {
    QHash<QString, int> level;
    for (const Found &pack : packs)
        level.insert(pack.id, 0);
    for (int round = 0; round < packs.size(); ++round) {
        bool moved = false;
        for (const Found &pack : packs)
            for (const QString &parent : pack.inherits)
                if (level.contains(parent) && parent != pack.id && level[pack.id] <= level[parent]) {
                    level[pack.id] = level[parent] + 1;
                    moved = true;
                }
        if (!moved)
            break;
    }
    QList<Found> sorted = packs;
    std::stable_sort(sorted.begin(), sorted.end(), [&level](const Found &a, const Found &b) { return level[a.id] < level[b.id]; });
    QStringList folders;
    for (const Found &pack : std::as_const(sorted))
        folders << pack.folder;
    return folders;
}

}  // namespace

namespace IconArchive {

bool isArchive(const QString &path) {
    for (const QString &suffix : suffixes())
        if (path.endsWith(suffix, Qt::CaseInsensitive))
            return true;
    return false;
}

QString baseName(const QString &path) {
    const QString name = QFileInfo(path).fileName();
    for (const QString &suffix : suffixes())
        if (name.endsWith(suffix, Qt::CaseInsensitive))
            return name.left(name.size() - suffix.size());
    return QFileInfo(path).completeBaseName();
}

Result extract(const QString &archivePath, const QString &into, const std::atomic_bool &cancel, const std::function<void(qreal)> &progress) {
    Result result;
    const QString content = into + "/content";
    if (::mkdir(local(into).constData(), 0700) != 0 || ::mkdir(local(content).constData(), 0755) != 0) {
        result.error = tr("%1 could not be written.").arg(into);
        return result;
    }
    struct archive *reader = archive_read_new();
    // Named formats and filters only: the catch-alls would also run outside programs for
    // some rare compressions, and read formats no icon pack comes in.
    archive_read_support_filter_gzip(reader);
    archive_read_support_filter_bzip2(reader);
    archive_read_support_filter_xz(reader);
    archive_read_support_filter_lzma(reader);
    archive_read_support_filter_zstd(reader);
    archive_read_support_filter_lz4(reader);
    archive_read_support_format_tar(reader);
    archive_read_support_format_gnutar(reader);
    archive_read_support_format_zip(reader);
    archive_read_support_format_7zip(reader);
    const QString name = QFileInfo(archivePath).fileName();
    if (archive_read_open_filename(reader, local(archivePath).constData(), 64 * 1024) != ARCHIVE_OK) {
        result.error = tr("%1 could not be opened as an archive.").arg(name);
        archive_read_free(reader);
        return result;
    }
    const qreal fileSize = qMax<qreal>(1, QFileInfo(archivePath).size());
    QByteArray buffer(256 * 1024, Qt::Uninitialized);
    qint64 entries = 0, total = 0, special = 0;
    int status = ARCHIVE_OK;
    struct archive_entry *entry = nullptr;
    QString &error = result.error;
    while (error.isEmpty() && ((status = archive_read_next_header(reader, &entry)) == ARCHIVE_OK || status == ARCHIVE_WARN)) {
        if (cancel.load()) {
            error = tr("Import cancelled.");
            break;
        }
        if (++entries > MaxEntries) {
            error = tr("This archive has too many files to be an icon pack.");
            break;
        }
        const char *utf8 = archive_entry_pathname_utf8(entry);
        const char *raw = utf8 ? utf8 : archive_entry_pathname(entry);
        QString path;
        if (!raw || !cleanPath(QString::fromUtf8(raw), &path)) {
            error = tr("The archive holds a path outside itself, %1, so nothing was imported.").arg(QString::fromUtf8(raw ? raw : ""));
            break;
        }
        // Folders macOS adds to a zip are no part of any pack.
        if (path.isEmpty() || path == QLatin1String("__MACOSX") || path.startsWith(QLatin1String("__MACOSX/")) || path.endsWith(QLatin1String(".DS_Store"))) {
            archive_read_data_skip(reader);
            continue;
        }
        const QString target = content + "/" + path;
        if (!makeParents(content, path, &error))
            break;
        const mode_t type = archive_entry_filetype(entry);
        if (const char *hardlink = archive_entry_hardlink(entry)) {
            // Another name for a file already unpacked: copied, so nothing is shared with
            // anything outside, and the copy is checked like any other file.
            QString linked;
            struct stat info;
            if (!cleanPath(QString::fromUtf8(hardlink), &linked) || linked.isEmpty() || ::lstat(local(content + "/" + linked).constData(), &info) != 0
                || !S_ISREG(info.st_mode)) {
                error = tr("The archive links %1 to something that is not a file in it, so nothing was imported.").arg(path);
                break;
            }
            total += info.st_size;
            if (total > MaxTotalBytes) {
                error = tr("This archive is too large to be an icon pack.");
                break;
            }
            if (!clearForFile(target, path, &error))
                break;
            if (!QFile::copy(content + "/" + linked, target)) {
                error = tr("%1 could not be written.").arg(path);
                break;
            }
        } else if (type == AE_IFDIR) {
            struct stat info;
            if (::lstat(local(target).constData(), &info) == 0) {
                if (!S_ISDIR(info.st_mode)) {
                    error = tr("The archive writes through a link at %1, so nothing was imported.").arg(path);
                    break;
                }
            } else if (::mkdir(local(target).constData(), 0755) != 0) {
                error = tr("%1 could not be written.").arg(path);
                break;
            }
        } else if (type == AE_IFLNK) {
            const char *to = archive_entry_symlink_utf8(entry);
            if (!to)
                to = archive_entry_symlink(entry);
            // Where a relative link leads is checked again, file by file, by the importer.
            if (!to || !*to || to[0] == '/') {
                error = tr("The link %1 points outside the archive, so nothing was imported.").arg(path);
                break;
            }
            if (!clearForFile(target, path, &error))
                break;
            if (::symlink(to, local(target).constData()) != 0) {
                error = tr("%1 could not be written.").arg(path);
                break;
            }
        } else if (type == AE_IFREG) {
            if (archive_entry_size_is_set(entry) && archive_entry_size(entry) > MaxFileBytes) {
                error = tr("%1 is too large to be part of an icon pack.").arg(path);
                break;
            }
            if (!clearForFile(target, path, &error))
                break;
            const int fd = ::open(local(target).constData(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644);
            if (fd < 0) {
                error = tr("%1 could not be written.").arg(path);
                break;
            }
            qint64 size = 0;
            la_ssize_t got = 0;
            while (error.isEmpty() && (got = archive_read_data(reader, buffer.data(), size_t(buffer.size()))) > 0) {
                size += got;
                total += got;
                if (size > MaxFileBytes)
                    error = tr("%1 is too large to be part of an icon pack.").arg(path);
                else if (total > MaxTotalBytes)
                    error = tr("This archive is too large to be an icon pack.");
                else
                    writeAll(fd, buffer.constData(), got, path, &error);
                if (cancel.load())
                    error = tr("Import cancelled.");
            }
            ::close(fd);
            if (error.isEmpty() && got < 0)
                error = tr("%1 could not be read from the archive.").arg(path);
        } else {
            // Devices, pipes and sockets: no pack has a use for them.
            ++special;
            archive_read_data_skip(reader);
        }
        progress(qMin<qreal>(0.99, archive_filter_bytes(reader, -1) / fileSize));
    }
    if (error.isEmpty() && status != ARCHIVE_EOF)
        error = tr("%1 could not be read to the end. It may be damaged or incomplete.").arg(name);
    archive_read_free(reader);
    if (!error.isEmpty())
        return result;
    if (special > 0)
        result.notes << QCoreApplication::translate("IconArchive", "%n special files, such as devices, were left out.", "", int(special));

    // A pack at the archive's top level takes the archive's name.
    QString search = content;
    if (QFileInfo(content + "/index.theme").isFile() && !QFileInfo(content + "/index.theme").isSymLink()) {
        const QString id = baseName(archivePath);
        if (!IconThemes::isValidId(id) || !QDir().rename(content, into + "/" + id)) {
            error = tr("The pack in %1 has no folder of its own, and the archive's name cannot be used for one.").arg(name);
            return result;
        }
        search = into + "/" + id;
    }
    QStringList indexes;
    if (search != content) {
        indexes << search + "/index.theme";
    } else {
        QDirIterator it(content, {"index.theme"}, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
        while (it.hasNext())
            indexes << it.next();
    }
    // Outer folders first, so a pack's own subfolders are never taken for packs.
    std::sort(indexes.begin(), indexes.end(), [](const QString &a, const QString &b) { return a.count('/') != b.count('/') ? a.count('/') < b.count('/') : a < b; });
    QList<Found> found;
    for (const QString &index : std::as_const(indexes)) {
        const QFileInfo info(index);
        if (info.isSymLink() || info.size() > MaxIndexBytes)
            continue;
        const QString folder = info.absolutePath();
        bool nested = false;
        for (const Found &pack : std::as_const(found))
            nested = nested || folder.startsWith(pack.folder + "/");
        if (nested)
            continue;
        const QString id = QFileInfo(folder).fileName();
        QFile file(index);
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const IconPack pack = IconThemes::parseIndex(file.readAll(), id);
        if (!IconThemes::isValidId(id))
            result.notes << tr("A folder called %1 cannot be used as a pack's name, so it was left out.").arg(id);
        else if (!pack.found)
            result.notes << tr("%1 has an index.theme that is not valid, so it was left out.").arg(id);
        else if (!pack.hasIcons())
            result.notes << tr("%1 has no icon folders, so it was left out. It may be a cursor theme, which goes elsewhere.").arg(pack.name);
        else if (std::any_of(found.begin(), found.end(), [&id](const Found &other) { return other.id == id; }))
            result.notes << tr("The archive has two packs called %1, so only the first was used.").arg(id);
        else
            found << Found{folder, id, pack.inherits};
    }
    for (const Found &pack : std::as_const(found))
        result.ids << pack.id;
    result.packs = ordered(found);
    if (result.packs.isEmpty())
        error = result.notes.isEmpty() ? tr("This archive has no icon pack in it.") : result.notes.join(' ');
    progress(1.0);
    return result;
}

}  // namespace IconArchive
