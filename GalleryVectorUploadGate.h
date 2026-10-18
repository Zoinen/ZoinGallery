#pragma once
#include <QQuickItem>
#include <QSharedPointer>

struct GalleryVectorUploadState;

class GalleryVectorUploadGate : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QString sourceKey READ sourceKey WRITE setSourceKey NOTIFY sourceKeyChanged)
    Q_PROPERTY(bool cpuComplete READ cpuComplete WRITE setCpuComplete NOTIFY cpuCompleteChanged)
    Q_PROPERTY(bool present READ present WRITE setPresent NOTIFY presentChanged)
    Q_PROPERTY(int uploadPriority READ uploadPriority WRITE setUploadPriority NOTIFY uploadPriorityChanged)
    Q_PROPERTY(bool nativeKnown READ nativeKnown NOTIFY nativeStateChanged)
    Q_PROPERTY(bool nativeAvailable READ nativeAvailable NOTIFY nativeStateChanged)
    Q_PROPERTY(bool nativeReady READ nativeReady NOTIFY nativeStateChanged)
    Q_PROPERTY(bool nativeFailed READ nativeFailed NOTIFY nativeStateChanged)
public:
    explicit GalleryVectorUploadGate(QQuickItem *parent = nullptr);
    ~GalleryVectorUploadGate() override;
    QString sourceKey() const { return _sourceKey; }
    void setSourceKey(const QString &value);
    bool cpuComplete() const;
    void setCpuComplete(bool value);
    bool present() const;
    void setPresent(bool value);
    int uploadPriority() const;
    void setUploadPriority(int value);
    bool nativeKnown() const { return _known; }
    bool nativeAvailable() const { return _available; }
    bool nativeReady() const { return _ready; }
    bool nativeFailed() const { return _failed; }
signals:
    void sourceKeyChanged();
    void cpuCompleteChanged();
    void presentChanged();
    void uploadPriorityChanged();
    void nativeStateChanged();
protected:
    QSGNode *updatePaintNode(QSGNode *, UpdatePaintNodeData *) override;
private:
    void watchWindow();
    void watchProgress();
    void publishNativeState();
    void resetNativeState();
    QSharedPointer<GalleryVectorUploadState> _state;
    QString _sourceKey;
    bool _known = true, _available = false, _ready = false, _failed = false;
    QMetaObject::Connection _sync, _rendered, _published, _invalidated;
};
