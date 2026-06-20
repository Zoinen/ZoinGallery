#ifndef TRAYCONTROLLER_H
#define TRAYCONTROLLER_H

#include <QObject>

class QMenu;
class QSystemTrayIcon;
class QWindow;

class TrayController : public QObject {
    Q_OBJECT

public:
    explicit TrayController(QWindow *mainWindow, QObject *parent = nullptr);
    ~TrayController() override;

    bool isAvailable() const;
    void showTray();
    void hideTray();
    void showFromTray();

signals:
    void quitRequested();

private:
    void createTrayIcon();

    QWindow *_mainWindow;
    QSystemTrayIcon *_trayIcon;
    QMenu *_trayMenu;
    bool _available;
};

#endif // TRAYCONTROLLER_H
