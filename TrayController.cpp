#include "TrayController.h"

#include "MacApplication.h"
#include "MainWindow.h"

#include <QAction>
#include <QCursor>
#include <QDebug>
#include <QGuiApplication>
#include <QIcon>
#include <QMenu>
#include <QScreen>
#include <QSystemTrayIcon>
#include <QTimer>

namespace
{
constexpr int kMacTrayMenuYOffset = 14;
constexpr int kMacFallbackStatusItemSize = 24;

QPoint clampMenuPositionToScreen(QPoint position, const QSize &menuSize, const QRect &screenGeometry)
{
    const int maxX = screenGeometry.right() - menuSize.width() + 1;
    const int maxY = screenGeometry.bottom() - menuSize.height() + 1;
    if (maxX >= screenGeometry.left()) {
        position.setX(qBound(screenGeometry.left(), position.x(), maxX));
    }
    if (maxY >= screenGeometry.top()) {
        position.setY(qBound(screenGeometry.top(), position.y(), maxY));
    }
    return position;
}

#if defined(Q_OS_MACOS)
QPoint fallbackMacTrayMenuPosition(const QSize &menuSize)
{
    const QPoint cursorPosition = QCursor::pos();
    QScreen *screen = QGuiApplication::screenAt(cursorPosition);
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (!screen) {
        return cursorPosition;
    }

    const QRect screenGeometry = screen->geometry();
    const QRect availableGeometry = screen->availableGeometry();
    int iconBottom = screenGeometry.top() + kMacFallbackStatusItemSize;
    if (availableGeometry.top() > screenGeometry.top() &&
        availableGeometry.top() - screenGeometry.top() < 80) {
        iconBottom = availableGeometry.top();
    }

    QPoint position(cursorPosition.x() - kMacFallbackStatusItemSize / 2,
                    iconBottom - kMacTrayMenuYOffset);
    return clampMenuPositionToScreen(position, menuSize, screenGeometry);
}
#endif
}

TrayController::TrayController(QWindow *mainWindow, QObject *parent)
    : QObject(parent)
    , _mainWindow(mainWindow)
    , _trayIcon(nullptr)
    , _trayMenu(nullptr)
    , _available(QSystemTrayIcon::isSystemTrayAvailable())
    , _trayMenuPending(false) {
    if (_available) {
        createTrayIcon();
    }
}

TrayController::~TrayController() {
    hideTray(false);
    delete _trayMenu;
}

bool TrayController::isAvailable() const {
    return _available;
}

void TrayController::createTrayIcon() {
    QIcon icon(QStringLiteral(":/ZoinGallery/resources/ZoinGallery.ico"));
    if (icon.isNull()) {
        icon = QIcon(QStringLiteral(":/ZoinGallery/resources/Logo.svg"));
    }

    _trayIcon = new QSystemTrayIcon(icon, this);
    _trayIcon->setToolTip(QGuiApplication::applicationName());

    _trayMenu = new QMenu();
    QAction *showAction = _trayMenu->addAction(QStringLiteral("Show"));
    connect(showAction, &QAction::triggered, this, [this]() {
        qInfo() << "[Shutdown] Tray menu Show action triggered";
        showFromTray();
    });

    _trayMenu->addSeparator();

    QAction *quitAction = _trayMenu->addAction(QStringLiteral("Quit"));
    connect(quitAction, &QAction::triggered, this, [this]() {
        qInfo() << "[Shutdown] Tray menu Quit action triggered";
        emit quitRequested();
    });

#if !defined(Q_OS_MACOS)
    _trayIcon->setContextMenu(_trayMenu);
#endif
    connect(_trayIcon, &QSystemTrayIcon::activated, this, [this] (QSystemTrayIcon::ActivationReason reason) {
        qInfo() << "[Shutdown] Tray icon activated"
                << "reason" << static_cast<int>(reason)
                << "windowVisible" << (_mainWindow ? _mainWindow->isVisible() : false)
                << "windowVisibility" << (_mainWindow ? static_cast<int>(_mainWindow->visibility()) : -1);
#if defined(Q_OS_MACOS)
        if (reason == QSystemTrayIcon::Trigger) {
            qInfo() << "[Shutdown] Tray activation branch: toggle from left click";
            toggleFromTray();
        }
        else if (reason == QSystemTrayIcon::Context) {
            qInfo() << "[Shutdown] Tray activation branch: schedule context menu";
            scheduleTrayMenu();
        }
#else
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
            qInfo() << "[Shutdown] Tray activation branch: toggle from trigger/double-click";
            toggleFromTray();
        }
#endif
    });
}

void TrayController::showTray(bool windowHidden) {
    qInfo() << "[Shutdown] TrayController::showTray"
            << "windowHidden" << windowHidden;
    if (_trayIcon) {
        _trayIcon->show();
        applyMacApplicationDockIconPolicy(!windowHidden);
    }
}

