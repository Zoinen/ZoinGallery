#ifndef CACHEIMAGERUNNERS_H
#define CACHEIMAGERUNNERS_H

#include "DecodeManager.h"

class CachedImageInfoRunner : public Runner {
    Q_OBJECT

public:
    CachedImageInfoRunner(const QStringList &imagePaths, bool isFromEmbeddedView);

    RunnerType type() override { return RunnerType::CachedImageInfo; }
    void run() override;

signals:
    void cachedImageInfoRetrieved(const QList<ImageInfo> &result, const QStringList &notFound, bool isFromEmbeddedView,
                                  const QString &lastPath);

private:
    friend class DecodeManager;

    QStringList _imagePaths;
    bool _isFromEmbeddedView;
};


class CachedImageRetrieveRunner : public Runner {
    Q_OBJECT

public:
    CachedImageRetrieveRunner(const ImageDecodeRequest &request);

    RunnerType type() override { return RunnerType::CachedImageRetrieve; }
    void run() override;

signals:
    void cachedThumbnailRetrieved(const ImageDecodeRequest &request, const QImage &image, bool isFromCache);

private:
    friend class DecodeManager;

    ImageDecodeRequest _request;
};


class CachedImageStoreRunner : public Runner {
    Q_OBJECT

public:
    CachedImageStoreRunner(const ImageInfo &imageInfo, const QImage &image);

    RunnerType type() override { return RunnerType::CachedImageStore; }
    void run() override;

private:
    friend class DecodeManager;

    ImageInfo _imageInfo;
    QImage _image;
};

#endif // CACHEIMAGERUNNERS_H
