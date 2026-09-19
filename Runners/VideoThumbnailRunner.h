#ifndef VIDEOTHUMBNAILRUNNER_H
#define VIDEOTHUMBNAILRUNNER_H

#include "DecodeManager.h"
#include "PersistentDerivedImageCache.h"

#include <QSharedPointer>

class VideoThumbnailRunner final : public Runner {
    Q_OBJECT

public:
    VideoThumbnailRunner(
        const ImageDecodeRequest &request,
        QSharedPointer<ZoinGallery::ImageSourceProvider> provider = {});

    RunnerType type() override { return RunnerType::ImageRead; }
    void run() override;

    QString path() const override;
    bool isViewerRequest() const override;
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
