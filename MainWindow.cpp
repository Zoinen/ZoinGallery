#include "MainWindow.h"

#include <QSettings>
#include <QTimer>
#include <QDebug>

MainWindow::MainWindow(QWindow *parent)
    : QQuickWindow(parent) {
    _leftButtonPressed = false;
    _ignoreNormalGeometryChange = false;

    _dpr = -1;
    updateDpr();

    QSettings set;
    QVariant savedGeometryVar = set.value("normalGeometry");
    if (savedGeometryVar.isValid()) {
        _normalGeometry = savedGeometryVar.toRect();
    }
    else if (screen()) {
        QSize size(1250, 700);
        _normalGeometry = QRect(screen()->virtualGeometry().center() - QPoint(size.width() / 2, size.height() / 2), size);
    }
    setGeometry(_normalGeometry);

    QWindow::Visibility windowVisibility = set.value("windowVisibility", QWindow::Windowed).value<QWindow::Visibility>();
    if (windowVisibility == Minimized) {
        windowVisibility = Windowed;
    }
    setVisibility(windowVisibility);
}

void MainWindow::toggleFullscreen() {
    QSettings set;
    QWindow::Visibility nonFSVisibility = set.value("nonFSVisibility", QWindow::Windowed).value<QWindow::Visibility>();

    if (visibility() == QWindow::FullScreen) {
        setVisibility(nonFSVisibility);
        _ignoreNormalGeometryChange = false;
    }
    else {
        _ignoreNormalGeometryChange = true;
        set.setValue("nonFSVisibility", visibility());
        setVisibility(QWindow::FullScreen);
    }
}

bool MainWindow::isResizing() const {
    return _leftButtonPressed;
}

void MainWindow::showEvent(QShowEvent *event) {
    QQuickWindow::showEvent(event);
    _lastSize = size();
    qDebug() << "SHOW" << geometry();
}

void MainWindow::updateDpr() {
    if (_dpr != devicePixelRatio()) {
        qDebug() << "DPR changed" << _dpr << "->" << devicePixelRatio();
        _dpr = devicePixelRatio();
        emit dprChanged();
    }
}

bool MainWindow::event(QEvent *event) {
    if (event->type() == QEvent::NonClientAreaMouseButtonPress) {
        _leftButtonPressed = true;
        emit isResizingChanged();
        _lastSize = size();
    }
    else if (event->type() == QEvent::NonClientAreaMouseButtonRelease && _leftButtonPressed) {
        _leftButtonPressed = false;
        emit isResizingChanged();
        if (_lastSize != size()) {
            QTimer::singleShot(0, this, [&] () {
                emit mainWindowResized();
            });
        }
    }
    else if (event->type() == QEvent::Resize && !_leftButtonPressed) {
        if (_lastSize != size()) {
            QTimer::singleShot(0, this, [&] () {
                emit mainWindowResized();
            });
        }
        _lastSize = size();
    }


    if (event->type() == QEvent::Resize && !_ignoreNormalGeometryChange) {
        updateDpr();
        if (visibility() == QWindow::Windowed) {
            if (static_cast<QResizeEvent *>(event)->size() != _normalGeometry.size()) {
                _normalGeometry.setSize(static_cast<QResizeEvent *>(event)->oldSize());
            }
        }
    }
    else if (event->type() == QEvent::Move && !_ignoreNormalGeometryChange) {
        updateDpr();
        if (visibility() == QWindow::Windowed) {
            if (static_cast<QMoveEvent *>(event)->pos() != _normalGeometry.topLeft()) {
                _normalGeometry.moveTopLeft(static_cast<QMoveEvent *>(event)->oldPos());
            }
        }
    }
    else if (event->type() == QEvent::Close) {
        if (visibility() == QWindow::Windowed && !_ignoreNormalGeometryChange) {
            _normalGeometry = geometry();
        }
        QSettings set;
        set.setValue("normalGeometry", _normalGeometry);
        set.setValue("windowVisibility", visibility());
    }
    return QQuickWindow::event(event);
}

int MainWindow::availableScreenHeight() const {
    return screen()->availableSize().height();
}
