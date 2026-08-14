#ifndef PROVIDERIMAGESTORE_H
#define PROVIDERIMAGESTORE_H

#include <QHash>
#include <QImage>
#include <QReadWriteLock>
#include <QString>
#include <QStringList>

class ProviderImageStore {
public:
    QImage snapshot(const QString &imageId) const;
    bool contains(const QString &imageId) const;
    qsizetype imageCount() const;
    qint64 retainedBytes() const;
    void publish(const QString &imageId, const QImage &image);
    void remove(const QString &imageId);
    void remove(const QStringList &imageIds);

private:
    mutable QReadWriteLock _lock;
    QHash<QString, QImage> _images;
    qint64 _retainedBytes = 0;
};

#endif // PROVIDERIMAGESTORE_H
