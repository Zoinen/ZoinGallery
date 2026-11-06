#ifndef IMAGEDECODERUNNER_H
#define IMAGEDECODERUNNER_H

#include "DecodeManager.h"

class ImageDecodeRunner : public Runner {
    Q_OBJECT

public:
    ImageDecodeRunner(const ImageData &request, QThreadPool *resizePool = nullptr);

    RunnerType type() override { return RunnerType::ImageDecode; }
    void run() override;

    QString path() const override;
    bool isViewerRequest() const override;
    bool isViewerFitRequest() const override { return _imageData.request.fitToViewerRequest; }
    bool isPanelThumbnailRequest() const override {
        return _imageData.request.panelThumbnailRequest;
    }
    bool isHighPriority() const override {
        return _imageData.request.highPriority;
    }
    QString requestNamespace() const override {
        return _imageData.request.requestNamespace;
    }
    quint64 viewerGeneration() const override {
        return _imageData.request.viewerGeneration;
    }
    int viewerPriorityOrdinal() const override {
        return _imageData.request.viewerPriorityOrdinal;
    }
    QSharedPointer<ZoinGallery::ImageSourceCancellation>
    sourceCancellation() const override { return _resizeCancellation; }

signals:
    void imageReady(const ImageDecodeRequest &request, const QImage &image, const DecodedImageInfo &decodedInfo);
    void storeInCache(const ImageDecodeRequest &request, const QByteArray &imageData);
    void storeViewerFit(const ImageDecodeRequest &request, const QImage &image);

private:
    friend class DecodeManager;

    ImageData _imageData;
    QThreadPool *const _resizePool;
    QSharedPointer<ZoinGallery::ImageSourceCancellation> _resizeCancellation;
    std::atomic_bool _foregroundFit{false};
};

#endif // IMAGEDECODERUNNER_H
