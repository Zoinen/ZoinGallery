#ifndef VIDEOTHUMBNAILRUNNER_H
#define VIDEOTHUMBNAILRUNNER_H

#include "DecodeManager.h"
#include "PersistentDerivedImageCache.h"

#include <QSharedPointer>
#include <QSize>
#include <QVector>
#include <QMediaMetaData>

class VideoThumbnailRunner final : public Runner {
    Q_OBJECT

public:
    VideoThumbnailRunner(
        const ImageDecodeRequest &request,
        QSharedPointer<ZoinGallery::ImageSourceProvider> provider = {});

    // Keep the four storyboard samples away from both endpoints.  The
    // helper is exposed so the temporal policy remains unit-testable without
    // opening a media backend.
    static QVector<qint64> thumbnailPositions(qint64 duration);
    static QVector<qint64> capturePositions(qint64 duration);
    static QSize posterSizeFor(const QSize &frameSize);
    static QVariantMap fileFieldsForMetadata(const QMediaMetaData &metadata,
                                            qint64 durationMs, qint64 fileSize);

    RunnerType type() override { return RunnerType::ImageRead; }
    void run() override;

    QString path() const override;
    bool isViewerRequest() const override;
    bool isPanelThumbnailRequest() const override {
        return _request.panelThumbnailRequest;
    }
    bool isHighPriority() const override { return _request.highPriority; }
    QString requestNamespace() const override {
        return _request.requestNamespace;
    }
    quint64 viewerGeneration() const override {
        return _request.viewerGeneration;
    }
    int viewerPriorityOrdinal() const override {
        return _request.viewerPriorityOrdinal;
    }
    QSharedPointer<ZoinGallery::ImageSourceCancellation>
    sourceCancellation() const override { return _cancellation; }
    const ImageDecodeRequest &request() const { return _request; }

signals:
    void imageInfoReady(const ImageInfo &info);
    void imageReady(const ImageDecodeRequest &request, const QImage &image,
                    const DecodedImageInfo &decodedInfo);
    void imageReadFailed(const ImageDecodeRequest &request);
    void storeInCache(const ImageDecodeRequest &request,
                      const QByteArray &imageData);

private:
    ImageDecodeRequest _request;
    QSharedPointer<ZoinGallery::ImageSourceProvider> _provider;
    QSharedPointer<ZoinGallery::ImageSourceCancellation> _cancellation;
    PersistentDerivedImageCache::LookupGate _derivedLookupGate;
};

#endif // VIDEOTHUMBNAILRUNNER_H
