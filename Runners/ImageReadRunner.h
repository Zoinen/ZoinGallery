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

signals:
    void imageReadReady(const ImageData &result);
    void imageReadFailed(const ImageDecodeRequest &request);

private:
    friend class DecodeManager;

    ImageDecodeRequest _request;
};

#endif // IMAGEREADRUNNER_H
