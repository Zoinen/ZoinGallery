#ifndef QMLIMAGEPROVIDER_H
#define QMLIMAGEPROVIDER_H

#include <QQuickImageProvider>

class FileListModel;
class StandardFileListModel;

class QmlImageProvider : public QQuickImageProvider {
public:
    QmlImageProvider(const QString &prefix, FileListModel *model);
    QmlImageProvider(const QString &prefix, StandardFileListModel *model);

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    QString _prefix;
    FileListModel *_model;
    StandardFileListModel *_standardModel;
};

#endif // QMLIMAGEPROVIDER_H
