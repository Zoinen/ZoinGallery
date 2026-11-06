#pragma once

#include <QtQuick/private/qquickanimation_p.h>
#include <memory>

class MasonryLayout;
struct GalleryScrollAnimationState;

// NumberAnimation-compatible owner for native panel motion. Native rollout is
// explicit until panel chrome and the complete layout lifecycle are verified.
class GalleryScrollAnimation : public QQuickNumberAnimation {
    Q_OBJECT
    Q_PROPERTY(bool nativeEnabled READ nativeEnabled WRITE setNativeEnabled
               NOTIFY nativeEnabledChanged)
    Q_PROPERTY(bool nativeRunning READ nativeRunning NOTIFY nativeRunningChanged)
    Q_PROPERTY(bool continuousRunning READ continuousRunning NOTIFY nativeRunningChanged)
    Q_PROPERTY(QQuickItem *scrollBar READ scrollBar WRITE setScrollBar NOTIFY scrollBarChanged)
public:
    explicit GalleryScrollAnimation(QObject *parent = nullptr);
    ~GalleryScrollAnimation() override;
    bool nativeEnabled() const { return _nativeEnabled; }
    void setNativeEnabled(bool enabled);
    QQuickItem *scrollBar() const { return _scrollBar; }
    void setScrollBar(QQuickItem *scrollBar);
    bool nativeRunning() const { return bool(_native); }
    quint64 nativeJobStarts() const { return _nativeJobStarts; }
    bool continuousRunning() const;
    Q_INVOKABLE bool setContinuousVelocity(qreal velocity);
    Q_INVOKABLE void stopContinuous();
    Q_INVOKABLE void retarget(int edgeDirection = 0);
    Q_INVOKABLE void retargetPage();
    Q_INVOKABLE bool rebasingPage() const;
    ThreadingModel threadingModel() const override;

signals:
    void nativeEnabledChanged();
    void nativeRunningChanged();
    void scrollBarChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    QAbstractAnimationJob *transition(QQuickStateActions &, QQmlProperties &,
        TransitionDirection, QObject *defaultTarget = nullptr) override;

private slots:
    void syncNativePose();
    void prepareNativeStop();

private:
    bool canUseNative() const;
    bool canUseNative(qreal destination) const;
    void retargetMotion(int edgeDirection, bool page);
    void finishNative(bool commitPose);
    void cancelNative(bool commitPose);
    void beginGeometryChange();
    void endGeometryChange();
    void completeAtDestination();
    bool _nativeEnabled = false;
    bool _continuousRequested = false;
    bool _pageRequested = false;
    int _edgeDirectionRequested = 0;
    qreal _continuousVelocity = 0;
    bool _commitOnStop = true;
    quint64 _nativeJobStarts = 0;
    bool _geometryChanging = false;
    qreal _geometryStartPose = 0;
    int _geometryPageTargetIndex = -1;
    qreal _geometryPageTargetInset = 0;
    QPointer<QQuickItem> _scrollBar;
    QMetaObject::Connection _scrollBarDestroyed;
    std::shared_ptr<GalleryScrollAnimationState> _native;
    QList<QMetaObject::Connection> _nativeConnections;
};