void TrayController::hideTray(bool restoreDock) {
    qInfo() << "[Shutdown] TrayController::hideTray"
            << "restoreDock" << restoreDock
            << "hasTrayIcon" << (_trayIcon != nullptr);
    if (restoreDock) {
        applyMacApplicationDockIconPolicy(true);
    }
    if (_trayIcon) {
        _trayIcon->hide();
    }
}

void TrayController::scheduleTrayMenu() {
    if (_trayMenuPending) {
        qInfo() << "[Shutdown] TrayController::scheduleTrayMenu skipped: already pending";
        return;
    }

    _trayMenuPending = true;
    showTrayMenuAfterMouseRelease();
}

void TrayController::showTrayMenuAfterMouseRelease() {
    if (!_trayMenuPending) {
        return;
    }

    const Qt::MouseButtons buttons = QGuiApplication::mouseButtons();
#if defined(Q_OS_MACOS)
    const bool mouseButtonsPressed = macMouseButtonsPressed();
#else
    const bool mouseButtonsPressed = buttons != Qt::NoButton;
#endif
    if (mouseButtonsPressed) {
        qInfo() << "[Shutdown] TrayController::showTrayMenuAfterMouseRelease waiting"
                << "qtButtons" << static_cast<int>(buttons)
                << "macButtonsPressed" << mouseButtonsPressed;
        QTimer::singleShot(25, this, &TrayController::showTrayMenuAfterMouseRelease);
        return;
    }

    _trayMenuPending = false;
    qInfo() << "[Shutdown] TrayController::showTrayMenuAfterMouseRelease mouse released, posting menu";
    QTimer::singleShot(25, this, [this]() {
        qInfo() << "[Shutdown] TrayController::showTrayMenuAfterMouseRelease showing menu";
        showTrayMenu();
    });
}

void TrayController::showTrayMenu() {
    if (!_trayMenu) {
        qInfo() << "[Shutdown] TrayController::showTrayMenu skipped: no menu";
        return;
    }

    QPoint menuPosition = QCursor::pos();
    const QSize menuSize = _trayMenu->sizeHint();
    if (_trayIcon) {
        const QRect iconGeometry = _trayIcon->geometry();
        if (iconGeometry.isValid() && !iconGeometry.isEmpty()) {
            menuPosition = QPoint(iconGeometry.left(), iconGeometry.bottom() - kMacTrayMenuYOffset);

            if (QScreen *screen = QGuiApplication::screenAt(iconGeometry.center())) {
                menuPosition = clampMenuPositionToScreen(menuPosition, menuSize, screen->geometry());
            }
        }
#if defined(Q_OS_MACOS)
        else {
            menuPosition = fallbackMacTrayMenuPosition(menuSize);
        }
#endif
    }

    qInfo() << "[Shutdown] TrayController::showTrayMenu"
            << "position" << menuPosition
            << "trayGeometry" << (_trayIcon ? _trayIcon->geometry() : QRect());

#if defined(Q_OS_MACOS)
    showMacTrayMenu(_trayMenu, menuPosition);
#else
    _trayMenu->popup(menuPosition);
#endif
}

void TrayController::toggleFromTray() {
    if (!_mainWindow) {
        qInfo() << "[Shutdown] TrayController::toggleFromTray skipped: no main window";
        return;
    }

    const QWindow::Visibility visibility = _mainWindow->visibility();
    const bool windowIsShown = _mainWindow->isVisible()
                               && visibility != QWindow::Hidden
                               && visibility != QWindow::Minimized;
    const bool windowIsActive = _mainWindow->isActive()
                                || QGuiApplication::focusWindow() == _mainWindow;
    qInfo() << "[Shutdown] TrayController::toggleFromTray"
            << "windowIsShown" << windowIsShown
            << "windowIsActive" << windowIsActive
            << "isVisible" << _mainWindow->isVisible()
            << "visibility" << static_cast<int>(visibility);
    if (windowIsShown && windowIsActive) {
        qInfo() << "[Shutdown] TrayController::toggleFromTray closing main window";
        _mainWindow->close();
        return;
    }

    qInfo() << "[Shutdown] TrayController::toggleFromTray showing or activating main window";
    showFromTray();
}

void TrayController::showFromTray() {
    if (!_mainWindow) {
        qInfo() << "[Shutdown] TrayController::showFromTray skipped: no main window";
        return;
    }

    qInfo() << "[Shutdown] TrayController::showFromTray"
            << "isVisible" << _mainWindow->isVisible()
            << "visibility" << static_cast<int>(_mainWindow->visibility());

    applyMacApplicationDockIconPolicy(true);

    if (auto *mainWindow = qobject_cast<MainWindow *>(_mainWindow)) {
        mainWindow->showAndActivate();
    }
    else {
        _mainWindow->show();
        _mainWindow->raise();
        _mainWindow->requestActivate();
    }
}
