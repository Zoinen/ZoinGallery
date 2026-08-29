#ifndef IMAGEREADRUNNER_H
#define IMAGEREADRUNNER_H

#include "DecodeManager.h"
#include "PersistentDerivedImageCache.h"

class ImageReadRunner : public Runner {
    Q_OBJECT

public:
    ImageReadRunner(
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

signals:
    void imageReadReady(const ImageData &result);
    void imageReadFailed(const ImageDecodeRequest &request);

private:
    friend class DecodeManager;

    ImageDecodeRequest _request;
    QSharedPointer<ZoinGallery::ImageSourceProvider> _provider;
    QSharedPointer<ZoinGallery::ImageSourceCancellation> _cancellation;
    PersistentDerivedImageCache::LookupGate _derivedLookupGate;
};

#endif // IMAGEREADRUNNER_H
