#pragma once

#include "Decoders/PagedImageDocument.h"

#include <ZoinGallery/ImageSourceProvider.h>

#include <QObject>
#include <QThread>
#include <QTimer>
#include <QUrl>

class ProviderImageStore;

namespace ZoinGallery {

class GalleryPageController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(int index READ index NOTIFY changed)
    Q_PROPERTY(bool animated READ animated NOTIFY changed)
    Q_PROPERTY(bool playing READ playing NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool scalable READ scalable NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QUrl source READ source NOTIFY changed)
    Q_PROPERTY(QSizeF size READ size NOTIFY changed)
    Q_PROPERTY(QUrl detailSource READ detailSource NOTIFY changed)
    Q_PROPERTY(QRectF detailRect READ detailRect NOTIFY changed)

public:
    GalleryPageController(const QString &sessionId, const QString &providerName,
                          QSharedPointer<ProviderImageStore> store,
                          QSharedPointer<ImageSourceProvider> provider,
                          qint64 cacheBudget, QObject *parent);
    ~GalleryPageController() override;

    void present(const QString &path, const QString &name, const QString &revision,
                 const ImageSourceDescriptor &descriptor, QSize bounds, bool visible);
    void clear();
    bool active() const { return _active; }
    int count() const { return _count; }
    int index() const { return _displayedIndex; }
    bool animated() const { return _animated; }
    bool playing() const { return _playing; }
    bool busy() const { return _busy && _source.isEmpty() && _error.isEmpty(); }
    bool scalable() const { return _scalable; }
    QString error() const { return _error; }
    QUrl source() const { return _source; }
    QSizeF size() const { return _size; }
    QUrl detailSource() const { return _detailSource; }
    QRectF detailRect() const { return _detailRect; }

    Q_INVOKABLE bool step(int direction);
    Q_INVOKABLE void togglePlayback();
    Q_INVOKABLE void requestDetail(const QSize &fullSize, const QRect &clip);

signals:
    void changed();

private:
    struct Result;
    class Worker;
    void dispatch();
    void complete(quint64 generation, Result result);
    void advance();
    void armTimer();
    void cancel();
    void clearImages();
    QUrl publish(const QImage &image, QString &id);

    QSharedPointer<ProviderImageStore> _store;
    QSharedPointer<ImageSourceCancellation> _cancellation;
    const QString _idPrefix;
    const QString _providerPrefix;
    QThread _thread;
    Worker *_worker;
    QTimer _timer;
    ImageSourceDescriptor _descriptor;
    QString _path;
    QString _name;
    QString _key;
    QString _error;
    QSize _bounds;
    QSize _lastBounds;
    QSize _detailSize;
    QRect _clip;
    QRectF _detailRect;
    bool _detailPending = false;
    bool _prefetchPending = false;
    bool _visible = false;
    bool _pending = false;
    bool _busy = false;
    bool _active = false;
    bool _animated = false;
    bool _playing = false;
    bool _scalable = false;
    bool _manual = false;
    bool _atEnd = false;
    int _count = 0;
    int _requestedIndex = 0;
    int _displayedIndex = 0;
    int _repeats = 0;
    int _delayMs = 0;
    int _completedLoops = 0;
    quint64 _generation = 0;
    quint64 _serial = 0;
    QString _imageId;
    QString _detailId;
    QUrl _source;
    QUrl _detailSource;
    QSizeF _size;
};

} // namespace ZoinGallery
