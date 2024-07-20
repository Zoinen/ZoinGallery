#ifndef IMAGEFILE_H
#define IMAGEFILE_H

#include <QString>
#include <QSize>
#include <QImage>
#include <QMetaType>
#include <QDir>
#include <QSharedPointer>

#include <memory>

enum ExifOrientation {
    Horizontal = 1,
    MirrorHorizontal = 2,
    Rotate180 = 3,
    MirrorVertical = 4,
    MirrorHorizontalAndRotate270CW = 5,
    Rotate90CW = 6,
    MirrorHorizontalAndRotate90CW = 7,
    Rotate270CW = 8
};

inline QSize rotateToOrientation(QSize size, ExifOrientation orientation) {
    if (orientation == ExifOrientation::Rotate270CW ||
            orientation == ExifOrientation::Rotate90CW ||
            orientation == ExifOrientation::MirrorHorizontalAndRotate270CW ||
            orientation == ExifOrientation::MirrorHorizontalAndRotate90CW) {
        return QSize(size.height(), size.width());
    }
    return size;
}


struct ImageInfo {
    QString path;
    QDateTime lastModified;
    QSize imageSize;
    QVariantMap exif;

    bool isLast = false;
    bool isFromEmbeddedView = false;
    bool isCached = false;
};

struct ImageDecodeRequest {
    ImageInfo info;

    QSize targetSize;
    bool viewerRequest = false;
    bool checkCache = false;
};

struct ImageData {
    const ImageDecodeRequest request;

    QByteArray data;
    QString mimeType;
    ExifOrientation orientation = ExifOrientation::Horizontal;

    std::shared_ptr<char> previewData;
    int64_t previewDataSize = 0;
    QString previewMimeType;
    ExifOrientation previewOrientation = ExifOrientation::Horizontal;

    QSize imageSize;

    ImageData(const ImageDecodeRequest &request_) : request(request_) {}
};


struct ImageFile {
    QString folderPath;
    QString fileName;
    QDateTime lastModified;
    QImage image;
    QString imageId;
    QSize fullSize;
    bool isFolder = false;
    bool isImage = false;
    bool isFolderView = false;
    bool isCacheAvailable = false;
    bool isCachedThumbnail = false;
    QString iconPath;
    int index = -1;
    QString nestingInfo;
    QVariantMap exif;

    QList<ImageFile *> subfiles;
    ImageFile *parent = nullptr;

    QString fullPath() const {
        return folderPath.isEmpty() ? fileName : QDir::toNativeSeparators(QDir(folderPath).filePath(fileName));
    }

    bool folderView() const {
        return subfiles.size() != 0 || isFolderView;
    }

    ImageInfo toImageInfo() const {
        return {fullPath(), lastModified, fullSize, exif, false, false, isCachedThumbnail};
    }

    QVariantList exifList() const;
};
Q_DECLARE_METATYPE(ImageFile *)

struct FileInfo {
    QString name;
    QDateTime lastModified;
};

struct FolderInfo {
    QString path;
    QList<FileInfo> subfiles;
};

const QSize CACHE_IMAGE_RESOLUTION(1920, 1080);

inline QSize expandToCacheImageResolution(QSize targetSize) {
    if (targetSize.width() < CACHE_IMAGE_RESOLUTION.width() ||
        targetSize.height() < CACHE_IMAGE_RESOLUTION.height()) {
        targetSize = targetSize.scaled(CACHE_IMAGE_RESOLUTION, Qt::KeepAspectRatio);
    }
    return targetSize;
}

#endif // IMAGEFILE_H

