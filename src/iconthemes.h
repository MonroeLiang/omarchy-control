#pragma once

#include <QHash>
#include <QImage>
#include <QList>
#include <QQuickImageProvider>
#include <QString>
#include <QStringList>

#include "env.h"

// Reading icon packs as the freedesktop Icon Theme Specification lays them out. Only
// descriptors and a pack's own folders are read, never every image in it. Nothing here
// runs anything from a pack, reaches the network, or changes which pack is in use.
struct IconPack {
    struct Directory {
        QString subdir, type = QStringLiteral("Threshold");
        int size = 0, scale = 1, minSize = 0, maxSize = 0, threshold = 2;
        int distance(int wanted) const;
    };

    QString id;                // The folder's name: what settings store and what inherits it.
    QString name;              // What to call it, in this language where the pack says.
    QString comment;
    QString indexPath;
    QStringList inherits;
    QStringList locations;     // Every <root>/<id> folder, since one theme can span roots.
    QList<Directory> directories;
    bool hidden = false;
    bool found = false;        // An index.theme was there and readable.
    QString problem;           // Why this could not be used, for diagnostics.

    // A pack with no icon folders is a cursor theme, or just an alias like "default".
    bool hasIcons() const { return !directories.isEmpty(); }
};

namespace IconThemes {

// Icon folders in lookup order: the user's first, then the legacy folder, then shared ones.
QStringList searchRoots(const Env &env);

// A theme id ends up as a path component and as an argument, so it has to be one name.
bool isValidId(const QString &id);

IconPack parseIndex(const QByteArray &contents, const QString &id, const QString &language = {});
IconPack load(const Env &env, const QString &id, const QString &language = {});

// Every installed pack with icons, sorted by name. Hidden ones are left out unless
// `alwaysInclude` names them. What could not be read lands in `diagnostics`.
QList<IconPack> discover(const Env &env, const QString &alwaysInclude = {}, QStringList *diagnostics = nullptr);

// The icon file for the first of `names` found in this pack, then what it inherits, then
// hicolor. Only that chain is searched, so a preview can never show another pack's icons.
// `fromTheme` says which pack it really came from.
QString findIcon(const Env &env, const QString &id, const QStringList &names, int size, QString *fromTheme = nullptr);

// What the current Omarchy theme asks for, with the same fallback omarchy-theme-set-gnome uses.
QString desktopDefault(const Env &env);

// The samples a pack is recognized by, in the order they are drawn.
QStringList sampleKeys();
QStringList sampleNames(const QString &key);

// `gsettings get` prints a GVariant string: quoted, with escapes.
QString parseGVariantString(const QByteArray &output, bool *ok);

}  // namespace IconThemes

// Draws a sample from one candidate pack: image://iconpacks/<id>/<sample>/<version>. It
// asks the pack directly, so looking at a pack never touches the theme in use, and it is
// separate from the app's own image://icons/ glyphs.
class IconPackImageProvider : public QQuickImageProvider {
public:
    explicit IconPackImageProvider(const Env &env);
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

    // Rendered no larger than `target`, and refused if the file is unreasonably big.
    static QImage render(const QString &path, const QSize &target);

private:
    const Env m_env;
};
