#ifndef MASONRYLAYOUT_H
#define MASONRYLAYOUT_H

#include <QQuickItem>
#include <QAbstractListModel>
#include "ImageFile.h"

class BrickItem : public QQuickItem {
    Q_OBJECT
public:
    BrickItem(QQuickItem *parent = nullptr);

    void setRowColumn(int row, int column);
    void setGeometry(QRectF rect, bool instantMove);
    QRectF geometry() const;

    int row() const;
    int column() const;

    // QQuickItem interface
protected:
#if QT_VERSION < QT_VERSION_CHECK(6, 3, 0)
    void geometryChanged(const QRectF &newGeometry, const QRectF &oldGeometry) override;
#else
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry);
#endif

private:
    bool _isChangingGeometry;
    int _row;
    int _column;
};


class MasonryLayout : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(int targetHeight READ targetHeight WRITE setTargetHeight NOTIFY targetHeightChanged)
    Q_PROPERTY(int contentY READ contentY WRITE setContentY NOTIFY contentYChanged)
    Q_PROPERTY(int contentHeight READ contentHeight NOTIFY contentHeightChanged)
    Q_PROPERTY(QAbstractListModel *model READ model WRITE setModel NOTIFY modelChanged)
    Q_PROPERTY(QQmlComponent *delegate MEMBER _delegate)
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(QQuickItem *currentItem READ currentItem NOTIFY currentIndexChanged)
    Q_PROPERTY(int spacing READ spacing WRITE setSpacing NOTIFY spacingChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    explicit MasonryLayout(QQuickItem *parent = nullptr);
    void componentComplete() override;

    Q_INVOKABLE QQuickItem *itemAt(qreal x, qreal y) const;
    Q_INVOKABLE int indexAt(qreal x, qreal y) const;
    Q_INVOKABLE QRectF indexGeometry(int index) const;

    Q_INVOKABLE void reReadAndDecodeThumbnails();

    int targetHeight() const;
    void setTargetHeight(int newTargetHeight);

    int contentY() const;
    void setContentY(int newContentY);

    int contentHeight() const;

    QAbstractListModel *model() const;
    void setModel(QAbstractListModel *newModel);

    int currentIndex() const;
    void setCurrentIndex(int newCurrentIndex);

    static int spacing();
    void setSpacing(int newSpacing);

    QQuickItem *currentItem() const;

    int count() const;

signals:
    void targetHeightChanged();
    void contentYChanged();
    void contentHeightChanged();
    void modelChanged();

    void currentIndexChanged();

    void spacingChanged();

    void countChanged();

private:
    struct MasonryBrick {
        QSizeF originalSize;
        QSizeF normalizedSize;
        qreal x;
        qreal y;
        int row;
        int column;
        bool lastInRow;
        BrickItem *item;
        ImageFile *image;
        int globalIndex;
        bool forceNewLine;

        MasonryBrick(int width, int height);
        MasonryBrick(ImageFile *image_, QSizeF originalSize_);
        QRectF geometry() const;
        QSize thumbnailSize() const;
    };

    BrickItem *createComponent();

    void rewrap();
    static void calcLayout(QList<MasonryBrick> &bricks, int canvasWidth, int rowTargetHeight, int spacing);
    void updateProperties();
    void setContentYInternal(int newContentY);

    void setContentHeight(int newContentHeight);

    void onDataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight, const QVector<int> &roles = QVector<int>());
    void pushToCurrentRow(int index);
    void onThumbnailReadFinished();
    void onModelReset();

    void pushBrickItem(BrickItem *item);
    BrickItem *popBrickItem();
    QSet<BrickItem *> _usedBrickItems;
    QSet<BrickItem *> _freeBrickItems;

    QAbstractListModel *_model;

    QList<MasonryBrick> _bricks;
    QList<MasonryBrick> _currentLoadingRow;
    int _visibleStart;
    int _visibleEnd;
    int _topItem;
    int _topItemOffset;
    QQuickItem *_viewport;

    int _targetHeight;
    int _contentY;
    int _contentHeight;
    QRect _lastViewportGeometry;
    QQmlComponent *_delegate;
    int _currentIndex;
    static int _spacing;
};

#endif // MASONRYLAYOUT_H
