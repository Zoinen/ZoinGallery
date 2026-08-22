#include <ZoinGallery/ImageSourceProvider.h>

#include <QFile>
#include <QFileInfo>

#include <utility>
#include <vector>

namespace ZoinGallery {

namespace {

class LocalImageSourceLease final : public ImageSourceLease {
public:
    explicit LocalImageSourceLease(QString path)
        : _path(std::move(path)) {
    }

    QString localPath() const override {
        return _path;
    }

private:
    QString _path;
};

QString localPathFor(const ImageSourceDescriptor &source) {
    // A local descriptor uses the resource handle as its direct path. The
    // source key remains separately available for identity and caching.
    return source.resourceId;
}

} // namespace

void ImageSourceCancellation::cancel() {
    if (_canceled.exchange(true, std::memory_order_acq_rel)) {
        return;
    }

    std::vector<Callback> callbacks;
    {
        const std::lock_guard<std::mutex> locker(_callbackMutex);
        callbacks.reserve(_callbacks.size());
        for (auto &[id, callback] : _callbacks) {
            Q_UNUSED(id)
            callbacks.push_back(std::move(callback));
        }
        _callbacks.clear();
    }
    for (const Callback &callback : callbacks) {
        callback();
    }
}

quint64 ImageSourceCancellation::addCallback(Callback callback) {
    if (!callback) {
        return 0;
    }
    {
        const std::lock_guard<std::mutex> locker(_callbackMutex);
        if (!_canceled.load(std::memory_order_acquire)) {
            const quint64 callbackId = _nextCallbackId++;
            _callbacks.emplace(callbackId, std::move(callback));
            return callbackId;
        }
    }
    callback();
    return 0;
}

bool ImageSourceCancellation::removeCallback(quint64 callbackId) {
    if (callbackId == 0) {
        return false;
    }
    const std::lock_guard<std::mutex> locker(_callbackMutex);
    return _callbacks.erase(callbackId) != 0;
}

ImageSourceReadResult LocalImageSourceProvider::readRange(
    const ImageSourceDescriptor &source, qint64 offset, qint64 length,
    const QSharedPointer<ImageSourceCancellation> &cancellation) {
    if (cancellation && cancellation->isCanceled()) {
        return {.errorString = QStringLiteral("image source read canceled")};
    }
    if (!source.isValid() || offset < 0 || length < 0) {
        return {.errorString = QStringLiteral("invalid image source range")};
    }

    QFile file(localPathFor(source));
    if (!file.open(QIODevice::ReadOnly)) {
        return {.errorString = file.errorString()};
    }
    if (!file.seek(offset)) {
        return {.errorString = file.errorString()};
    }
    ImageSourceReadResult result;
    result.data = file.read(length);
    if (result.data.size() < length && file.error() != QFile::NoError) {
        result.errorString = file.errorString();
        result.data.clear();
        return result;
    }
    result.endOfFile = file.atEnd();
    if (cancellation && cancellation->isCanceled()) {
        result.data.clear();
        result.errorString = QStringLiteral("image source read canceled");
    }
    return result;
}

QSharedPointer<ImageSourceLease> LocalImageSourceProvider::materialize(
    const ImageSourceDescriptor &source,
    const QSharedPointer<ImageSourceCancellation> &cancellation) {
    if (!source.isValid() ||
        (cancellation && cancellation->isCanceled())) {
        return {};
    }
    const QString path = localPathFor(source);
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile() || !info.isReadable()) {
        return {};
    }
    return QSharedPointer<LocalImageSourceLease>::create(
        info.absoluteFilePath());
}

} // namespace ZoinGallery
