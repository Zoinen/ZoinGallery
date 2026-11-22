#include "GalleryPageController.h"

#include "DisplayColorSpace.h"
#include "ProviderImageStore.h"

#include <ZoinGallery/MediaTimingTrace.h>
#include <QCache>
#include <QMetaObject>

namespace ZoinGallery {
namespace {

constexpr qint64 MaximumCacheBytes = 64 * 1024 * 1024;

QString cacheKey(int page, QSize size, QRect clip)
{
    return QString::number(page) + u':' + QString::number(size.width()) + u':'
        + QString::number(size.height()) + u':' + QString::number(clip.x()) + u':'
        + QString::number(clip.y()) + u':' + QString::number(clip.width()) + u':'
        + QString::number(clip.height());
}

} // namespace

struct GalleryPageController::Result {
    ImagePage page;
    QSize bounds;
    QSize fullSize;
    QRect clip;
    int count = 0;
    int index = 0;
    int repeats = 0;
    bool active = false;
    bool animated = false;
    bool scalable = false;
    bool prefetch = false;
};

class GalleryPageController::Worker final : public QObject {
public:
    Worker(QSharedPointer<ImageSourceProvider> provider, qint64 budget)
        : _provider(std::move(provider)),
          _frames(budget < 0 ? MaximumCacheBytes : qMin(budget, MaximumCacheBytes))
    {}

