#ifndef IMAGEPROBERUNNER_H
#define IMAGEPROBERUNNER_H

#include "DecodeManager.h"
#include "ImageProbe.h"

class ImageProbeRunner final : public Runner {
    Q_OBJECT

public:
    ImageProbeRunner(
        ZoinGallery::ImageProbeRequest request,
        QSharedPointer<ZoinGallery::ImageSourceProvider> provider);

    RunnerType type() override { return RunnerType::ImageProbe; }
    void run() override;
    QString path() const override {
        return _request.source.runtimeIdentity();
    }
    bool isHighPriority() const override {
        return _request.highPriority;
    }
    QString requestNamespace() const override {
        return _request.requestNamespace;
    }
    QSharedPointer<ZoinGallery::ImageSourceCancellation>
    sourceCancellation() const override {
        return _cancellation;
    }

signals:
    void imageProbeReady(const ZoinGallery::ImageProbeResult &result);

private:
    friend class DecodeManager;

    ZoinGallery::ImageProbeRequest _request;
    QSharedPointer<ZoinGallery::ImageSourceProvider> _provider;
    QSharedPointer<ZoinGallery::ImageSourceCancellation> _cancellation;
};

#endif // IMAGEPROBERUNNER_H
