#pragma once

#include <QObject>
#include <QSharedPointer>
#include <QThreadPool>

namespace ZoinGallery {
// A reservation lasts until the underlying operation returns, even if its
// consumer has been destroyed or a provider ignores cancellation.
class DirectoryPreviewScheduler final : public QObject {
    Q_OBJECT
public:
    explicit DirectoryPreviewScheduler(QSharedPointer<QThreadPool> pool)
        : _pool(std::move(pool)) { _pool->setMaxThreadCount(2); }
    bool acquire() {
        if (_active == 2) return false;
        ++_active;
        return true;
    }
    void release() { Q_ASSERT(_active > 0); --_active; emit capacityAvailable(); }
    QThreadPool *pool() const { return _pool.data(); }
    int activeCount() const { return _active; }
signals:
    void capacityAvailable();
private:
    QSharedPointer<QThreadPool> _pool;
    int _active = 0;
};
}
