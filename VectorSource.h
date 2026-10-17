#pragma once

#include "ImageFile.h"
#include <QUrl>
#include <QTemporaryDir>
#include <QQmlComponent>
#include <QPointer>

// Immutable source, not a decoded image. Keeping this handle pins VFS backing
// storage until the last panel/viewer consumer releases the revision.
struct VectorSource {
    QUrl url;
    QSize size;
    QUrl componentUrl;
    QSharedPointer<QTemporaryDir> preparedFiles;
    QSharedPointer<ZoinGallery::ImageSourceLease> lease;
};
using VectorSourcePtr = QSharedPointer<VectorSource>;
Q_DECLARE_METATYPE(VectorSourcePtr)

bool isVectorSource(const ImageInfo &info);
QString vectorRevisionKey(const ImageInfo &info);
VectorSourcePtr acquireVectorSource(const ImageInfo &info,
    const QSharedPointer<ZoinGallery::ImageSourceLease> &lease,
    const QSharedPointer<ZoinGallery::ImageSourceCancellation> &cancellation = {},
    int preparationPriority = 1000);

// A QML surface owns this handle independently of catalog eviction/navigation.
class VectorSourcePin : public QObject {
    Q_OBJECT
    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(bool prepareComponent READ prepareComponent WRITE setPrepareComponent NOTIFY preparedChanged)
    Q_PROPERTY(QQmlComponent *component READ component NOTIFY preparedChanged)
    Q_PROPERTY(QSize sourceSize READ sourceSize NOTIFY preparedChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY preparedChanged)
    Q_PROPERTY(bool failed READ failed NOTIFY preparedChanged)
public:
    explicit VectorSourcePin(QObject *parent = nullptr) : QObject(parent) {}
    ~VectorSourcePin() override;
    QUrl source() const { return _source; }
    void setSource(const QUrl &source);
    bool prepareComponent() const { return _prepareComponent; }
    void setPrepareComponent(bool prepare);
    QQmlComponent *component() const { return _component; }
    QSize sourceSize() const { return _pin ? _pin->size : QSize(); }
    bool loading() const { return _loading; }
    bool failed() const { return _failed; }
signals:
    void sourceChanged();
    void preparedChanged();
private:
    void loadComponent();
    QUrl _source;
    VectorSourcePtr _pin;
    QPointer<QQmlComponent> _component;
    quint64 _generation = 0;
    bool _prepareComponent = false;
    bool _loading = false;
    bool _failed = false;
};
