#ifndef VIEWERCONTROLLER_H
#define VIEWERCONTROLLER_H

#include <QObject>
#include <QWindow>

class QQmlEngine;
class FileListModel;
class ImageModel;
class TrayController;
class BackgroundInstance;

class ViewerController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString currentPath MEMBER _currentPath NOTIFY currentPathChanged)
    Q_PROPERTY(bool canUp READ canUp NOTIFY canUpChanged)
    Q_PROPERTY(bool canBack READ canBack NOTIFY canBackChanged)
    Q_PROPERTY(bool canForward READ canForward NOTIFY canForwardChanged)
    Q_PROPERTY(QStringList backMenu READ backMenu NOTIFY historyChanged)
    Q_PROPERTY(QStringList forwardMenu READ forwardMenu NOTIFY historyChanged)
    Q_PROPERTY(bool backgroundMode READ backgroundMode CONSTANT)
    Q_PROPERTY(bool pendingOpenInViewer READ pendingOpenInViewer NOTIFY pendingOpenInViewerChanged)

public:
    ViewerController(QQmlEngine *engine);

    Q_INVOKABLE void cd(const QString &currentFolder, bool changeHistory = true);

    Q_INVOKABLE int up();
    bool canUp() const;

    Q_INVOKABLE void saveCurrentState(qreal contentY, int currentIndex);
    Q_INVOKABLE qreal savedContentY() const;
    Q_INVOKABLE int savedCurrentIndex() const;

    Q_INVOKABLE void back();
    bool canBack() const;
    Q_INVOKABLE QStringList backMenu() const;

    Q_INVOKABLE void forward();
    bool canForward() const;
    Q_INVOKABLE QStringList forwardMenu() const;

    Q_INVOKABLE void jumpBack(int backIndex);
    Q_INVOKABLE void jumpForward(int forwardIndex);

    Q_INVOKABLE void prepareToClose();
    Q_INVOKABLE void hideToTray();
    Q_INVOKABLE void quitApplication();
    Q_INVOKABLE void clipboardCopyIndexName(int index);
    Q_INVOKABLE void clipboardCopyIndexFullPath(int index);
    Q_INVOKABLE QUrl indexUrl(int index);

    Q_INVOKABLE void enterRecursiveView();

    Q_INVOKABLE void initialCd(int viewerWidth = -1, int viewerHeight = -1);
    Q_INVOKABLE void openPendingExternalFileInViewer(int viewerWidth = -1, int viewerHeight = -1);

    Q_INVOKABLE QColor adjustHSL(const QColor &color, qreal h, qreal s, qreal l);
    Q_INVOKABLE QString makeDefaultImageViewer();

    bool backgroundMode() const;
    bool pendingOpenInViewer() const;

    void setBackgroundMode(bool enabled);
    void setStartupFilePath(const QString &path);
    void initializeBackgroundMode(QWindow *mainWindow);

    void openExternalFile(const QString &path);
    void activateFromExternal();

    Q_INVOKABLE void clearPendingOpenInViewer();

signals:
    void currentPathChanged();
    void setCurrentIndex(int index);

    void canUpChanged();
    void canBackChanged();
    void canForwardChanged();
    void historyChanged();

    void externalActivateRequested();
    void externalFileOpened();
    void pendingOpenInViewerChanged();

private:
    void updateHistory(bool changeHistory);
    void loadSavedState();
    int setCurrentPath(const QString &newPath, const QString &itemToSelect = QString());
    void openFileInViewer(const QString &path, int viewerWidth, int viewerHeight, bool changeHistory = true);

    FileListModel *_fileListModel;
    ImageModel *_imageModel;
    TrayController *_trayController;
    BackgroundInstance *_backgroundInstance;

    QString _currentPath;
    QString _startupFilePath;
    QString _pendingExternalFilePath;
    struct HistoryEntity {
        QString path;
        int currentIndex;
        qreal contentY;

        HistoryEntity() : currentIndex(0), contentY(0) {}
        HistoryEntity(QString path_) : path(path_), currentIndex(0), contentY(0) {}
    };

    QList<HistoryEntity> _history;
    bool _canUp;
    bool _canBack;
    bool _canForward;
    int _indexInHistory;

    qreal _savedContentY;
    int _savedCurrentIndex;

    bool _backgroundMode;
    bool _pendingOpenInViewer;
};

#endif // VIEWERCONTROLLER_H
