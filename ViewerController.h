#ifndef VIEWERCONTROLLER_H
#define VIEWERCONTROLLER_H

#include <QObject>

class QQmlEngine;
class FileListModel;

class ViewerController : public QObject {
    Q_OBJECT
public:
    ViewerController(QQmlEngine *engine);

    Q_INVOKABLE void doCd();

private:
    FileListModel *_fileListModel;
};

#endif // VIEWERCONTROLLER_H
