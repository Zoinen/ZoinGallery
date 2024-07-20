#ifndef IMAGEREADRUNNER_H
#define IMAGEREADRUNNER_H

#include "DecodeManager.h"

class ImageReadRunner : public Runner {
    Q_OBJECT

public:
    ImageReadRunner(const ImageDecodeRequest &request);

    RunnerType type() override { return RunnerType::ImageRead; }
    void run() override;

    bool isViewerRequest() const;

signals:
    void imageReadReady(const ImageData &result);

private:
    friend class DecodeManager;

    ImageDecodeRequest _request;
};

#endif // IMAGEREADRUNNER_H
