#ifndef IMAGEINFOREADRUNNER_H
#define IMAGEINFOREADRUNNER_H

#include "DecodeManager.h"

class ImageInfoReadRunner : public Runner {
    Q_OBJECT

public:
    ImageInfoReadRunner(const QString &path, bool isLast, bool isFromEmbeddedView);

    RunnerType type() override { return RunnerType::ImageInfoRead; }
    void run() override;

    bool isEmbeddedRequest() const;

signals:
    void imageInfoReady(const ImageInfo &result);

private:
    QString _path;
    bool _isLast;
    bool _isFromEmbeddedView;
};

#endif // IMAGEINFOREADRUNNER_H
