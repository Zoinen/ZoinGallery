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
    Q_PROPERTY(bool isResizing READ isResizing NOTIFY isResizingChanged)

public:
    ViewerController(QQmlEngine *engine);

    Q_INVOKABLE void cd(QString folder);
    Q_INVOKABLE int up();
    Q_INVOKABLE void prepareToClose();
    Q_INVOKABLE void clipboardCopyIndexName(int index);
    Q_INVOKABLE void clipboardCopyIndexFullPath(int index);

    QWindow *mainWindow() const;
    void setMainWindow(QWindow *mainWindow);

    bool eventFilter(QObject *watched, QEvent *event) override;

    bool isResizing() const;

signals:
    void currentPathChanged();
    void mainWindowResized();
    void setCurrentIndex(int index);

    void isResizingChanged();

private:
    FileListModel *_fileListModel;

    QString _currentPath;
    QWindow *_mainWindow;
    bool _leftButtonPressed;
    QSize _lastSize;
    QRect _normalGeometry;
};

#endif // VIEWERCONTROLLER_H
