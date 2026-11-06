#include "HdrVideoOutput.h"
#include "ViewerVideoFrameSource.h"

#include <QScreen>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QSGRendererInterface>
#include <QTimer>
#include <QtConcurrentRun>
#include <rhi/qrhi.h>
#include <atomic>
#include <memory>
#include <cmath>
#include <utility>

namespace ZoinGallery {

HdrVideoOutput::HdrVideoOutput(QObject *parent) : QObject(parent)
{
    connect(&m_switch, &QFutureWatcher<int>::finished, this, [this] {
        const int result = m_switch.result();
        if (m_restoring) {
            if (result != 0)
                m_error = tr("Could not restore the display's HDR mode (Windows error %1).").arg(result);
            else
                m_restoreDisplay = false;
            finishClose();
        } else if (result != 0) {
            const auto current = queryHdrDisplayState(m_ownedDisplay);
            if (!current.error && current.enabled == m_ownedDisplay.enabled)
                m_restoreDisplay = false;
            fail(tr("Could not enable HDR (Windows error %1).").arg(result));
        } else if (m_closing) {
            close();
        } else {
            completeEnable();
        }
    });
}

HdrVideoOutput::~HdrVideoOutput()
{
    disconnect(&m_switch, nullptr, this, nullptr);
    // A just-closed window may still be waiting for deleteLater(). Retire its
    // render thread before destroying the controller captured by its callbacks.
    const auto windows = findChildren<QQuickWindow *>(QString(), Qt::FindDirectChildrenOnly);
    for (auto *window : windows) {
        disconnect(window, nullptr, this, nullptr);
        delete window;
    }
    m_switch.waitForFinished();
    if (m_restoreDisplay) setHdrDisplayEnabled(m_ownedDisplay, false);
}

void HdrVideoOutput::setSurface(QQuickItem *surface)
{
    if (m_surface == surface) return;
    close();
    disconnect(m_screenConnection);
    disconnect(m_windowConnection);
    m_surface = surface;
    if (surface) {
        m_windowConnection = connect(surface, &QQuickItem::windowChanged,
                                     this, &HdrVideoOutput::refreshDisplay);
    }
    refreshDisplay();
    emit surfaceChanged();
}

void HdrVideoOutput::refreshDisplay()
{
    auto *window = m_surface ? m_surface->window() : nullptr;
    if (m_sourceWindow != window) {
        disconnect(m_screenConnection);
        m_sourceWindow = window;
        if (window) m_screenConnection = connect(window, &QWindow::screenChanged,
                                                 this, &HdrVideoOutput::refreshDisplay);
    }
    m_display = window && window->screen()
        ? queryHdrDisplayForWindow(window->winId()) : HdrDisplayState{};
    if (window) {
        const auto api = window->rendererInterface()->graphicsApi();
        if (api != QSGRendererInterface::Direct3D11 && api != QSGRendererInterface::Direct3D12)
            m_display.supported = false;
    }
    if (m_active && (m_display.adapterId != m_ownedDisplay.adapterId
                    || m_display.targetId != m_ownedDisplay.targetId)) close();
    emit changed();
}

void HdrVideoOutput::toggle()
{
    if (m_active || m_busy) { close(); return; }
    refreshDisplay();
    auto *source = m_surface
        ? qobject_cast<ViewerVideoFrameSource *>(m_surface->property("frameSource").value<QObject *>())
        : nullptr;
    if (!supported() || !source || !source->hdr()) return;
    const auto sourceFormat = source->snapshot().frame.surfaceFormat();
    qInfo() << "[HDR] source" << sourceFormat.pixelFormat() << sourceFormat.colorTransfer()
            << sourceFormat.colorSpace() << "display" << m_display.displayName;
    ++m_generation;
    m_error.clear();
    m_busy = true;
    m_closing = false;
    m_restoring = false;
    m_ownedDisplay = m_display;
    m_restoreDisplay = !m_display.enabled;
    emit changed();
    if (m_display.active) {
        createWindow();
    } else {
        const auto display = m_display;
        m_switch.setFuture(QtConcurrent::run([display] { return setHdrDisplayEnabled(display, true); }));
    }
}

void HdrVideoOutput::completeEnable(int attempt)
{
    if (!m_busy || m_restoring) return;
    if (m_closing) { close(); return; }
    const auto display = queryHdrDisplayState(m_ownedDisplay);
    if (display.active) { createWindow(); return; }
    if (attempt >= 30) {
        fail(tr("The display did not enter HDR mode."));
        return;
    }
    const auto generation = m_generation;
    QTimer::singleShot(100, this, [this, attempt, generation] {
        if (generation == m_generation) completeEnable(attempt + 1);
    });
}

void HdrVideoOutput::createWindow()
{
    if (!m_surface || !m_sourceWindow || m_closing) { close(); return; }
    auto *engine = qmlEngine(m_surface);
    if (!engine) { fail(tr("The HDR video surface is unavailable.")); return; }
    QQmlComponent component(engine, QUrl("qrc:/ZoinGallery/qml/GalleryHdrVideoWindow.qml"));
    auto *object = component.createWithInitialProperties({
        {"videoSurface", QVariant::fromValue(m_surface.data())},
        {"hdrOutput", QVariant::fromValue(this)},
    });
    auto *window = qobject_cast<QQuickWindow *>(object);
    if (!window) {
        delete object;
        fail(tr("Could not create the HDR video window: %1").arg(component.errorString()));
        return;
    }
    m_hdrWindow = window;
    window->QObject::setParent(this);
    window->setProperty("_qt_sg_hdr_format", QByteArrayLiteral("scrgb"));
    window->setScreen(m_sourceWindow->screen());
    // setScreen() does not move a window within the same virtual desktop.
    // Position the native surface before its first swapchain is created.
    window->setGeometry(m_sourceWindow->screen()->geometry());
    window->setTransientParent(m_sourceWindow);
    window->setTitle(m_sourceWindow->title() + QStringLiteral(" — HDR"));
    const auto verified = std::make_shared<std::atomic_bool>(false);
    connect(window, &QQuickWindow::beforeRenderPassRecording, this, [this, window, verified] {
        if (verified->exchange(true)) return;
        auto *swapchain = static_cast<QRhiSwapChain *>(window->rendererInterface()->getResource(
            window, QSGRendererInterface::RhiSwapchainResource));
        const bool hdr = swapchain && swapchain->format() == QRhiSwapChain::HDRExtendedSrgbLinear;
        QMetaObject::invokeMethod(this, [this, window, hdr] {
            if (m_hdrWindow != window || m_closing) return;
            if (!hdr) { fail(tr("The graphics backend could not create an HDR output.")); return; }
            m_active = true;
            m_busy = false;
            qInfo() << "[HDR] scRGB FP16 video output active on" << m_ownedDisplay.displayName;
            emit changed();
        }, Qt::QueuedConnection);
    }, Qt::DirectConnection);
    connect(window, &QWindow::visibleChanged, this, [this](bool visible) {
        if (!visible && !m_closing) close();
    });
    connect(window, &QWindow::visibilityChanged, this, [this](QWindow::Visibility visibility) {
        if (m_active && visibility != QWindow::FullScreen) close();
    });
    m_restoreFocus = true;
    window->showFullScreen();
    const auto nativeDisplay = queryHdrDisplayForWindow(window->winId());
    qInfo() << "[HDR] window target" << window->screen()->name() << window->geometry()
            << nativeDisplay.displayName << nativeDisplay.deviceName << "active" << nativeDisplay.active;
    window->requestActivate();
    const auto generation = m_generation;
    QTimer::singleShot(5000, this, [this, verified, generation] {
        if (generation == m_generation && m_busy && !m_closing && !verified->load())
            fail(tr("The HDR video output did not start."));
    });
}

void HdrVideoOutput::fail(const QString &message)
{
    m_error = message;
    close();
}

void HdrVideoOutput::navigate(int direction)
{
    m_pendingNavigation = direction < 0 ? -1 : 1;
    close();
}

void HdrVideoOutput::close()
{
    ++m_generation;
    m_closing = true;
    if (m_hdrWindow) {
        auto *window = m_hdrWindow.data();
        m_hdrWindow.clear();
        window->hide();
        window->deleteLater();
    }
    m_active = false;
    if (m_switch.isRunning()) return;
    if (m_restoreDisplay && !m_restoring) {
        m_restoring = true;
        m_busy = true;
        const auto display = m_ownedDisplay;
        m_switch.setFuture(QtConcurrent::run([display] { return setHdrDisplayEnabled(display, false); }));
        emit changed();
        return;
    }
    finishClose();
}

void HdrVideoOutput::finishClose()
{
    m_active = false;
    m_busy = false;
    m_restoring = false;
    m_closing = false;
    if (std::exchange(m_restoreFocus, false) && m_sourceWindow && m_sourceWindow->isVisible())
        m_sourceWindow->requestActivate();
    emit changed();
    const int direction = std::exchange(m_pendingNavigation, 0);
    if (direction) emit navigationRequested(direction);
}

QColor HdrVideoOutput::linearColor(const QColor &color) const
{
    const auto linear = [](float value) {
        return value <= .04045f ? value / 12.92f : std::pow((value + .055f) / 1.055f, 2.4f);
    };
    return QColor::fromRgbF(linear(color.redF()), linear(color.greenF()),
                           linear(color.blueF()), color.alphaF());
}
} // namespace ZoinGallery
