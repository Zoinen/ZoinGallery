#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QQuickWindow>

class MainWindow : public QQuickWindow {
    Q_OBJECT
    Q_PROPERTY(bool isResizing READ isResizing NOTIFY isResizingChanged)
    Q_PROPERTY(qreal dpr MEMBER _dpr NOTIFY dprChanged)
    Q_PROPERTY(bool isQWK READ isQWK CONSTANT)

public:
    MainWindow(QWindow *parent = nullptr);

    bool event(QEvent *event) override;

    Q_INVOKABLE int availableScreenHeight() const;
    Q_INVOKABLE void toggleFullscreen();
    Q_INVOKABLE void setMousePos(QPoint pos) const;
    Q_INVOKABLE QPointF mousePos() const;
    Q_INVOKABLE bool isPressedOnTitleBar() const;
    Q_INVOKABLE void setSphereScrollingMouseCursor(bool set, bool idle = false, qreal rotation = 0);
    Q_INVOKABLE void showAndActivate();

    bool isResizing() const;

    bool isQWK() const;

signals:
    void mainWindowResized(bool widthChanged);
    void fullScreenEntered();
    void isResizingChanged();
    void dprChanged();
    void windowIsReady();

protected:
    void showEvent(QShowEvent *event) override;

private:
    void updateDpr();
    void enableWindowAnimations(bool enable);

    bool _leftButtonPressed;
    QSize _lastSize;
    QRect _normalGeometry;
    qreal _dpr;
    QWindow::Visibility _lastVisibleVisibility;
    bool _ignoreNormalGeometryChange;
    bool _enableNormalGeometryChangeOnNextExpose;
    QTimer *_delayedNormalGeometryChangeEnabler;
};

#endif // MAINWINDOW_H
