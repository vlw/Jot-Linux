// Renders the bundled Breeze SVG icons crisply at the requested size, recolored
// to any color: image://icon/<name>?<#rrggbb or #aarrggbb>
#pragma once

#include <QQuickImageProvider>

namespace jot {

class IconProvider : public QQuickImageProvider {
public:
    IconProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};

} // namespace jot
