#ifndef IMAGEFILE_H
#define IMAGEFILE_H

#include <QString>
#include <QSize>
#include <QImage>
#include <QMetaType>
#include <QSharedPointer>
#include <QDir>

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

struct ImageFile {
    QString folderPath;
    QString fileName;
    QDateTime lastModified;
    QImage image;
    QString imageId;
    QSize fullSize;
    bool isFolder;
    bool isImage;
    bool isFolderView;
    bool isCachedThumbnail;
    QString iconPath;
    int index;
    QVariantMap exif;

    QList<ImageFile *> subfiles;
    ImageFile *parent;

    ImageFile() : isFolder(false), isFolderView(false), isCachedThumbnail(false), index(-1), parent(nullptr) {}

    QString fullPath() const {
        return folderPath + QDir::separator() + fileName;
    }

    bool folderView() const {
        return subfiles.size() != 0 || isFolderView;
    }

    QVariantList exifList();
};

struct ImageReadRequest {
    QString sourcePath;
    QSize targetSize;
    bool viewerRequest;
    bool folderRequest;
    int folderRequestImageCount;
    bool higherThumbnailRequest;
    int queueId;

    ImageReadRequest(const QString &path = QString(), const QSize &size = QSize()) :
        sourcePath(path),
        targetSize(size),
        viewerRequest(false),
        folderRequest(false),
        folderRequestImageCount(0),
        higherThumbnailRequest(false),
        queueId(-1) {}

    bool operator==(const ImageReadRequest &other) const {
        return sourcePath == other.sourcePath && viewerRequest == other.viewerRequest && higherThumbnailRequest == other.higherThumbnailRequest;
    }
};

inline uint qHash(const ImageReadRequest &key, uint seed = 0) {
    return qHash(QString("%1|%2").arg(key.viewerRequest).arg(key.sourcePath)), seed;
}

struct ImageReadResult {
    ImageReadRequest request;
    QSize fullSize;
    QSize thumbnailSize;
    ExifOrientation orientation;
    QString mimeType;
    QByteArray thumbnailData;
    QByteArray fullImageData;
    QVariantMap exif;
    bool largerImageAvailable;
    bool success;

    ImageReadResult() : orientation(ExifOrientation::Horizontal), largerImageAvailable(false), success(false) {}
};

Q_DECLARE_METATYPE(ImageFile *)

#endif // IMAGEFILE_H

