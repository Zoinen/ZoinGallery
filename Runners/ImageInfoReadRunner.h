#ifndef IMAGEINFOREADRUNNER_H
#define IMAGEINFOREADRUNNER_H

#include "DecodeManager.h"

class ImageInfoReadRunner : public Runner {
    Q_OBJECT

public:
    ImageInfoReadRunner(const QString &path, bool isLast, bool isFromEmbeddedView, bool isFromScanner = false,
                        int directOpenGeneration = 0);

    RunnerType type() override { return RunnerType::ImageInfoRead; }
    void run() override;

    bool isEmbeddedRequest() const;

signals:
    void imageInfoReady(const ImageInfo &result);

private:
    friend class DecodeManager;

    QString _path;
    bool _isLast;
    bool _isFromEmbeddedView;
    bool _isFromScanner;
    int _directOpenGeneration;
};

#endif // IMAGEINFOREADRUNNER_H
