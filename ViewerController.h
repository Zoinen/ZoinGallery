#ifndef VIEWERCONTROLLER_H
#define VIEWERCONTROLLER_H

#include <QObject>

class QQmlApplicationEngine;
class FileListModel;

class ViewerController : public QObject {
    Q_OBJECT
public:
    ViewerController(QQmlApplicationEngine *engine);

    Q_INVOKABLE void doCd();

private:
    FileListModel *_fileListModel;
};

#endif // VIEWERCONTROLLER_H
