#ifndef IMAGEREADRUNNER_H
#define IMAGEREADRUNNER_H

#include "DecodeManager.h"

class ImageReadRunner : public Runner {
    Q_OBJECT

public:
    ImageReadRunner(const ImageDecodeRequest &request);

    RunnerType type() override { return RunnerType::ImageRead; }
    void run() override;

    QString path() const override;
    bool isViewerRequest() const override;

signals:
    void imageReadReady(const ImageData &result);

private:
    friend class DecodeManager;

    ImageDecodeRequest _request;
};

#endif // IMAGEREADRUNNER_H
