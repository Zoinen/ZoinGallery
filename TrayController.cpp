#include "TrayController.h"

#include "MainWindow.h"

#include <QAction>
#include <QGuiApplication>
#include <QIcon>
#include <QMenu>
#include <QSystemTrayIcon>

TrayController::TrayController(QWindow *mainWindow, QObject *parent)
    : QObject(parent)
    , _mainWindow(mainWindow)
    , _trayIcon(nullptr)
    , _trayMenu(nullptr)
    , _available(QSystemTrayIcon::isSystemTrayAvailable()) {
    if (_available) {
        createTrayIcon();
    }
}

TrayController::~TrayController() {
    hideTray();
}

bool TrayController::isAvailable() const {
    return _available;
}

void TrayController::createTrayIcon() {
    QIcon icon(QStringLiteral(":/resources/ZoinGallery.ico"));
    if (icon.isNull()) {
        icon = QIcon(QStringLiteral(":/resources/Logo.svg"));
    }

    _trayIcon = new QSystemTrayIcon(icon, this);
    _trayIcon->setToolTip(QGuiApplication::applicationName());

    _trayMenu = new QMenu();
    QAction *showAction = _trayMenu->addAction(QStringLiteral("Show"));
    connect(showAction, &QAction::triggered, this, &TrayController::showFromTray);

    _trayMenu->addSeparator();

    QAction *quitAction = _trayMenu->addAction(QStringLiteral("Quit"));
    connect(quitAction, &QAction::triggered, this, &TrayController::quitRequested);

    _trayIcon->setContextMenu(_trayMenu);
    connect(_trayIcon, &QSystemTrayIcon::activated, this, [this] (QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::DoubleClick) {
            showFromTray();
        }
    });
}

void TrayController::showTray() {
    if (_trayIcon) {
        _trayIcon->show();
    }
}

void TrayController::hideTray() {
    if (_trayIcon) {
        _trayIcon->hide();
    }
}

void TrayController::showFromTray() {
    if (!_mainWindow) {
        return;
    }

    if (auto *mainWindow = qobject_cast<MainWindow *>(_mainWindow)) {
        mainWindow->showAndActivate();
    }
    else {
        _mainWindow->show();
        _mainWindow->raise();
        _mainWindow->requestActivate();
    }
}
