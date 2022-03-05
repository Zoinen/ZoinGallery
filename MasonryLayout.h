#ifndef MASONRYLAYOUT_H
#define MASONRYLAYOUT_H

#include <QQuickItem>


class BrickItem : public QQuickItem {
    Q_OBJECT
public:
    BrickItem(QQuickItem *parent = nullptr);

    void setGeometry(QRectF rect);

    // QQuickItem interface
protected:
    void geometryChanged(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    bool _isChangingGeometry;
};


class MasonryLayout : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(int targetHeight READ targetHeight WRITE setTargetHeight NOTIFY targetHeightChanged)
    Q_PROPERTY(int contentY READ contentY WRITE setContentY NOTIFY contentYChanged)
    Q_PROPERTY(int contentHeight READ contentHeight NOTIFY contentHeightChanged)

public:
    explicit MasonryLayout(QQuickItem *parent = nullptr);
    void componentComplete() override;

    int targetHeight() const;
    void setTargetHeight(int newTargetHeight);

    int contentY() const;
    void setContentY(int newContentY);

    int contentHeight() const;

signals:
    void targetHeightChanged();
    void contentYChanged();
    void contentHeightChanged();

private:
    BrickItem *createComponent();

    void rewrap();
    void updateProperties();

    void setContentHeight(int newContentHeight);

    struct Pic {
        QSizeF originalSize;
        QSizeF normalizedSize;
        qreal x;
        qreal y;
        int row;
        int column;
        BrickItem *item;

        Pic(int width, int height) : originalSize(QSize(width, height)), x(0), y(0), row(0), column(0), item(nullptr) {
        }
    };
    QList<Pic> _pics;
    int _targetHeight;
    int _contentY;
    int _contentHeight;
};

#endif // MASONRYLAYOUT_H
