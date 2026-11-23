#include "GalleryPageController.h"

#include "DisplayColorSpace.h"
#include "ProviderImageStore.h"

#include <ZoinGallery/MediaTimingTrace.h>
#include <QCache>
#include <QMetaObject>
#include <QMutex>

#include <limits>
#include <optional>

namespace ZoinGallery {
namespace {

constexpr qint64 MaximumCacheBytes = 64 * 1024 * 1024;

QString cacheKey(const GalleryPageController::Source &source, int page,
                 const GalleryPageController::Viewport &view, QSize fullSize = {}, QRect clip = {})
{
    QString key = QString::number(source.key.size()) + u':' + source.key + u':' + QString::number(page);
    const auto append = [&key](int value) { key += u':' + QString::number(value); };
    if (!clip.isEmpty()) {
        key += u'c';
        append(fullSize.width());
        append(fullSize.height());
        append(clip.x());
        append(clip.y());
        append(clip.width());
        append(clip.height());
    } else {
        append(view.bounds.width());
        append(view.bounds.height());
        append(view.rotation);
        if (view.pixelsPerPoint > 0) {
            key += u':' + QString::number(view.pixelsPerPoint, 'g', 17);
            append(view.origin.x());
            append(view.origin.y());
        }
    }
    return key;
}

QRect visiblePageRegion(QSize fullSize, const GalleryPageController::Viewport &view)
{
    const QSize displayed = view.rotation % 2 ? fullSize.transposed() : fullSize;
    const auto place = [](int margin, int offset) {
        return margin >= 0 ? margin / 2 : qBound(margin, offset, 0);
    };
    const QPoint position(place(view.bounds.width() - displayed.width(), view.origin.x()),
                          place(view.bounds.height() - displayed.height(), view.origin.y()));
    const QRect area = QRect(QPoint{}, view.bounds)
        .intersected(QRect(position, displayed)).translated(-position);
    QRect clip = area;
    switch (view.rotation) {
    case 1:
        clip = {area.y(), fullSize.height() - area.x() - area.width(), area.height(), area.width()};
        break;
    case 2:
        clip.moveTo(fullSize.width() - area.x() - area.width(),
                    fullSize.height() - area.y() - area.height());
        break;
    case 3:
        clip = {fullSize.width() - area.y() - area.height(), area.x(), area.height(), area.width()};
        break;
    }
    // Cover the viewport's final integer-pixel snap as well as the logical clip.
    return clip.adjusted(-2, -2, 2, 2).intersected(QRect(QPoint{}, fullSize));
}

} // namespace

struct GalleryPageController::Result {
    ImagePage page;
    Viewport viewport;
    QSize fullSize;
    QRect clip;
    QImage detail;
    QSize detailSize;
    QRect detailClip;
    int count = 0;
    int index = 0;
    int repeats = 0;
    bool active = false;
    bool animated = false;
    bool scalable = false;
};

class GalleryPageController::Worker final : public QObject {
public:
    Worker(QSharedPointer<ImageSourceProvider> provider, qint64 budget)
        : _provider(std::move(provider)),
          _frames(budget < 0 ? MaximumCacheBytes : qMin(budget, MaximumCacheBytes))
    {}

    std::optional<Result> cached(const QString &key)
    {
        QMutexLocker lock(&_cacheMutex);
        if (const auto *frame = _frames.object(key))
            return *frame;
        return std::nullopt;
    }

    bool canPrefetch() const { return _frames.maxCost() > 0; }

