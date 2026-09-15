#pragma once
#include <QObject>
#include <QThreadPool>
#include <QVariantList>
#include <QVariantMap>

class DecodeManager;
class QWindow;
namespace ZoinGallery {
class GalleryPreferences final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap values READ values NOTIFY changed)
    Q_PROPERTY(QVariantList decoders READ decoders CONSTANT)
    Q_PROPERTY(qint64 diskBytes READ diskBytes NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString targetColorSpace READ targetColorSpace NOTIFY changed)
    Q_PROPERTY(bool animateResizing READ animateResizing NOTIFY changed)
public:
    explicit GalleryPreferences(DecodeManager *decoder, bool persistent, QObject *parent = nullptr);
    QVariantMap values() const;
    QVariantList decoders() const;
    qint64 diskBytes() const { return _diskBytes; }
    bool busy() const { return _busy; }
    QString error() const { return _error; }
    QString targetColorSpace() const;
    bool animateResizing() const;
    Q_INVOKABLE bool apply(const QVariantMap &values);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void clearCache();
    Q_INVOKABLE void updateDisplay(QObject *window);
signals:
    void changed();
    void cacheCleared();
    void clearSnapshotsRequested();
    void pixelsInvalidated();
private:
    void runCacheOperation(bool clear, bool maintenance);
    DecodeManager *_decoder;
    QVariantMap _values;
    QVariantList _decoders;
    qint64 _diskBytes = 0;
    bool _busy = false;
    bool _refreshing = false;
    QString _error;
    // Its destructor joins cache I/O before the runtime's owner is destroyed.
    QThreadPool _cachePool;
};
}
