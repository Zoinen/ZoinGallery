#ifndef VIEWERCONTROLLER_H
#define VIEWERCONTROLLER_H

#include <QObject>
#include <QWindow>

class QQmlEngine;
class FileListModel;

class ViewerController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString currentPath MEMBER _currentPath NOTIFY currentPathChanged)

public:
    ViewerController(QQmlEngine *engine);

    Q_INVOKABLE void cd(QString folder);
    Q_INVOKABLE int up();
    Q_INVOKABLE void prepareToClose();
    Q_INVOKABLE void clipboardCopyIndexName(int index);
    Q_INVOKABLE void clipboardCopyIndexFullPath(int index);

signals:
    void currentPathChanged();
    void setCurrentIndex(int index);

private:
    FileListModel *_fileListModel;

    QString _currentPath;
};

#endif // VIEWERCONTROLLER_H
