#ifndef VIEWERCONTROLLER_H
#define VIEWERCONTROLLER_H

#include <QObject>
#include <QWindow>

class QQmlEngine;
class FileListModel;

class ViewerController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString currentPath MEMBER _currentPath NOTIFY currentPathChanged)
    Q_PROPERTY(bool canUp READ canUp NOTIFY canUpChanged)
    Q_PROPERTY(bool canBack READ canBack NOTIFY canBackChanged)
    Q_PROPERTY(bool canForward READ canForward NOTIFY canForwardChanged)
    Q_PROPERTY(QStringList backMenu READ backMenu NOTIFY historyChanged)
    Q_PROPERTY(QStringList forwardMenu READ forwardMenu NOTIFY historyChanged)

public:
    ViewerController(QQmlEngine *engine);

    Q_INVOKABLE void cd(QString folder, bool changeHistory = true);

    Q_INVOKABLE int up();
    bool canUp() const;

    Q_INVOKABLE void back();
    bool canBack() const;
    Q_INVOKABLE QStringList backMenu() const;

    Q_INVOKABLE void forward();
    bool canForward() const;
    Q_INVOKABLE QStringList forwardMenu() const;

    Q_INVOKABLE void jumpBack(int backIndex);
    Q_INVOKABLE void jumpForward(int forwardIndex);

    Q_INVOKABLE void prepareToClose();
    Q_INVOKABLE void clipboardCopyIndexName(int index);
    Q_INVOKABLE void clipboardCopyIndexFullPath(int index);

signals:
    void currentPathChanged();
    void setCurrentIndex(int index);

    void canUpChanged();
    void canBackChanged();
    void canForwardChanged();
    void historyChanged();

private:
    void updateHistory(bool changeHistory);

    FileListModel *_fileListModel;

    QString _currentPath;
    QStringList _history;
    bool _canUp;
    bool _canBack;
    bool _canForward;
    int _indexInHistory;
};

#endif // VIEWERCONTROLLER_H
