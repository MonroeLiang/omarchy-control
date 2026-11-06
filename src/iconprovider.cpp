#include "iconprovider.h"

#include <QBrush>
#include <QFile>
#include <QImageReader>
#include <QPainter>
#include <QSvgRenderer>
#include <QUrl>

IconProvider::IconProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}

// A picture cut to exactly the size asked for, with the desktop's corners. Done here
// rather than with a shader so it looks the same on every renderer, and so a 4K
// wallpaper never has to be decoded at full size for a small tile.
static QImage thumbnail(const QString &id, const QSize &target) {
    const int radius = id.section('/', 1, 1).toInt();
    // Section 2 is the file's age, there only to tell one picture's address from another.
    const QString path = QUrl::fromPercentEncoding(id.section('/', 3).toUtf8());
    QImage result(target, QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize original = reader.size();
    if (original.isValid() && !original.isEmpty())
        reader.setScaledSize(original.scaled(target, Qt::KeepAspectRatioByExpanding));
    QImage picture = reader.read();
    if (picture.isNull())
        return result;
    const QImage covered = picture.scaled(target, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    const QImage cropped = covered.copy((covered.width() - target.width()) / 2, (covered.height() - target.height()) / 2, target.width(), target.height());
    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QBrush(cropped));
    painter.drawRoundedRect(QRectF(QPointF(0, 0), QSizeF(target)), radius, radius);
    return result;
}

QImage IconProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize) {
    if (id.startsWith(QLatin1String("thumb/"))) {
        const QSize target = requestedSize.isValid() && !requestedSize.isEmpty() ? requestedSize : QSize(320, 200);
        if (size)
            *size = target;
        return thumbnail(id, target);
    }
    const QString name = id.section('?', 0, 0);
    const QString color = id.section("color=", 1, 1);
    const QSize target = requestedSize.isValid() && !requestedSize.isEmpty() ? requestedSize : QSize(48, 48);

    QImage image(target, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QFile file(":/icons/" + name + ".svg");
    if (file.open(QIODevice::ReadOnly)) {
        QByteArray svg = file.readAll();
        svg.replace("currentColor", color.isEmpty() ? QByteArray("#888888") : "#" + color.toLatin1());
        QSvgRenderer renderer(svg);
        QPainter painter(&image);
        renderer.render(&painter);
    }
    if (size)
        *size = target;
    return image;
}
