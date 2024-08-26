#ifndef IMAGEDECODERUNNER_H
#define IMAGEDECODERUNNER_H

#include "DecodeManager.h"

class ImageDecodeRunner : public Runner {
    Q_OBJECT

public:
    ImageDecodeRunner(const ImageData &request);

    RunnerType type() override { return RunnerType::ImageDecode; }
    void run() override;

    QString path() const override;
    bool isViewerRequest() const override;

signals:
    void imageReady(const ImageDecodeRequest &request, const QImage &image, const DecodedImageInfo &decodedInfo);
    void storeInCache(const ImageDecodeRequest &request, const QByteArray &imageData);

private:
    friend class DecodeManager;

    ImageData _imageData;
};

#endif // IMAGEDECODERUNNER_H
