#ifndef IMAGEINFOREADRUNNER_H
#define IMAGEINFOREADRUNNER_H

#include "DecodeManager.h"

class ImageInfoReadRunner : public Runner {
    Q_OBJECT

public:
    ImageInfoReadRunner(const QString &path, bool isLast, bool isFromEmbeddedView, bool isFromScanner = false,
                        int directOpenGeneration = 0,
                        bool highPriority = false);

    RunnerType type() override { return RunnerType::ImageInfoRead; }
    void run() override;

    bool isEmbeddedRequest() const;
    bool isHighPriority() const override { return _highPriority; }

signals:
    void imageInfoReady(const ImageInfo &result);

private:
    friend class DecodeManager;

    QString _path;
    bool _isLast;
    bool _isFromEmbeddedView;
    bool _isFromScanner;
    int _directOpenGeneration;
    bool _highPriority;
};

#endif // IMAGEINFOREADRUNNER_H
