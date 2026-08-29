#include "ImageProbeRunner.h"

#include "ThumbnailLoader.h"
#include "src/embed/EmbeddedImageProbe.h"

#include <ZoinGallery/MediaTimingTrace.h>

#include <QBuffer>
#include <QImageReader>

#include <utility>

namespace {

constexpr qsizetype MaxEncodedPreviewBytes = 4 * 1024 * 1024;
// Pass 1 is a latency placeholder, never a native-quality artifact. Keep its
// worst-case decoded allocation small even when a tiny compressed payload
// advertises pathological dimensions.
constexpr qint64 MaxPreviewPixels = 16LL * 1024LL * 1024LL;
constexpr int MaxPreviewEdge = 16384;

bool safePreviewSize(const QSize &size)
{
    return size.isValid() && size.width() > 0 && size.height() > 0 &&
        size.width() <= MaxPreviewEdge && size.height() <= MaxPreviewEdge &&
        static_cast<qint64>(size.width()) <=
            MaxPreviewPixels / size.height();
}

ZoinGallery::ImageSourceProbeStatus sourceStatus(
    ZoinGallery::EmbeddedProbeResult::Outcome outcome)
{
    using ProbeOutcome = ZoinGallery::EmbeddedProbeResult::Outcome;
    using SourceStatus = ZoinGallery::ImageSourceProbeStatus;
    switch (outcome) {
    case ProbeOutcome::Found:
        return SourceStatus::Found;
    case ProbeOutcome::NotFound:
        return SourceStatus::NotFound;
    case ProbeOutcome::ReadFailed:
        return SourceStatus::Failed;
    case ProbeOutcome::Unsupported:
        return SourceStatus::Unsupported;
    }
    return SourceStatus::Unsupported;
}

} // namespace

ImageProbeRunner::ImageProbeRunner(
    ZoinGallery::ImageProbeRequest request,
    QSharedPointer<ZoinGallery::ImageSourceProvider> provider)
    : _request(std::move(request)),
      _provider(std::move(provider)),
      _cancellation(
          QSharedPointer<ZoinGallery::ImageSourceCancellation>::create())
{
}

void ImageProbeRunner::run()
{
    QVariantMap timingFields =
        ZoinGallery::MediaTimingTrace::sourceFields(_request.source);
    timingFields.insert(QStringLiteral("highPriority"),
                        _request.highPriority);
    timingFields.insert(QStringLiteral("requestNamespace"),
                        _request.requestNamespace);
    ZoinGallery::MediaTimingTrace::Span timingSpan(
        QStringLiteral("qt.gallery.probe"), timingFields);
    ZoinGallery::ImageProbeResult result;
    result.request = _request;
    if (!_provider || !_request.source.isValid()) {
        result.status = ZoinGallery::ImageSourceProbeStatus::Failed;
        result.diagnostic = QStringLiteral("source provider is unavailable");
        timingSpan.set(QStringLiteral("outcome"),
                       QStringLiteral("provider-unavailable"));
        emit imageProbeReady(result);
        emit finished(this);
        return;
    }

    const ZoinGallery::ImageSourceProbeResult providerProbe =
        _provider->probeEmbedded(_request.source, _cancellation);
    if (_cancellation->isCanceled()) {
        timingSpan.set(QStringLiteral("outcome"), QStringLiteral("cancelled"));
        emit finished(this);
        return;
    }

    QByteArray encoded;
    if (providerProbe.status !=
        ZoinGallery::ImageSourceProbeStatus::Unsupported) {
        result.status = providerProbe.status;
        result.diagnostic = providerProbe.errorString;
        encoded = providerProbe.encodedData;
    }
    else {
        const ZoinGallery::EmbeddedProbeResult probe =
            ZoinGallery::probeEmbeddedImage(
                _request.source.displayName,
                [this](qint64 offset, qsizetype length) {
                    ZoinGallery::EmbeddedRangeResult range;
                    if (_cancellation->isCanceled()) {
                        range.error = QStringLiteral("canceled");
                        return range;
                    }
                    const ZoinGallery::ImageSourceReadResult read =
                        _provider->readRange(
                            _request.source, offset, length, _cancellation);
                    range.bytes = read.data;
                    range.endOfFile = read.endOfFile;
                    range.error = read.errorString;
                    return range;
                }, ZoinGallery::EmbeddedProbeLimits{},
                _request.source.mimeType);
        if (_cancellation->isCanceled()) {
            timingSpan.set(QStringLiteral("outcome"),
                           QStringLiteral("cancelled"));
            emit finished(this);
            return;
        }
        result.status = sourceStatus(probe.outcome);
        result.sourceSize = probe.sourceSize;
        result.orientation = probe.orientation;
        result.sourceBytesRead = probe.sourceBytesRead;
        result.rangeRequests = probe.rangeRequests;
        result.diagnostic = probe.diagnostic;
        encoded = probe.encodedPreview;
    }

    if (result.status == ZoinGallery::ImageSourceProbeStatus::Found) {
        if (encoded.isEmpty() || encoded.size() > MaxEncodedPreviewBytes) {
            result.status = ZoinGallery::ImageSourceProbeStatus::NotFound;
            result.diagnostic =
                QStringLiteral("embedded preview exceeds the byte budget");
        }
        else {
            QBuffer buffer(&encoded);
            buffer.open(QIODevice::ReadOnly);
            QImageReader reader(&buffer);
            const QSize encodedSize = reader.size();
            if (!safePreviewSize(encodedSize)) {
                result.status =
                    ZoinGallery::ImageSourceProbeStatus::NotFound;
                result.diagnostic = QStringLiteral(
                    "embedded preview dimensions exceed the pixel budget");
            }
            else {
                ZoinGallery::MediaTimingTrace::Span decodeSpan(
                    QStringLiteral("qt.gallery.probe.preview_decode"),
                    timingFields);
                result.preview = reader.read();
                decodeSpan.set(QStringLiteral("encodedBytes"), encoded.size());
                decodeSpan.set(QStringLiteral("previewWidth"),
                               result.preview.width());
                decodeSpan.set(QStringLiteral("previewHeight"),
                               result.preview.height());
                decodeSpan.set(QStringLiteral("ok"),
                               !result.preview.isNull());
                if (result.preview.isNull()) {
                    result.status =
                        ZoinGallery::ImageSourceProbeStatus::NotFound;
                    result.diagnostic = QStringLiteral(
                        "embedded preview cannot be decoded");
                }
            }
        }
        if (!result.preview.isNull() &&
            result.orientation >= ExifOrientation::Horizontal &&
                 result.orientation <= ExifOrientation::Rotate270CW) {
            result.preview = ThumbnailLoader::rotateAndFlip(
                result.preview,
                static_cast<ExifOrientation>(result.orientation));
        }
    }

    timingSpan.set(QStringLiteral("status"),
                   static_cast<int>(result.status));
    timingSpan.set(QStringLiteral("found"), result.found());
    timingSpan.set(QStringLiteral("sourceBytesRead"),
                   result.sourceBytesRead);
    timingSpan.set(QStringLiteral("rangeRequests"), result.rangeRequests);
    timingSpan.set(QStringLiteral("encodedBytes"), encoded.size());
    timingSpan.set(QStringLiteral("previewWidth"), result.preview.width());
    timingSpan.set(QStringLiteral("previewHeight"), result.preview.height());
    timingSpan.set(QStringLiteral("diagnostic"), result.diagnostic);
    emit imageProbeReady(result);
    emit finished(this);
}
