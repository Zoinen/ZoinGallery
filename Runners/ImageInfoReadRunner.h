#ifndef IMAGEINFOREADRUNNER_H
#define IMAGEINFOREADRUNNER_H

#include "DecodeManager.h"

class ImageInfoReadRunner : public Runner {
    Q_OBJECT

public:
    ImageInfoReadRunner(const QString &path, bool isLast, bool isFromEmbeddedView, bool isFromScanner = false,
                        int directOpenGeneration = 0,
                        bool highPriority = false,
                        QString requestNamespace = QString(),
                        QString sourceVersionToken = QString(),
                        ZoinGallery::ImageSourceDescriptor source = {},
                        QSharedPointer<ZoinGallery::ImageSourceProvider> provider = {},
                        bool readDerivedMetadataCache = true,
                        bool writeDerivedMetadataCache = true);

    RunnerType type() override { return RunnerType::ImageInfoRead; }
    void run() override;
    QString path() const override {
        return _source.isValid() ? _source.runtimeIdentity() : _path;
    }

    bool isEmbeddedRequest() const;
    bool isHighPriority() const override { return _highPriority; }
    QString requestNamespace() const override { return _requestNamespace; }
    QSharedPointer<ZoinGallery::ImageSourceCancellation>
    sourceCancellation() const override { return _cancellation; }

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
    QString _requestNamespace;
    QString _sourceVersionToken;
    ZoinGallery::ImageSourceDescriptor _source;
    QSharedPointer<ZoinGallery::ImageSourceProvider> _provider;
    QSharedPointer<ZoinGallery::ImageSourceCancellation> _cancellation;
    bool _readDerivedMetadataCache = true;
    bool _writeDerivedMetadataCache = true;
};

#endif // IMAGEINFOREADRUNNER_H
