#ifndef CACHEIMAGERUNNERS_H
#define CACHEIMAGERUNNERS_H

#include "DecodeManager.h"

class CachedImageInfoRunner : public Runner {
    Q_OBJECT

public:
    CachedImageInfoRunner(const QStringList &imagePaths, bool isFromEmbeddedView,
                          bool validateSource, int directOpenGeneration = 0,
                          bool highPriority = false,
                          QString requestNamespace = QString(),
                          qint64 sourceVersionToken = 0);

    RunnerType type() override { return RunnerType::CachedImageInfo; }
    void run() override;
    bool isHighPriority() const override { return _highPriority; }
    QString requestNamespace() const override { return _requestNamespace; }

signals:
    void cachedImageInfoRetrieved(const QList<ImageInfo> &result, const QStringList &notFound, bool isFromEmbeddedView,
                                  const QString &lastPath, int directOpenGeneration,
                                  bool highPriority,
                                  const QString &requestNamespace,
                                  qint64 sourceVersionToken);

private:
    friend class DecodeManager;

    QStringList _imagePaths;
    bool _isFromEmbeddedView;
    bool _validateSource;
    int _directOpenGeneration;
    bool _highPriority;
    QString _requestNamespace;
    qint64 _sourceVersionToken = 0;
};


class CachedImageRetrieveRunner : public Runner {
    Q_OBJECT

public:
    CachedImageRetrieveRunner(const ImageDecodeRequest &request, bool validateRequestVersion);

    RunnerType type() override { return RunnerType::CachedImageRetrieve; }
    void run() override;
    QString path() const override { return _request.info.path; }
    bool isViewerRequest() const override { return _request.viewerRequest; }
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
    void cachedThumbnailRetrieved(const ImageDecodeRequest &request, const QImage &image, const DecodedImageInfo &decodedInfo);

private:
    friend class DecodeManager;

    ImageDecodeRequest _request;
    bool _validateRequestVersion;
};


class CachedImageStoreRunner : public Runner {
    Q_OBJECT

public:
    CachedImageStoreRunner(const ImageInfo &imageInfo, const QByteArray &imageData);

    RunnerType type() override { return RunnerType::CachedImageStore; }
    void run() override;
    QString requestNamespace() const override {
        return _imageInfo.requestNamespace;
    }

private:
    friend class DecodeManager;

    ImageInfo _imageInfo;
    QByteArray _imageData;
};

#endif // CACHEIMAGERUNNERS_H