    Result render(const QString &key, const QString &path, const QString &name,
                  const ImageSourceDescriptor &descriptor, int index, QSize bounds,
                  QSize fullSize, QRect clip, bool prefetch,
                  const QSharedPointer<ImageSourceCancellation> &cancel)
    {
        const auto canceled = [&cancel] { return cancel->isCanceled(); };
        const bool pdf = PagedImageDocument::isPdf(name);
        Result result;
        result.index = index;
        result.bounds = bounds;
        result.fullSize = fullSize;
        result.clip = clip;
        result.active = pdf;
        result.prefetch = prefetch;
        MediaTimingTrace::Span timing(QStringLiteral("qt.gallery.paged_render"),
            {{QStringLiteral("name"), name}, {QStringLiteral("page"), index},
             {QStringLiteral("prefetch"), prefetch}, {QStringLiteral("detail"), !clip.isEmpty()}});
        if (_key != key)
            reset();
        if (key.isEmpty() || canceled())
            return result;
        if (!_document) {
            if (descriptor.isValid() && path.isEmpty() && _provider && !_lease)
                _lease = _provider->materialize(descriptor, cancel);
            const auto local = _lease ? _lease->localPath() : path;
            if (!local.isEmpty() && !canceled())
                _document = PagedImageDocument::open(local, name, result.page.error, true);
            else
                result.page.error = PageError::CannotRead;
            _key = key;
        }
        if (!_document || canceled())
            return result;
        result.count = _document->pageCount();
        result.animated = _document->animated();
        result.repeats = _document->repeats();
        result.scalable = _document->scalable();
        result.active = pdf || result.count != 1;
        if (!result.active)
            return result;
        const QSize target = clip.isEmpty()
            ? boundedPageRaster(_document->pageSize(index), bounds, result.scalable) : fullSize;
        const QString frameKey = cacheKey(index, target, clip);
        if (const auto *frame = _frames.object(frameKey)) {
            result.page = *frame;
            timing.set(QStringLiteral("cacheHit"), true);
        } else {
            result.page = _document->render(index, target, clip, canceled);
            if (result.page.error == PageError::None && !result.page.pixels.isNull() && !canceled()) {
                result.page.pixels = DisplayColorSpace::convertImage(result.page.pixels);
                _frames.insert(frameKey, new ImagePage(result.page), result.page.pixels.sizeInBytes());
            }
        }
        result.count = _document->pageCount();
        timing.set(QStringLiteral("renderWidth"), result.page.pixels.width());
        timing.set(QStringLiteral("renderHeight"), result.page.pixels.height());
        return result;
    }

private:
    void reset()
    {
        _document.reset();
        _lease.reset();
        _frames.clear();
        _key.clear();
    }
    QSharedPointer<ImageSourceProvider> _provider;
    QSharedPointer<ImageSourceLease> _lease;
    QCache<QString, ImagePage> _frames;
    std::unique_ptr<PagedImageDocument> _document;
    QString _key;
};

GalleryPageController::GalleryPageController(
    const QString &sessionId, const QString &providerName,
    QSharedPointer<ProviderImageStore> store, QSharedPointer<ImageSourceProvider> provider,
    qint64 cacheBudget, QObject *parent)
    : QObject(parent), _store(std::move(store)), _idPrefix(sessionId + QStringLiteral("-paged-")),
      _providerPrefix(QStringLiteral("image://") + providerName + u'/'),
      _worker(new Worker(std::move(provider), cacheBudget))
{
    _thread.setObjectName(QStringLiteral("gallery-pages"));
    connect(&_thread, &QThread::finished, _worker, &QObject::deleteLater);
    _timer.setSingleShot(true);
    _timer.setTimerType(Qt::PreciseTimer);
    connect(&_timer, &QTimer::timeout, this, &GalleryPageController::advance);
}

GalleryPageController::~GalleryPageController()
{
    cancel();
    clearImages();
    if (_thread.isRunning()) {
        _thread.quit();
        _thread.wait();
    } else {
        delete _worker;
    }
}

void GalleryPageController::cancel()
{
    ++_generation;
    if (_cancellation)
        _cancellation->cancel();
}

void GalleryPageController::clearImages()
{
    _store->remove({_imageId, _detailId});
    _imageId.clear();
    _detailId.clear();
    _source.clear();
    _detailSource.clear();
    _detailRect = {};
}

void GalleryPageController::present(
    const QString &path, const QString &name, const QString &revision,
    const ImageSourceDescriptor &descriptor, QSize bounds, bool visible)
{
    const QString key = PagedImageDocument::canOpen(name) && (!path.isEmpty() || descriptor.isValid())
        ? (descriptor.isValid() ? descriptor.accessKey() : path + u'\x1f' + revision) : QString{};
    const bool changedSource = _key != key;
    const bool changedBounds = _bounds != bounds;
    const bool becameVisible = !_visible && visible;
    _visible = visible;
    _bounds = bounds;
    if (changedSource) {
        cancel();
        clearImages();
        _timer.stop();
        _key = key;
        _path = path;
        _name = name;
        _descriptor = descriptor;
        _active = !key.isEmpty() && PagedImageDocument::isPdf(name);
        _scalable = _active;
        _animated = _playing = _manual = _atEnd = false;
        _requestedIndex = _displayedIndex = _count = _completedLoops = 0;
        _error.clear();
        _size = {};
        _lastBounds = {};
        _clip = {};
        _detailPending = false;
        _prefetchPending = false;
        _pending = !_key.isEmpty() || _thread.isRunning();
        emit changed();
    } else if (changedBounds && !_key.isEmpty()
               && (_active || _count == 0) && bounds != _lastBounds) {
        _pending = true;
    }
    if (!visible)
        _timer.stop();
    else if (becameVisible)
        armTimer();
    dispatch();
}

void GalleryPageController::clear()
{
    present({}, {}, {}, {}, {}, false);
}

bool GalleryPageController::step(int direction)
{
    if (!_active || (_count >= 0 && _count <= 1) || direction == 0)
        return false;
    const int next = qMax(0, _requestedIndex + (direction > 0 ? 1 : -1));
    _timer.stop();
    _playing = false;
    _manual = true;
    _atEnd = false;
    _prefetchPending = false;
    if (next != _requestedIndex && (_count < 0 || next < _count)) {
        _requestedIndex = next;
        _detailPending = false;
        _pending = true;
        cancel();
        dispatch();
    }
    emit changed();
    return true;
}

void GalleryPageController::togglePlayback()
{
    if (!_animated)
        return;
    _manual = true;
    _playing = !_playing;
    if (_playing && _atEnd) {
        _atEnd = false;
        _requestedIndex = 0;
        _completedLoops = 0;
        _pending = true;
        dispatch();
    } else if (_playing) {
        armTimer();
    } else {
        _timer.stop();
        if (_requestedIndex != _displayedIndex) {
            cancel();
            _requestedIndex = _displayedIndex;
            _pending = false;
        }
    }
    emit changed();
}

void GalleryPageController::requestDetail(const QSize &fullSize, const QRect &clip)
{
    if (!_scalable || _source.isEmpty() || _requestedIndex != _displayedIndex)
        return;
    if (_detailSize == fullSize && _clip == clip)
        return;
    _detailSize = fullSize;
    _clip = clip;
    _detailPending = !clip.isEmpty();
    if (!_detailPending && !_detailSource.isEmpty()) {
        _store->remove(_detailId);
        _detailId.clear();
        _detailSource.clear();
        _detailRect = {};
        emit changed();
    }
    dispatch();
}

void GalleryPageController::dispatch()
{
    if (_busy || (!_pending && !_detailPending && !_prefetchPending)
        || (!_key.isEmpty() && (_bounds.isEmpty() || !_visible)))
        return;
    if (!_thread.isRunning()) {
        _worker->moveToThread(&_thread);
        _thread.start();
    }
    const bool detail = !_pending && _detailPending;
    const bool prefetch = !_pending && !detail && _prefetchPending;
    const QSize fullSize = detail ? _detailSize : QSize{};
    const QRect clip = detail ? _clip : QRect{};
    const int page = prefetch ? _displayedIndex + 1 : _requestedIndex;
    _pending = false;
    _detailPending = false;
    _prefetchPending = false;
    _busy = true;
    const quint64 generation = _generation;
    const auto cancellation = QSharedPointer<ImageSourceCancellation>::create();
    _cancellation = cancellation;
    QMetaObject::invokeMethod(_worker, [this, worker = _worker, generation, cancellation,
        key = _key, path = _path, name = _name, descriptor = _descriptor, bounds = _bounds,
        page, fullSize, clip, prefetch] {
        auto result = worker->render(key, path, name, descriptor, page, bounds, fullSize, clip,
                                     prefetch, cancellation);
        QMetaObject::invokeMethod(this, [this, generation, result = std::move(result)]() mutable {
            complete(generation, std::move(result));
        });
    });
}

void GalleryPageController::complete(quint64 generation, Result result)
{
    _busy = false;
    if (generation != _generation || _key.isEmpty() || result.prefetch) {
        dispatch();
        return;
    }
    if (!result.clip.isEmpty()) {
        if (_clip == result.clip && _detailSize == result.fullSize && _displayedIndex == result.index
            && !result.page.pixels.isNull()) {
            _detailRect = QRectF(qreal(result.clip.x()) / result.fullSize.width(),
                qreal(result.clip.y()) / result.fullSize.height(),
                qreal(result.clip.width()) / result.fullSize.width(),
                qreal(result.clip.height()) / result.fullSize.height());
            _detailSource = publish(result.page.pixels, _detailId);
            emit changed();
        }
        dispatch();
        return;
    }
    if (result.bounds != _bounds) {
        _pending = true;
        dispatch();
        return;
    }

    const bool firstFrame = _source.isEmpty();
    const bool pageChanged = result.index != _displayedIndex;
    _lastBounds = result.bounds;
    _active = result.active;
    _count = result.count;
    _animated = result.animated;
    _scalable = result.scalable;
    _repeats = result.repeats;
    if (_active && result.page.error == PageError::EndOfSequence) {
        _requestedIndex = _displayedIndex;
        if (_playing)
            advance();
    } else if (_active && result.page.pixels.isNull()) {
        _playing = false;
        _error = result.page.error == PageError::PasswordRequired
            ? QStringLiteral("password") : QStringLiteral("render");
        clearImages();
    } else if (_active) {
        _error.clear();
        if (pageChanged) {
            _store->remove(_detailId);
            _detailId.clear();
            _detailSource.clear();
            _detailRect = {};
            _clip = {};
        }
        _source = publish(result.page.pixels, _imageId);
        _size = result.page.size;
        _displayedIndex = result.index;
        _delayMs = qMax(10, result.page.delayMs);
        if (firstFrame && !_manual)
            _playing = _animated;
        if (firstFrame || pageChanged)
            armTimer();
        _prefetchPending = (_playing || _scalable) && _count > _displayedIndex + 1;
    }
    emit changed();
    dispatch();
}

void GalleryPageController::advance()
{
    if (!_playing || !_visible)
        return;
    if (_count >= 0 && _displayedIndex + 1 >= _count) {
        if (_repeats >= 0 && _completedLoops >= _repeats) {
            _playing = false;
            _atEnd = true;
            emit changed();
            return;
        }
        ++_completedLoops;
        _requestedIndex = 0;
    } else {
        _requestedIndex = _displayedIndex + 1;
    }
    _pending = true;
    dispatch();
}

void GalleryPageController::armTimer()
{
    if (_playing && _visible && _requestedIndex == _displayedIndex && !_source.isEmpty())
        _timer.start(_delayMs);
}

QUrl GalleryPageController::publish(const QImage &image, QString &id)
{
    _store->remove(id);
    id = _idPrefix + QString::number(++_serial);
    _store->publish(id, image);
    return QUrl(_providerPrefix + id);
}

} // namespace ZoinGallery
