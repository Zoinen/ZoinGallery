#ifndef IMAGEFILE_H
#define IMAGEFILE_H

#include <QString>
#include <QSize>
#include <QImage>
#include <QMetaType>
#include <QSharedPointer>

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
    QString fileName;
    QImage image;
    QString imageId;
    QSize fullSize;
    bool isFolder;
    bool isImage;
    QString iconPath;
    int index;

    ImageFile() : isFolder(false), index(-1) {}
};

struct ThumbnailReadRequest {
    QString sourcePath;
    QSize targetSize;
    bool viewerRequest;
    int queueId;

    ThumbnailReadRequest(const QString &path = QString(), const QSize &size = QSize(), bool viewerRequest_ = false)
        : sourcePath(path), targetSize(size), viewerRequest(viewerRequest_), queueId(-1) {}
};

struct ThumbnailReadResult {
    ThumbnailReadRequest request;
    QSize fullSize;
    QSize thumbnailSize;
    ExifOrientation orientation;
    QString mimeType;
    QByteArray thumbnailData;
    QByteArray fullImageData;

    ThumbnailReadResult() : orientation(ExifOrientation::Horizontal) {}
};

Q_DECLARE_METATYPE(ImageFile *)

#endif // IMAGEFILE_H