    Result render(const Source &source, int index, Viewport viewport,
                  QSize fullSize, QRect clip, bool prefetch, bool neighbor,
                  const QSharedPointer<ImageSourceCancellation> &cancel)
    {
        const auto canceled = [&cancel] { return cancel->isCanceled(); };
        const bool pdf = PagedImageDocument::isPdf(source.name);
        Result result;
        result.index = index;
        result.viewport = viewport;
        result.fullSize = fullSize;
        result.clip = clip;
        result.active = pdf;
        MediaTimingTrace::Span timing(QStringLiteral("qt.gallery.paged_render"),
            {{QStringLiteral("name"), source.name}, {QStringLiteral("page"), index},
             {QStringLiteral("prefetch"), prefetch}, {QStringLiteral("detail"), !clip.isEmpty()}});
        if (source.key.isEmpty() || canceled()) {
            if (!neighbor)
                reset();
            return result;
        }
        const QString frameKey = cacheKey(source, index, viewport, fullSize, clip);
        if (pdf) {
            if (auto frame = cached(frameKey)) {
                timing.set(QStringLiteral("cacheHit"), true);
                return *frame;
            }
        }

        // Warming another file must not discard the active decoder and stream.
        QSharedPointer<ImageSourceLease> neighborLease;
        std::unique_ptr<PagedImageDocument> neighborDocument;
        if (!neighbor && _key != source.key)
            reset();
        auto &document = neighbor ? neighborDocument : _document;
        auto &lease = neighbor ? neighborLease : _lease;
        if (!document) {
            if (source.descriptor.isValid() && source.path.isEmpty() && _provider && !lease)
                lease = _provider->materialize(source.descriptor, cancel);
            const auto local = lease ? lease->localPath() : source.path;
            if (!local.isEmpty() && !canceled())
                document = PagedImageDocument::open(local, source.name, result.page.error, !neighbor);
            else
                result.page.error = PageError::CannotRead;
            if (!neighbor)
                _key = source.key;
        }
        if (!document || canceled())
            return result;
        result.count = document->pageCount();
        result.animated = document->animated();
        result.repeats = document->repeats();
        result.scalable = document->scalable();
        result.active = pdf || result.count != 1;
        if (!result.active)
            return result;
        if (auto frame = !pdf ? cached(frameKey) : std::nullopt) {
            result.page = frame->page;
            timing.set(QStringLiteral("cacheHit"), true);
        } else {
            const QSize bounds = pdf && viewport.rotation % 2
                ? viewport.bounds.transposed() : viewport.bounds;
            const QSize target = clip.isEmpty()
                ? boundedPageRaster(document->pageSize(index), bounds, result.scalable) : fullSize;
            result.page = document->render(index, target, clip, canceled);
            if (result.page.error == PageError::None && !result.page.pixels.isNull() && !canceled()) {
                result.page.pixels = DisplayColorSpace::convertImage(result.page.pixels);
                if (pdf && clip.isEmpty() && viewport.pixelsPerPoint > 0) {
                    const QSizeF pixels = result.page.size * viewport.pixelsPerPoint;
                    const qreal limit = std::numeric_limits<int>::max() - 4;
                    if (pixels.isValid() && pixels.width() < limit && pixels.height() < limit) {
                        result.detailSize = pixels.toSize().expandedTo(QSize(1, 1));
                        result.detailClip = visiblePageRegion(result.detailSize, viewport);
                        if (!result.detailClip.isEmpty() && !canceled()) {
                            const auto detail = document->render(index, result.detailSize,
                                                                 result.detailClip, canceled);
                            result.detail = DisplayColorSpace::convertImage(detail.pixels);
                        }
                    }
                }
                if (viewport.pixelsPerPoint <= 0 || !result.detail.isNull()) {
                    QMutexLocker lock(&_cacheMutex);
                    _frames.insert(frameKey, new Result(result),
                                   result.page.pixels.sizeInBytes() + result.detail.sizeInBytes());
                }
            }
        }
        result.count = document->pageCount();
        timing.set(QStringLiteral("renderWidth"), result.page.pixels.width());
        timing.set(QStringLiteral("renderHeight"), result.page.pixels.height());
        return result;
    }

private:
    void reset()
    {
        _document.reset();
        _lease.reset();
        _key.clear();
    }
    QSharedPointer<ImageSourceProvider> _provider;
    QSharedPointer<ImageSourceLease> _lease;
    QMutex _cacheMutex;
    QCache<QString, Result> _frames;
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

void GalleryPageController::cancel(bool keepRender)
{
    ++_generation;
    if (_cancellation && !keepRender)
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

void GalleryPageController::present(Source source, Viewport viewport, bool visible,
                                    QList<Source> neighbors)
{
    const bool changedSource = _file != source;
    const bool changedBounds = _viewport.bounds != viewport.bounds
        || _viewport.rotation % 2 != viewport.rotation % 2;
    const bool changedViewport = _viewport != viewport;
    const bool becameVisible = !_visible && visible;
    _visible = visible;
    _viewport = viewport;
    if (changedSource || changedViewport || _neighbors != neighbors) {
        _neighbors = std::move(neighbors);
        _nextNeighbor = 0;
    }
    if (changedSource) {
        const bool pdf = !source.key.isEmpty() && PagedImageDocument::isPdf(source.name);
        cancel(pdf && cacheKey(source, 0, viewport) == _runningFrameKey);
        if (pdf) {
            // The viewport still owns the outgoing base and its sharp detail.
            _store->remove(_imageId);
            _imageId.clear();
            _source.clear();
        } else {
            clearImages();
        }
        _timer.stop();
        _file = std::move(source);
        _active = pdf;
        _scalable = _active;
        _animated = _playing = _manual = _atEnd = false;
        _requestedIndex = _displayedIndex = _count = _completedLoops = 0;
        _error.clear();
        _size = {};
        _detailSize = {};
        _clip = {};
        _detailPending = false;
        _prefetchPending = false;
        _pending = !_file.key.isEmpty() || _thread.isRunning();
        emit changed();
    } else if (!_file.key.isEmpty() && (_active || _count == 0)
               && (changedBounds || (changedViewport && _scalable && _source.isEmpty()))) {
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
    if (_file.key.isEmpty() && _busy)
        cancel();
    present({}, {}, false, {});
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
        cancel(cacheKey(_file, next, _viewport) == _runningFrameKey);
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
    if (_detailSize == fullSize && (_clip == clip
        || (!_detailPending && !_detailSource.isEmpty() && !clip.isEmpty() && _clip.contains(clip))))
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
    if (_pending && _scalable && !_viewport.bounds.isEmpty()) {
        if (const auto ready = _worker->cached(cacheKey(_file, _requestedIndex, _viewport))) {
            _pending = false;
            if (MediaTimingTrace::enabled())
                MediaTimingTrace::event(QStringLiteral("qt.gallery.prepared_page_presented"),
                    {{QStringLiteral("name"), _file.name}, {QStringLiteral("page"), ready->index},
                     {QStringLiteral("renderWidth"), ready->page.pixels.width()},
                     {QStringLiteral("renderHeight"), ready->page.pixels.height()}});
            applyFrame(*ready);
        }
    }
    if (_busy || (!_file.key.isEmpty() && (_viewport.bounds.isEmpty() || !_visible)))
        return;
    const bool detail = !_pending && _detailPending;
    const bool warming = !_pending && !detail;
    if (warming && (!_visible || _viewport.bounds.isEmpty() || !_worker->canPrefetch()))
        return;
    bool neighbor = false;
    Source source = _file;
    if (warming && !(_animated && _prefetchPending)) {
        while (_nextNeighbor < _neighbors.size()) {
            const auto &candidate = _neighbors.at(_nextNeighbor++);
            if (!_worker->cached(cacheKey(candidate, 0, _viewport))) {
                source = candidate;
                neighbor = true;
                break;
            }
        }
    }
    const bool pagePrefetch = warming && !neighbor && _prefetchPending;
    if (warming && !neighbor && !pagePrefetch)
        return;
    const int page = neighbor ? 0 : pagePrefetch ? _displayedIndex + 1 : _requestedIndex;
    if (!_thread.isRunning()) {
        _worker->moveToThread(&_thread);
        _thread.start();
    }
    const QSize fullSize = detail ? _detailSize : QSize{};
    const QRect clip = detail ? _clip : QRect{};
    _pending = false;
    if (detail)
        _detailPending = false;
    if (pagePrefetch)
        _prefetchPending = false;
    _busy = true;
    _runningFrameKey = cacheKey(source, page, _viewport, fullSize, clip);
    const quint64 generation = _generation;
    const auto cancellation = QSharedPointer<ImageSourceCancellation>::create();
    _cancellation = cancellation;
    const bool prefetch = pagePrefetch || neighbor;
    QMetaObject::invokeMethod(_worker, [this, worker = _worker, generation, cancellation,
        source = std::move(source), viewport = _viewport, page, fullSize, clip, prefetch, neighbor] {
        auto result = worker->render(source, page, viewport, fullSize, clip, prefetch, neighbor,
                                     cancellation);
        QMetaObject::invokeMethod(this, [this, generation, result = std::move(result), prefetch]() mutable {
            complete(generation, std::move(result), prefetch);
        });
    });
}

void GalleryPageController::complete(quint64 generation, Result result, bool prefetch)
{
    _busy = false;
    _runningFrameKey.clear();
    if (generation != _generation || _file.key.isEmpty() || prefetch) {
        dispatch();
        return;
    }
    if (!result.clip.isEmpty()) {
        if (_clip == result.clip && _detailSize == result.fullSize && _displayedIndex == result.index
            && !result.page.pixels.isNull()) {
            applyDetail(result.page.pixels, result.fullSize, result.clip);
            emit changed();
        }
        dispatch();
        return;
    }
    if (result.viewport.bounds != _viewport.bounds
        || (result.scalable && result.viewport != _viewport)) {
        _pending = true;
        dispatch();
        return;
    }
    applyFrame(result);
    dispatch();
}

void GalleryPageController::applyDetail(const QImage &image, QSize fullSize, QRect clip)
{
    if (image.isNull()) {
        _store->remove(_detailId);
        _detailId.clear();
        _detailSource.clear();
        _detailRect = {};
        return;
    }
    _detailRect = QRectF(qreal(clip.x()) / fullSize.width(), qreal(clip.y()) / fullSize.height(),
                        qreal(clip.width()) / fullSize.width(), qreal(clip.height()) / fullSize.height());
    _detailSource = publish(image, _detailId);
}

void GalleryPageController::applyFrame(const Result &result)
{
    const bool firstFrame = _source.isEmpty();
    const bool pageChanged = result.index != _displayedIndex;
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
        if (firstFrame || pageChanged || !result.detail.isNull()) {
            _detailPending = false;
            _clip = result.detailClip;
            _detailSize = result.detailSize;
            applyDetail(result.detail, result.detailSize, result.detailClip);
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
