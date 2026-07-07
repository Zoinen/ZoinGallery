#include "MainWindow.h"
#include "DisplayColorSpace.h"
#include "MacApplication.h"
#include "SvgCursor.h"

#include <QDebug>
#include <QGuiApplication>
#include <QQuickItem>
#include <QScreen>
#include <QSettings>
#include <QTimer>

#if defined(Q_OS_WIN)
#include "dwmapi.h"
#include <windows.h>
#endif

namespace
{
constexpr QSize kDefaultWindowSize(1250, 700);

QRect primaryAvailableGeometry()
{
    if (QScreen *primaryScreen = QGuiApplication::primaryScreen()) {
        return primaryScreen->availableGeometry();
    }
    return QRect(QPoint(0, 0), kDefaultWindowSize);
}

QRect centeredIn(const QRect &bounds, QSize size)
{
    size.setWidth(qMin(size.width(), bounds.width()));
    size.setHeight(qMin(size.height(), bounds.height()));
    return QRect(bounds.center() - QPoint(size.width() / 2, size.height() / 2), size);
}

QRect bestAvailableGeometryFor(const QRect &geometry)
{
    QRect bestAvailableGeometry;
    qsizetype bestVisibleArea = 0;

    const QList<QScreen *> screens = QGuiApplication::screens();
    for (QScreen *screen : screens) {
        const QRect availableGeometry = screen->availableGeometry();
        const QRect visibleRect = geometry.intersected(availableGeometry);
        const qsizetype visibleArea = static_cast<qsizetype>(visibleRect.width()) * visibleRect.height();
        if (visibleArea > bestVisibleArea) {
            bestVisibleArea = visibleArea;
            bestAvailableGeometry = availableGeometry;
        }
    }

    return bestVisibleArea > 0 ? bestAvailableGeometry : primaryAvailableGeometry();
}

QRect fitGeometryToAvailableBounds(QRect geometry, const QRect &bounds)
{
    if (!geometry.isValid()) {
        return centeredIn(bounds, kDefaultWindowSize);
    }

    geometry.setWidth(qMin(geometry.width(), bounds.width()));
    geometry.setHeight(qMin(geometry.height(), bounds.height()));

    if (geometry.left() < bounds.left()) {
        geometry.moveLeft(bounds.left());
    }
    if (geometry.right() > bounds.right()) {
        geometry.moveRight(bounds.right());
    }
    if (geometry.top() < bounds.top()) {
        geometry.moveTop(bounds.top());
    }
    if (geometry.bottom() > bounds.bottom()) {
        geometry.moveBottom(bounds.bottom());
    }

    return geometry;
}

QRect initialNormalGeometry(const QVariant &savedGeometryVar)
{
    if (!savedGeometryVar.isValid()) {
        return centeredIn(primaryAvailableGeometry(), kDefaultWindowSize);
    }

    const QRect savedGeometry = savedGeometryVar.toRect();
    return fitGeometryToAvailableBounds(savedGeometry, bestAvailableGeometryFor(savedGeometry));
}
}

