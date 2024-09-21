#include "MainWindow.h"
#include "SvgCursor.h"

#include <QDebug>
#include <QGuiApplication>
#include <QQuickItem>
#include <QSettings>
#include <QTimer>

#if defined(Q_OS_WIN)
#include "dwmapi.h"
#endif

MainWindow::MainWindow(QWindow *parent)
    : QQuickWindow(parent) {

    // QSurfaceFormat format;
    // format.setSamples(8);  // Specify the desired number of samples for multisampling
    // setFormat(format);

    _leftButtonPressed = false;
    _ignoreNormalGeometryChange = true;
    _enableNormalGeometryChangeOnNextExpose = true;
    _delayedNormalGeometryChangeEnabler = new QTimer(this);
    _delayedNormalGeometryChangeEnabler->setInterval(150);
    _delayedNormalGeometryChangeEnabler->setSingleShot(true);
    connect(_delayedNormalGeometryChangeEnabler, &QTimer::timeout, this, [&] () {
        _ignoreNormalGeometryChange = false;
        enableWindowAnimations(true);
    });

    _dpr = -1;
    updateDpr();

    QSettings set;
    QVariant savedGeometryVar = set.value("normalGeometry");
    if (savedGeometryVar.isValid()) {
        _normalGeometry = savedGeometryVar.toRect();
        QRect availableRect = QGuiApplication::primaryScreen()->availableGeometry();
        if (_normalGeometry.right() > availableRect.right()) {
            _normalGeometry.moveRight(availableRect.right());
        }
        if (_normalGeometry.bottom() > availableRect.bottom()) {
            _normalGeometry.moveBottom(availableRect.bottom());
        }
        if (_normalGeometry.left() < availableRect.left()) {
            _normalGeometry.moveLeft(availableRect.left());
        }
        if (_normalGeometry.top() < availableRect.top()) {
            _normalGeometry.moveTop(availableRect.top());
        }
        if (_normalGeometry.width() > availableRect.width()) {
            _normalGeometry.setWidth(availableRect.width());
        }
        if (_normalGeometry.height() > availableRect.height()) {
            _normalGeometry.setHeight(availableRect.height());
        }
    }
    else if (screen()) {
        QSize size(1250, 700);
        _normalGeometry = QRect(screen()->virtualGeometry().center() - QPoint(size.width() / 2, size.height() / 2), size);
    }
    setGeometry(_normalGeometry);

    enableWindowAnimations(false);

    setVisibility(Windowed);
}

void MainWindow::toggleFullscreen() {
    _ignoreNormalGeometryChange = true;
    _delayedNormalGeometryChangeEnabler->stop();
    enableWindowAnimations(false);
    QSettings set;
    QWindow::Visibility nonFSVisibility = set.value("nonFSVisibility", QWindow::Windowed).value<QWindow::Visibility>();

    QRect prevGeometry = geometry();
    if (visibility() == QWindow::FullScreen) {
#if defined(__USE_QWK)
        // Temporary workaround for transparent window BG bug
        setGeometry(QRect(0, 0, 0, 0));
        QTimer::singleShot(0, this, [=] () {
            setGeometry(_normalGeometry.adjusted(1, 0, 0, 0));
            setGeometry(_normalGeometry);
            qDebug() << "ZZ NORM GEOM" << _normalGeometry;
            setVisibility(nonFSVisibility);
            _enableNormalGeometryChangeOnNextExpose = true;

            if (prevGeometry != geometry()) {
                emit mainWindowResized(prevGeometry.width() != geometry().width());
            }
        });
#else
        setVisibility(nonFSVisibility);
#endif
    }
    else {
        set.setValue("nonFSVisibility", visibility());
        setVisibility(QWindow::FullScreen);
        emit fullScreenEntered();

        if (prevGeometry != geometry()) {
            emit mainWindowResized(prevGeometry.width() != geometry().width());
        }
    }
}

void MainWindow::setMousePos(QPoint pos) const {
    QCursor::setPos(pos);
}

QPointF MainWindow::mousePos() const {
    return QCursor::pos().toPointF();
}

bool MainWindow::isPressedOnTitleBar() const {
    return !mouseGrabberItem() || (mouseGrabberItem() && mouseGrabberItem()->objectName().startsWith("titleBar", Qt::CaseInsensitive));
}

void MainWindow::setSphereScrollingMouseCursor(bool set, bool idle, qreal rotation) {
    SvgCursor::setOverrideCursor(set ? (idle ? ":/resources/SphereScrollIdle.svg" : ":/resources/SphereScroll.svg") : "",
                                 devicePixelRatio(), rotation);
}

bool MainWindow::isResizing() const {
    return _leftButtonPressed;
}

bool MainWindow::isQWK() const {
#if defined(__USE_QWK)
    return true;
#else
    return false;
#endif
}

void MainWindow::showEvent(QShowEvent *event) {
    QQuickWindow::showEvent(event);
    _lastSize = size();
    qDebug() << "SHOW" << geometry();

// #if defined(__USE_QWK)
    // Temporary workaround for transparent window BG bug

    QSettings set;
    QWindow::Visibility windowVisibility = set.value("windowVisibility", QWindow::Windowed).value<QWindow::Visibility>();
    if (windowVisibility == Minimized) {
        windowVisibility = Windowed;
    }
    QRect geom = geometry();
    setGeometry(QRect(0, 0, 0, 0));
    QTimer::singleShot(0, this, [=] () {
        setGeometry(geom.adjusted(1, 0, 0, 0));
        setGeometry(geom);
        setVisibility(windowVisibility);
        qDebug() << "Window is ready";
        emit windowIsReady();
    });
// #endif
}

void MainWindow::updateDpr() {
    if (_dpr != devicePixelRatio()) {
        qDebug() << "DPR changed" << _dpr << "->" << devicePixelRatio();
        _dpr = devicePixelRatio();
        emit dprChanged();
    }
}

void MainWindow::enableWindowAnimations(bool enable) {
#if defined(Q_OS_WIN)
    BOOL attrib = !enable;
    DwmSetWindowAttribute((HWND)winId(), DWMWA_TRANSITIONS_FORCEDISABLED, &attrib, sizeof(attrib));
#endif
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
        if (_lastSize != size() && _lastSize.height() > 15 * _dpr && size().height() > 15 * _dpr && !_ignoreNormalGeometryChange) {
            qDebug() << "ZZ CHANGED 1" << _lastSize << "->" << size();
            bool changedWidth = _lastSize.width() != size().width();
            QTimer::singleShot(0, this, [=, this] () {
                emit mainWindowResized(changedWidth);
            });
        }
    }
    else if (event->type() == QEvent::Resize && !_leftButtonPressed) {
        if (_lastSize != size() && _lastSize.height() > 15 * _dpr && size().height() > 15 * _dpr && !_ignoreNormalGeometryChange) {
            qDebug() << "ZZ CHANGED 2" << _lastSize << "->" << size();
            bool changedWidth = _lastSize.width() != size().width();
            QTimer::singleShot(0, this, [=, this] () {
                emit mainWindowResized(changedWidth);
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
    else if (event->type() == QEvent::Expose && _enableNormalGeometryChangeOnNextExpose) {
        if (visibility() != QWindow::FullScreen) {
            _enableNormalGeometryChangeOnNextExpose = false;
            _delayedNormalGeometryChangeEnabler->start();
        }
    }
    return QQuickWindow::event(event);
}

int MainWindow::availableScreenHeight() const {
    return screen()->availableSize().height();
}
