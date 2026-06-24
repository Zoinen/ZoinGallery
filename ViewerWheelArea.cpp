#include "ViewerWheelArea.h"

#include "ViewerWheelAreaNative.h"

#include <QCoreApplication>
#include <QInputDevice>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QQmlEngine>

static void registerViewerWheelAreaQmlType()
{
    qmlRegisterType<ViewerWheelArea>("ZoinGallery", 1, 0, "ViewerWheelArea");
}

Q_COREAPP_STARTUP_FUNCTION(registerViewerWheelAreaQmlType)

ViewerWheelArea::ViewerWheelArea(QQuickItem *parent)
    : QQuickItem(parent)
{
    setAcceptedMouseButtons(Qt::AllButtons);
    setAcceptHoverEvents(true);
}

void ViewerWheelArea::mousePressEvent(QMouseEvent *event)
{
    event->ignore();
}

void ViewerWheelArea::mouseMoveEvent(QMouseEvent *event)
{
    event->ignore();
}

void ViewerWheelArea::mouseReleaseEvent(QMouseEvent *event)
{
    event->ignore();
}

void ViewerWheelArea::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers() == Qt::ControlModifier || (event->buttons() & Qt::LeftButton)) {
        emit wheelForwarded();
        event->ignore();
        return;
    }

    const QPoint pixelDelta = event->pixelDelta();
    const QPoint angleDelta = event->angleDelta();
    const QInputDevice *eventDevice = event->device();
    const ViewerWheelNativeInfo nativeInfo = currentViewerWheelNativeInfo();

    emit wheelReceived(pixelDelta.x(),
                       pixelDelta.y(),
                       angleDelta.x(),
                       angleDelta.y(),
                       event->phase(),
                       int(event->modifiers()),
                       int(event->buttons()),
                       event->hasPixelDelta(),
                       event->inverted(),
                       int(event->source()),
                       eventDevice ? int(eventDevice->type()) : int(QInputDevice::DeviceType::Unknown),
                       nativeInfo.momentum,
                       nativeInfo.valid ? nativeInfo.phase : 0,
                       nativeInfo.valid ? nativeInfo.momentumPhase : 0);

    event->accept();
}
