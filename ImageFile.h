#ifndef IMAGEFILE_H
#define IMAGEFILE_H

#include <QString>
#include <QSize>
#include <QImage>
#include <QMetaType>
#include <QSharedPointer>

struct ImageFile {
    QString path;
    QImage image;
    QString imageId;
    QSize fullSize;
    bool isFolder;
    int index;

    ImageFile() : isFolder(false), index(-1) {}
};

struct ThumbnailRequest {
    QString sourcePath;
    QSize targetSize;
    bool requested;

    ThumbnailRequest() : requested(false) {}
    ThumbnailRequest(const QString &path) : sourcePath(path), requested(false) {}
    ThumbnailRequest(const QString &path, const QSize &size) : sourcePath(path), targetSize(size), requested(false) {}
};

Q_DECLARE_METATYPE(ImageFile *)
Q_DECLARE_METATYPE(ThumbnailRequest *)

#endif // IMAGEFILE_H

