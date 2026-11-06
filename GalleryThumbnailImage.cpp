#include "GalleryThumbnailImage.h"

#include <QQmlEngine>
#include <QThread>
#include <QtQuick/private/qquickimagebase_p_p.h>

namespace {
constexpr auto CacheProperty = "_zoinGalleryThumbnailTextures";
}

GalleryThumbnailTextureCache::GalleryThumbnailTextureCache(QObject *parent, qsizetype byteBudget)
    : QObject(parent), _pixmaps(qMax(qsizetype(0), byteBudget)) {}

GalleryThumbnailTextureCache *GalleryThumbnailTextureCache::forEngine(QQmlEngine *engine, bool create) {
    if (!engine) return nullptr;
    Q_ASSERT(QThread::currentThread() == engine->thread());
    auto *cache = static_cast<GalleryThumbnailTextureCache *>(
        engine->property(CacheProperty).value<QObject *>());
    if (!cache && create) {
        cache = new GalleryThumbnailTextureCache(engine);
        engine->setProperty(CacheProperty, QVariant::fromValue(static_cast<QObject *>(cache)));
    }
    return cache;
}

bool GalleryThumbnailTextureCache::retain(const QQuickPixmap &pixmap) {
    Q_ASSERT(QThread::currentThread() == thread());
    if (!_enabled || !pixmap.isReady()) return false;
    const QUrl &url = pixmap.url();
    if (url.scheme() != QLatin1String("image")
        || !url.path().startsWith(QLatin1String("/zg-thumb-"))) return false;
    auto *factory = pixmap.textureFactory();
    if (!factory) return false;
    if (_pixmaps.object(factory)) return true; // Touch the LRU without copying pixels.
    const qsizetype bytes = factory->textureByteCount();
    if (bytes <= 0 || bytes > _pixmaps.maxCost()) return false;
    auto *pin = new QQuickPixmap;
    pin->setPixmap(pixmap);
    return _pixmaps.insert(factory, pin, bytes);
}

void GalleryThumbnailTextureCache::shutdown() {
    _enabled = false;
    _pixmaps.clear();
}

GalleryThumbnailImage::GalleryThumbnailImage(QQuickItem *parent)
    : QQuickImage(parent) {}

void GalleryThumbnailImage::pixmapChange() {
    QQuickImage::pixmapChange();
    if (!cache()) return;
    const auto *pixmap = QQuickImageBasePrivate::get(this)->currentPix;
    // retainWhileLoading can still expose the preceding identity's pixels.
    if (!pixmap || pixmap->url() != source() || !pixmap->isReady()) return;
    if (!_textureCache)
        _textureCache = GalleryThumbnailTextureCache::forEngine(qmlEngine(this));
    if (_textureCache) _textureCache->retain(*pixmap);
}
