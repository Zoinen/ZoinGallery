#ifndef CACHEIMAGERUNNERS_H
#define CACHEIMAGERUNNERS_H

#include "DecodeManager.h"

class CachedImageInfoRunner : public Runner {
    Q_OBJECT

public:
    CachedImageInfoRunner(const QStringList &imagePaths, bool isFromEmbeddedView,
                          bool validateSource, int directOpenGeneration = 0,
                          bool highPriority = false);

    RunnerType type() override { return RunnerType::CachedImageInfo; }
    void run() override;
    bool isHighPriority() const override { return _highPriority; }

signals:
    void cachedImageInfoRetrieved(const QList<ImageInfo> &result, const QStringList &notFound, bool isFromEmbeddedView,
                                  const QString &lastPath, int directOpenGeneration,
                                  bool highPriority);

private:
    friend class DecodeManager;

    QStringList _imagePaths;
    bool _isFromEmbeddedView;
    bool _validateSource;
    int _directOpenGeneration;
    bool _highPriority;
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

private:
    friend class DecodeManager;

    ImageInfo _imageInfo;
    QByteArray _imageData;
};

#endif // CACHEIMAGERUNNERS_H
