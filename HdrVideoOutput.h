#pragma once

#include "HdrDisplay.h"
#include <QFutureWatcher>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtQmlIntegration/qqmlintegration.h>
#include <functional>

namespace ZoinGallery {

class HdrVideoOutput : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQuickItem *surface READ surface WRITE setSurface NOTIFY surfaceChanged)
    Q_PROPERTY(bool supported READ supported NOTIFY changed)
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString displayName READ displayName NOTIFY changed)
public:
    explicit HdrVideoOutput(QObject *parent = nullptr);
    ~HdrVideoOutput() override;
    QQuickItem *surface() const { return m_surface; }
    void setSurface(QQuickItem *surface);
    bool supported() const { return m_display.supported; }
    bool active() const { return m_active; }
    bool busy() const { return m_busy; }
    QString error() const { return m_error; }
    QString displayName() const { return m_display.displayName; }
    Q_INVOKABLE void toggle();
    Q_INVOKABLE void close();
    Q_INVOKABLE void navigate(int direction);
    Q_INVOKABLE QColor linearColor(const QColor &color) const;
signals:
    void surfaceChanged();
    void changed();
    void navigationRequested(int direction);
private:
    void refreshDisplay();
    void createWindow();
    void completeEnable(int attempt = 0);
    void fail(const QString &message);
    void finishClose();
    QPointer<QQuickItem> m_surface;
    QPointer<QQuickWindow> m_sourceWindow;
    QPointer<QQuickWindow> m_hdrWindow;
    QMetaObject::Connection m_screenConnection;
    QMetaObject::Connection m_windowConnection;
    QFutureWatcher<int> m_switch;
    HdrDisplayState m_display;
    HdrDisplayState m_ownedDisplay;
    bool m_restoreDisplay = false;
    bool m_active = false;
    bool m_busy = false;
    bool m_closing = false;
    bool m_restoring = false;
    bool m_restoreFocus = false;
    int m_pendingNavigation = 0;
    quint64 m_generation = 0;
    QString m_error;
};
} // namespace ZoinGallery
