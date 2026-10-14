#pragma once

#include "ImageFile.h"
#include <QUrl>

// Immutable source, not a decoded image. Keeping this handle pins VFS backing
// storage until the last panel/viewer consumer releases the revision.
struct VectorSource {
    QUrl url;
    QSize size;
    QSharedPointer<ZoinGallery::ImageSourceLease> lease;
};
using VectorSourcePtr = QSharedPointer<VectorSource>;
Q_DECLARE_METATYPE(VectorSourcePtr)

bool isVectorSource(const ImageInfo &info);
QString vectorRevisionKey(const ImageInfo &info);
VectorSourcePtr acquireVectorSource(const ImageInfo &info,
    const QSharedPointer<ZoinGallery::ImageSourceLease> &lease);

// A QML surface owns this handle independently of catalog eviction/navigation.
class VectorSourcePin : public QObject {
    Q_OBJECT
    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged)
public:
    explicit VectorSourcePin(QObject *parent = nullptr) : QObject(parent) {}
    QUrl source() const { return _source; }
    void setSource(const QUrl &source);
signals:
    void sourceChanged();
private:
    QUrl _source;
    VectorSourcePtr _pin;
};
