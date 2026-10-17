#pragma once

#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QQuickItem>
#include <QRectF>
#include <QVariantMap>
#include <QtQml/qqml.h>
#include <cmath>
#include <memory>

class QQuickWindow;

namespace ZoinGallery {

void registerPixelGridTypes();

// Integer physical edges are the cached representation. Logical conversion is
// only needed when publishing geometry to Qt Quick, never to advance scrolling.
struct GridMetrics {
    qreal dpr = 1;
    explicit GridMetrics(qreal ratio = 1) : dpr(qMax<qreal>(.01, ratio)) {}
    qint64 edge(qreal logical) const { return qint64(std::round(logical * dpr)); }
    qint64 capacity(qreal logical) const { return qint64(std::ceil(logical * dpr - 1e-7)); }
    qreal logical(qint64 physical) const { return physical / dpr; }
    qreal snap(qreal value) const { return logical(edge(value)); }
    QPointF snap(QPointF point) const { return {snap(point.x()), snap(point.y())}; }
    QRectF rect(QRectF value) const { return {snap(value.topLeft()), snap(value.bottomRight())}; }
    qreal center(qreal outer, qreal inner) const {
        return logical(qint64(std::floor(qreal(edge(outer) - edge(inner)) / 2)));
    }
    // Cumulative edges distribute the remainder without gaps or accumulated drift.
    qreal partition(qreal extent, int index, int count) const {
        return count > 0 ? logical(edge(extent) * qint64(index) / count) : 0;
    }
};

class PixelGridAlignment;
class PixelGridScope;
class PixelGridScopeAttached;

class PixelGridContext final : public QObject {
    Q_OBJECT
    Q_PROPERTY(qreal dpr READ dpr NOTIFY metricsChanged)
    Q_PROPERTY(quint64 revision READ revision NOTIFY metricsChanged)
public:
    static PixelGridContext *forWindow(QQuickWindow *window);
    static void flushWindow(QQuickWindow *window);
    void invalidateScope(QQuickItem *item);
    explicit PixelGridContext(QQuickWindow *window);
    ~PixelGridContext() override;
    qreal dpr() const;
    quint64 revision() const;
    Q_INVOKABLE void flush();
    Q_INVOKABLE QVariantMap counters() const;
    Q_INVOKABLE void resetCounters();
    Q_INVOKABLE qreal snap(qreal value) const { return GridMetrics(dpr()).snap(value); }
    Q_INVOKABLE qreal capacity(qreal value) const { return GridMetrics(dpr()).logical(GridMetrics(dpr()).capacity(value)); }
    Q_INVOKABLE qreal center(qreal outer, qreal inner) const { return GridMetrics(dpr()).center(outer, inner); }
signals:
    void metricsChanged();
protected:
    bool eventFilter(QObject *object, QEvent *event) override;
private:
    friend class PixelGridAlignment;
    struct Private;
    std::unique_ptr<Private> d;
    void add(PixelGridAlignment *alignment);
    void remove(PixelGridAlignment *alignment);
    void invalidate(PixelGridAlignment *alignment);
};

class PixelGridAlignment : public QObject {
    Q_OBJECT
    Q_PROPERTY(QPointF offset READ offset NOTIFY offsetChanged)
    Q_PROPERTY(bool enabled READ isEnabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(ZoinGallery::PixelGridContext *grid READ grid NOTIFY gridChanged)
    QML_ATTACHED(PixelGridAlignment)
public:
    explicit PixelGridAlignment(QObject *parent = nullptr);
    ~PixelGridAlignment() override;
    static PixelGridAlignment *qmlAttachedProperties(QObject *object);
    static PixelGridAlignment *forItem(QQuickItem *item);
    QPointF offset() const { return m_offset; }
    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool enabled);
    Q_INVOKABLE void invalidate();
    QQuickItem *item() const { return m_item; }
    PixelGridContext *grid() const { return m_context; }
signals:
    void offsetChanged();
    void enabledChanged();
    void gridChanged();
private:
    friend class PixelGridContext;
    void attachWindow();
    void align();
    QPointer<QQuickItem> m_item;
    QPointer<QQuickItem> m_scope;
    QPointer<PixelGridContext> m_context;
    QPointF m_offset;
    bool m_enabled = true;
};

// This item is a coordinate boundary, not an offscreen layer. Its owned
// translation is deliberately absent from local layout invalidation.
class PixelGridScope : public QQuickItem {
    Q_OBJECT
    QML_ATTACHED(PixelGridScopeAttached)
    Q_PROPERTY(QPointF motionOffset READ motionOffset WRITE setMotionOffset NOTIFY motionOffsetChanged)
    Q_PROPERTY(QPointF appliedMotionOffset READ appliedMotionOffset NOTIFY appliedMotionOffsetChanged)
    Q_PROPERTY(bool motionActive READ motionActive WRITE setMotionActive NOTIFY motionActiveChanged)
    Q_PROPERTY(qreal gridDpr READ gridDpr NOTIFY gridDprChanged)
    Q_PROPERTY(bool tabFocus READ activeFocusOnTab WRITE setActiveFocusOnTab NOTIFY tabFocusChanged)
public:
    explicit PixelGridScope(QQuickItem *parent = nullptr);
    ~PixelGridScope() override;
    static PixelGridScopeAttached *qmlAttachedProperties(QObject *object);
    QPointF motionOffset() const { return m_motionOffset; }
    QPointF appliedMotionOffset() const { return m_appliedOffset; }
    bool motionActive() const { return m_motionActive; }
    qreal gridDpr() const;
    void setMotionOffset(QPointF offset);
    void setMotionActive(bool active);
    Q_INVOKABLE void invalidate();
protected:
    void itemChange(ItemChange change, const ItemChangeData &data) override;
signals:
    void motionOffsetChanged();
    void appliedMotionOffsetChanged();
    void motionActiveChanged();
    void gridDprChanged();
    void tabFocusChanged();
private:
    class Translation;
    Translation *m_translation;
    PixelGridAlignment *m_alignment;
    QPointF m_motionOffset;
    QPointF m_appliedOffset;
    bool m_motionActive = false;
    void updateTranslation();
};

// Adapt Qt-owned moving roots (Flickable/ListView contentItem) without
// replacing them, changing their bindings, or rasterizing their children.
class PixelGridScopeAttached : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool motionActive READ motionActive WRITE setMotionActive NOTIFY motionActiveChanged)
public:
    explicit PixelGridScopeAttached(QObject *parent);
    ~PixelGridScopeAttached() override;
    bool motionActive() const { return m_motionActive; }
    void setMotionActive(bool active);
    Q_INVOKABLE void invalidate();
signals:
    void motionActiveChanged();
private:
    class Translation;
    QPointer<QQuickItem> m_item;
    PixelGridAlignment *m_alignment = nullptr;
    Translation *m_translation = nullptr;
    bool m_motionActive = false;
    void updateTranslation();
private slots:
    void updateOwnerMotion();
};

} // namespace ZoinGallery

QML_DECLARE_TYPEINFO(ZoinGallery::PixelGridAlignment, QML_HAS_ATTACHED_PROPERTIES)
QML_DECLARE_TYPEINFO(ZoinGallery::PixelGridScope, QML_HAS_ATTACHED_PROPERTIES)
