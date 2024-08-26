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
    ExifOrientation orientation = ExifOrientation::Horizontal;
    QVariantMap exif;

    bool isLast = false;
    bool isFromEmbeddedView = false;
    bool isCached = false;
    bool isFromScanner = false;
};

struct ImageDecodeRequest {
    ImageInfo info;

    QSize targetSize;
    bool viewerRequest = false;
    bool checkCache = false;
};

struct DecodedImageInfo {
    QString decoderUsed;
    int decodingTookTime = -1;
    QString previewUsed;
    bool isFromCache = false;
};

struct ImageData {
    const ImageDecodeRequest request;

    QByteArray data;
    QString mimeType;

    std::shared_ptr<char> previewData;
    int64_t previewDataSize = 0;
    QString previewMimeType;
    QString previewUsed;

    ImageData(const ImageDecodeRequest &request_) : request(request_) {}
};


struct ImageFile {
    QString folderPath;
    QString fileName;
    QImage image;
    QString imageId;
    QSize fullSize;
    bool isFolder = false;
    bool isImage = false;
    bool isFolderView = false;
    bool isCachedThumbnail = false;
    QString iconPath;
    int index = -1;
    QString nestingInfo;
    ImageInfo info;

    QList<ImageFile *> subfiles;
    ImageFile *parent = nullptr;

    QString fullPath() const {
        return folderPath.isEmpty() ? fileName : QDir(folderPath).filePath(fileName);
    }

    bool folderView() const {
        return subfiles.size() != 0 || isFolderView;
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

const QSize CACHE_IMAGE_RESOLUTION(1024, 767);

inline QSize expandToCacheImageResolution(QSize targetSize) {
    if (targetSize.width() < CACHE_IMAGE_RESOLUTION.width() &&
        targetSize.height() < CACHE_IMAGE_RESOLUTION.height()) {
        QSizeF sizeScaled = QSizeF(targetSize).scaled(CACHE_IMAGE_RESOLUTION, Qt::KeepAspectRatio);
        targetSize.setWidth(sizeScaled.width());
        targetSize.setHeight(sizeScaled.height() - 1); // TODO: WHAT
    }
    return targetSize;
}

bool isExtensionMatch(const QString &path, const QStringList &pattern);

#endif // IMAGEFILE_H

