#ifndef VIEWERCONTROLLER_H
#define VIEWERCONTROLLER_H

#include <QObject>
#include <QWindow>

class QQmlEngine;
class FileListModel;

class ViewerController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString currentPath MEMBER _currentPath NOTIFY currentPathChanged)
    Q_PROPERTY(QWindow *mainWindow READ mainWindow WRITE setMainWindow)

public:
    ViewerController(QQmlEngine *engine);

    Q_INVOKABLE void cd(QString folder);
    Q_INVOKABLE int up();
    Q_INVOKABLE void prepareToClose();

    QWindow *mainWindow() const;
    void setMainWindow(QWindow *mainWindow);

    bool eventFilter(QObject *watched, QEvent *event) override;

signals:
    void currentPathChanged();
    void mainWindowResized();

private:
    FileListModel *_fileListModel;

    QString _currentPath;
    QWindow *_mainWindow;
    bool _leftButtonPressed;
    QSize _lastSize;
};

#endif // VIEWERCONTROLLER_H
