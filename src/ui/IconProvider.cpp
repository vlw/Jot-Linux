#include "IconProvider.h"

#include <QFile>
#include <QHash>
#include <QPainter>
#include <QSvgRenderer>

namespace jot {

QImage IconProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    const QString name = id.section('?', 0, 0);
    const QColor color(id.section('?', 1));
    const QSize target = requestedSize.isValid() && !requestedSize.isEmpty() ? requestedSize : QSize(16, 16);
    if (size) *size = target;

    static QHash<QString, QByteArray> cache;
    QByteArray svg = cache.value(name);
    if (svg.isEmpty()) {
        QFile f(":/icons/breeze/" + name + ".svg");
        if (!f.open(QIODevice::ReadOnly)) return {};
        svg = f.readAll();
        cache.insert(name, svg);
    }
    QSvgRenderer renderer(svg);
    QImage image(target, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    renderer.render(&p, QRectF(QPointF(0, 0), QSizeF(target)));
    // Monochrome tint: keep the icon's alpha, replace its colors.
    p.setCompositionMode(QPainter::CompositionMode_SourceIn);
    p.fillRect(image.rect(), color.isValid() ? color : QColor("#232629"));
    p.end();
    return image;
}

} // namespace jot
