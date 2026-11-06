#ifndef GALLERYTHUMBNAILIMAGE_H
#define GALLERYTHUMBNAILIMAGE_H

#include <QCache>
#include <QPointer>
#include <QtQuick/private/qquickimage_p.h>
#include <QtQuick/private/qquickpixmap_p.h>

class QQmlEngine;

// Pins immutable thumbnail factories; Qt's unused-pixmap cache remains unchanged.
class GalleryThumbnailTextureCache : public QObject {
public:
    explicit GalleryThumbnailTextureCache(QObject *parent = nullptr,
                                         qsizetype byteBudget = 64 * 1024 * 1024);
    static GalleryThumbnailTextureCache *forEngine(QQmlEngine *engine, bool create = true);
    bool retain(const QQuickPixmap &pixmap);
    qsizetype retainedBytes() const { return _pixmaps.totalCost(); }
    qsizetype count() const { return _pixmaps.size(); }
    bool contains(QQuickTextureFactory *factory) const { return _pixmaps.contains(factory); }
    void shutdown();

private:
    QCache<QQuickTextureFactory *, QQuickPixmap> _pixmaps;
    bool _enabled = true;
};

class GalleryThumbnailImage : public QQuickImage {
    Q_OBJECT
public:
    explicit GalleryThumbnailImage(QQuickItem *parent = nullptr);

protected:
    void pixmapChange() override;

private:
    QPointer<GalleryThumbnailTextureCache> _textureCache;
};

#endif
