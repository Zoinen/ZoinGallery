#ifndef BATCHTHUMBNAILGENERATOR_H
#define BATCHTHUMBNAILGENERATOR_H

#include <QObject>
#include <QMap>
#include <QImage>

template <class T>
class QFutureWatcher;

class BatchThumbnailGenerator : public QObject {
    Q_OBJECT

public:
    BatchThumbnailGenerator(QObject *parent = nullptr);

    void generate(QStringList paths);

signals:
    void thumbnailReady(const QString &path, const QImage &thumbnail);

private:
    void onResultReadyAt(int index);

    QFutureWatcher<QImage> *_iconGeneratorWatcher;
    QMap<QString, QImage> _iconRequestMap;
    QStringList _paths;
};

#endif // BATCHTHUMBNAILGENERATOR_H
