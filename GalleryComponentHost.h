#pragma once

#include <QPointer>
#include <QQmlComponent>
#include <QQuickItem>
#include <memory>

// A bounded, incremental renderer exchange. The ready item survives incubation.
class GalleryComponentHost : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QQmlComponent *sourceComponent READ sourceComponent WRITE setSourceComponent NOTIFY sourceComponentChanged)
    Q_PROPERTY(QQuickItem *item READ item NOTIFY itemChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(bool asynchronous READ asynchronous WRITE setAsynchronous NOTIFY asynchronousChanged)
    Q_PROPERTY(int status READ status NOTIFY statusChanged)
public:
    explicit GalleryComponentHost(QQuickItem *parent = nullptr);
    ~GalleryComponentHost() override;
    QQmlComponent *sourceComponent() const { return _source; }
    void setSourceComponent(QQmlComponent *component);
    QQuickItem *item() const { return _item; }
    bool active() const { return _active; }
    void setActive(bool active);
    bool asynchronous() const { return _asynchronous; }
    void setAsynchronous(bool asynchronous);
    int status() const { return _status; }
signals:
    void sourceComponentChanged();
    void itemChanged();
    void activeChanged();
    void asynchronousChanged();
    void statusChanged();
    void loaded();
protected:
    void componentComplete() override;
    void geometryChange(const QRectF &geometry, const QRectF &oldGeometry) override;
private:
    class Incubation;
    void start();
    void cancel();
    void discardItem();
    void complete(Incubation *incubation);
    void setStatus(int status);
    QPointer<QQmlComponent> _source;
    QPointer<QQuickItem> _item;
    std::shared_ptr<Incubation> _pending;
    QMetaObject::Connection _sourceStatusConnection;
    QMetaObject::Connection _sourceDestroyedConnection;
    bool _active = true;
    bool _asynchronous = true;
    bool _fitWidth = true;
    bool _fitHeight = true;
    int _status = 0;
    quint64 _generation = 0;
};
