#ifndef VIEWERWHEELAREA_H
#define VIEWERWHEELAREA_H

#include <QQuickItem>
#include <Qt>

class QWheelEvent;
class QMouseEvent;

class ViewerWheelArea : public QQuickItem
{
    Q_OBJECT

public:
    enum ScrollPhase {
        NoScrollPhase = Qt::NoScrollPhase,
        ScrollBegin = Qt::ScrollBegin,
        ScrollUpdate = Qt::ScrollUpdate,
        ScrollEnd = Qt::ScrollEnd,
        ScrollMomentum = Qt::ScrollMomentum
    };
    Q_ENUM(ScrollPhase)

    explicit ViewerWheelArea(QQuickItem *parent = nullptr);

signals:
    void wheelForwarded();
    void wheelReceived(qreal pixelDeltaX,
                       qreal pixelDeltaY,
                       qreal angleDeltaX,
                       qreal angleDeltaY,
                       int phase,
                       int modifiers,
                       int buttons,
                       bool hasPixelDelta,
                       bool inverted,
                       int source,
                       int deviceType,
                       bool nativeMomentum,
                       int nativePhase,
                       int nativeMomentumPhase);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
};

#endif // VIEWERWHEELAREA_H
