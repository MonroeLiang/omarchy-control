#pragma once

#include <QString>
#include <QStringList>
#include <atomic>
#include <functional>

// Unpacks a downloaded icon pack archive into a private folder, for the folder importer to
// take each pack from as it would from any folder. Read with libarchive, entry by entry,
// so every limit counts what is really there rather than what the headers claim, and each
// path is checked before anything is written. Nothing in an archive is ever run, and only
// tar (plain or compressed), zip, and 7z are read. Archive import follows Iconic's
// (github.com/UnderStackDev/omarchy_iconic, MIT), with the checks of this app's importer.
namespace IconArchive {

struct Result {
    QString error;        // Why nothing can be imported from it, if so.
    QStringList packs;    // Each pack's folder, packs that others build on first.
    QStringList ids;      // Every pack found, so their links between each other can be allowed.
    QStringList notes;    // What was left out, and why.
};

// Whether this looks like an archive this app reads, by its name.
bool isArchive(const QString &path);

// The name a pack whose index.theme sits at the archive's top level takes: the file's
// name without its archive extensions.
QString baseName(const QString &path);

// Unpacks `archive` into `into`, which must not exist yet. Stops at the first sign of
// trouble: a path outside the archive, writing through a link, or too much data.
Result extract(const QString &archive, const QString &into, const std::atomic_bool &cancel, const std::function<void(qreal)> &progress);

}  // namespace IconArchive
