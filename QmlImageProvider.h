#ifndef QMLIMAGEPROVIDER_H
#define QMLIMAGEPROVIDER_H

#include <QQuickImageProvider>

class FileListModel;

class QmlImageProvider : public QQuickImageProvider {
public:
    QmlImageProvider(const QString &prefix, FileListModel *model);

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    QString _prefix;
    FileListModel *_model;
};

#endif // QMLIMAGEPROVIDER_H