MainWindow::MainWindow(QWindow *parent)
    : QQuickWindow(parent) {

    // QSurfaceFormat format;
    // format.setSamples(8);  // Specify the desired number of samples for multisampling
    // setFormat(format);

    _leftButtonPressed = false;
    _ignoreNormalGeometryChange = true;
    _enableNormalGeometryChangeOnNextExpose = true;
    _suppressShowRestoreGeometryEvents = false;
    _windowReadyEmitted = false;
    _delayedNormalGeometryChangeEnabler = new QTimer(this);
    _delayedNormalGeometryChangeEnabler->setInterval(150);
    _delayedNormalGeometryChangeEnabler->setSingleShot(true);
    connect(_delayedNormalGeometryChangeEnabler, &QTimer::timeout, this, [&] () {
        _ignoreNormalGeometryChange = false;
        enableWindowAnimations(true);
    });

    _dpr = -1;
    _lastVisibleVisibility = QWindow::Windowed;
    updateDpr();
    updateDisplayColorSpace();

    connect(this, &QWindow::screenChanged, this, [this] () {
        updateDisplayColorSpace();
    });

    QSettings set;
    _normalGeometry = initialNormalGeometry(set.value("normalGeometry"));
    setGeometry(_normalGeometry);

    connect(this, &QWindow::visibilityChanged, this, [this] (QWindow::Visibility visibility) {
        if (visibility != QWindow::Hidden && visibility != QWindow::Minimized) {
            _lastVisibleVisibility = visibility;
        }
    });

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
#if defined(Q_OS_WIN) && defined(__USE_QWK)
        // Temporary Windows workaround for transparent window BG bug
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

QString MainWindow::targetColorSpaceDescription() const {
    return DisplayColorSpace::currentDescription();
}

bool MainWindow::convertToDisplayColorSpace() const {
    return DisplayColorSpace::conversionEnabled();
}

void MainWindow::setConvertToDisplayColorSpace(bool enabled) {
    if (DisplayColorSpace::conversionEnabled() == enabled) {
        return;
    }
    DisplayColorSpace::setConversionEnabled(enabled);
    emit convertToDisplayColorSpaceChanged();
}

void MainWindow::showEvent(QShowEvent *event) {
    QQuickWindow::showEvent(event);
    _lastSize = size();
    qDebug() << "SHOW" << geometry() << "windowReadyEmitted" << _windowReadyEmitted;

    if (_windowReadyEmitted || _suppressShowRestoreGeometryEvents) {
        return;
    }

// #if defined(__USE_QWK)
    // Temporary workaround for transparent window BG bug

    QSettings set;
    QWindow::Visibility windowVisibility = set.value("windowVisibility", _lastVisibleVisibility).value<QWindow::Visibility>();
    if (windowVisibility == QWindow::Minimized || windowVisibility == QWindow::Hidden) {
        windowVisibility = _lastVisibleVisibility;
    }
    if (windowVisibility == QWindow::Minimized || windowVisibility == QWindow::Hidden) {
        windowVisibility = QWindow::Windowed;
    }
    _lastVisibleVisibility = windowVisibility;
    QRect geom = geometry();
    _suppressShowRestoreGeometryEvents = true;
    setGeometry(QRect(0, 0, 0, 0));
    QTimer::singleShot(0, this, [this, geom, windowVisibility] () {
        setGeometry(geom.adjusted(1, 0, 0, 0));
        setGeometry(geom);
        setVisibility(windowVisibility);

        QTimer::singleShot(0, this, [this] () {
            _lastSize = size();
            _suppressShowRestoreGeometryEvents = false;
            qDebug() << "Initial window show restore complete" << geometry();
            _windowReadyEmitted = true;
            qDebug() << "Window is ready";
            emit windowIsReady();
        });
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

void MainWindow::updateDisplayColorSpace() {
    const QString previousDescription = targetColorSpaceDescription();
    DisplayColorSpace::setCurrent(DisplayColorSpace::colorSpaceForScreen(screen()));
    if (targetColorSpaceDescription() != previousDescription) {
        emit targetColorSpaceChanged();
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
            if (_suppressShowRestoreGeometryEvents) {
                qDebug() << "Suppressing mainWindowResized during show restore";
            }
            else {
                bool changedWidth = _lastSize.width() != size().width();
                QTimer::singleShot(0, this, [=, this] () {
                    emit mainWindowResized(changedWidth);
                });
            }
        }
    }
    else if (event->type() == QEvent::Resize && !_leftButtonPressed) {
        if (_lastSize != size() && _lastSize.height() > 15 * _dpr && size().height() > 15 * _dpr && !_ignoreNormalGeometryChange) {
            qDebug() << "ZZ CHANGED 2" << _lastSize << "->" << size();
            if (_suppressShowRestoreGeometryEvents) {
                qDebug() << "Suppressing mainWindowResized during show restore";
            }
            else {
                bool changedWidth = _lastSize.width() != size().width();
                QTimer::singleShot(0, this, [=, this] () {
                    emit mainWindowResized(changedWidth);
                });
            }
        }
        _lastSize = size();
    }


    if (event->type() == QEvent::Resize && !_ignoreNormalGeometryChange && !_suppressShowRestoreGeometryEvents) {
        updateDpr();
        if (visibility() == QWindow::Windowed) {
            if (static_cast<QResizeEvent *>(event)->size() != _normalGeometry.size()) {
                _normalGeometry.setSize(static_cast<QResizeEvent *>(event)->oldSize());
            }
        }
    }
    else if (event->type() == QEvent::Move && !_ignoreNormalGeometryChange && !_suppressShowRestoreGeometryEvents) {
        updateDpr();
        if (visibility() == QWindow::Windowed) {
            if (static_cast<QMoveEvent *>(event)->pos() != _normalGeometry.topLeft()) {
                _normalGeometry.moveTopLeft(static_cast<QMoveEvent *>(event)->oldPos());
            }
        }
    }
    else if (event->type() == QEvent::Close) {
        qInfo() << "[Shutdown] MainWindow::event Close"
                << "visibility" << static_cast<int>(visibility())
                << "isVisible" << isVisible()
                << "ignoreNormalGeometryChange" << _ignoreNormalGeometryChange
                << "geometry" << geometry()
                << "normalGeometry" << _normalGeometry;
        if (visibility() == QWindow::Windowed && !_ignoreNormalGeometryChange) {
            _normalGeometry = geometry();
        }
        QSettings set;
        set.setValue("normalGeometry", _normalGeometry);
        QWindow::Visibility windowVisibility = visibility();
        if (windowVisibility == QWindow::Hidden || windowVisibility == QWindow::Minimized) {
            windowVisibility = _lastVisibleVisibility;
        }
        set.setValue("windowVisibility", windowVisibility);
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

void MainWindow::showAndActivate() {
    applyMacApplicationDockIconPolicy(true);

    const QWindow::Visibility currentVisibility = visibility();
    if (currentVisibility == QWindow::Hidden) {
        QSettings set;
        QWindow::Visibility savedVisibility = set.value("windowVisibility", _lastVisibleVisibility).value<QWindow::Visibility>();
        if (savedVisibility == QWindow::Hidden || savedVisibility == QWindow::Minimized) {
            savedVisibility = _lastVisibleVisibility;
        }
        if (savedVisibility == QWindow::Hidden || savedVisibility == QWindow::Minimized) {
            savedVisibility = QWindow::Windowed;
        }
        setVisibility(savedVisibility);
    }
    else if (currentVisibility == QWindow::Minimized) {
        QWindow::Visibility restoreVisibility = _lastVisibleVisibility;
        if (restoreVisibility == QWindow::Hidden || restoreVisibility == QWindow::Minimized) {
            restoreVisibility = QWindow::Windowed;
        }
        setVisibility(restoreVisibility);
    }

    auto activate = [this] () {
        raise();
        requestActivate();
#if defined(Q_OS_WIN)
        const HWND hwnd = reinterpret_cast<HWND>(winId());
        if (hwnd) {
            if (IsIconic(hwnd)) {
                ShowWindow(hwnd, _lastVisibleVisibility == QWindow::Maximized ? SW_SHOWMAXIMIZED : SW_RESTORE);
            }
            SetForegroundWindow(hwnd);
        }
#endif
    };

    activate();

    // The first show may restore geometry/visibility asynchronously, so activate
    // once more after Qt has processed that queued work.
    QTimer::singleShot(0, this, activate);
}
