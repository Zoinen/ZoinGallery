#ifndef IMAGEDECODERUNNER_H
#define IMAGEDECODERUNNER_H

#include "DecodeManager.h"

class ImageDecodeRunner : public Runner {
    Q_OBJECT

public:
    ImageDecodeRunner(const ImageData &request);

    RunnerType type() override { return RunnerType::ImageDecode; }
    void run() override;

    bool isViewerRequest() const;

signals:
    void imageReady(const ImageDecodeRequest &request, const QImage &image);

private:
    ImageData _imageData;
};

#endif // IMAGEDECODERUNNER_H
