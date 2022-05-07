#ifndef VIEWERCONTROLLER_H
#define VIEWERCONTROLLER_H

#include <QObject>

class QQmlEngine;
class FileListModel;

class ViewerController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString currentPath MEMBER _currentPath NOTIFY currentPathChanged)

public:
    ViewerController(QQmlEngine *engine);

    Q_INVOKABLE void setThumbnailResolution(QSize dimensions, qreal dpr);
    Q_INVOKABLE void doCd();
    Q_INVOKABLE void cd(QString folder);
    Q_INVOKABLE int up();
    Q_INVOKABLE void prepareToClose();

signals:
    void currentPathChanged();

private:
    FileListModel *_fileListModel;

    QString _currentPath;
};

#endif // VIEWERCONTROLLER_H
