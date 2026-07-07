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
    void showTray(bool windowHidden = true);
    void hideTray(bool restoreDock = true);

public slots:
    void showFromTray();

signals:
    void quitRequested();

private:
    void createTrayIcon();
    void scheduleTrayMenu();
    void showTrayMenuAfterMouseRelease();
    void showTrayMenu();
    void toggleFromTray();

    QWindow *_mainWindow;
    QSystemTrayIcon *_trayIcon;
    QMenu *_trayMenu;
    bool _available;
    bool _trayMenuPending;
};

#endif // TRAYCONTROLLER_H
