#pragma once

#include <QQuickImageProvider>

// Renders the bundled line icons in any color, so one small SVG set follows the
// active theme: image://icons/<name>?color=rrggbb
class IconProvider : public QQuickImageProvider {
public:
    IconProvider();
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};
