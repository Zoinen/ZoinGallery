#ifndef MASONRYLAYOUT_H
#define MASONRYLAYOUT_H

#include <QQuickItem>
#include <QAbstractItemModel>

#include "MasonryLayoutQuickSearch.h"
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
    Q_PROPERTY(qreal contentY READ contentY WRITE setContentY NOTIFY contentYChanged)
    Q_PROPERTY(int contentHeight READ contentHeight NOTIFY contentHeightChanged)
    Q_PROPERTY(QAbstractItemModel *model READ model WRITE setModel NOTIFY modelChanged)
    Q_PROPERTY(QQmlComponent *delegate MEMBER _delegate)
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(int currentImageIndex READ currentImageIndex WRITE setCurrentImageIndex NOTIFY currentImageIndexChanged)
    Q_PROPERTY(QQuickItem *currentItem READ currentItem NOTIFY currentIndexChanged)
    Q_PROPERTY(int spacing READ spacing WRITE setSpacing NOTIFY spacingChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int imageCount READ imageCount NOTIFY imageCountChanged)
    Q_PROPERTY(MasonryLayoutQuickSearch *quickSearch READ quickSearch NOTIFY quickSearchChanged)
    Q_PROPERTY(bool needScroll READ needScroll NOTIFY needScrollChanged)
    Q_PROPERTY(bool listView READ listView WRITE setListView NOTIFY listViewChanged)

public:
    explicit MasonryLayout(QQuickItem *parent = nullptr);
    void componentComplete() override;

    Q_INVOKABLE QQuickItem *itemAt(qreal x, qreal y) const;
    Q_INVOKABLE int indexAt(qreal x, qreal y) const;
    Q_INVOKABLE QRectF indexGeometry(int index) const;
    Q_INVOKABLE QString indexImage(int index) const;
    Q_INVOKABLE int nextImageIndex(bool forward, bool moveToEnd);

    Q_INVOKABLE void reReadAndDecodeThumbnails();
    Q_INVOKABLE void zoomIn();
    Q_INVOKABLE void zoomOut();

    Q_INVOKABLE void setScrollingMode(bool scrollingMode, int direction = 0);

    int targetHeight() const;
    void setTargetHeight(int newTargetHeight);

    qreal contentY() const;
    void setContentY(qreal newContentY);

    int contentHeight() const;

    QAbstractItemModel *model() const;
    void setModel(QAbstractItemModel *newModel);

    int currentIndex() const;
    void setCurrentIndex(int newCurrentIndex);

    int spacing() const;
    void setSpacing(int newSpacing);

    QQuickItem *currentItem() const;

    int count() const;

    MasonryLayoutQuickSearch *quickSearch() const;

    bool needScroll() const;

    bool listView() const;
    void setListView(bool isListView);

    int imageCount() const;

    int currentImageIndex() const;
    void setCurrentImageIndex(int newCurrentImageIndex);

signals:
    void targetHeightChanged();
    void contentYChanged();
    void contentHeightChanged();
    void modelChanged();

    void currentIndexChanged();

    void spacingChanged();

    void countChanged();

    void layoutReset();

    void quickSearchChanged();

    void needScrollChanged();

    void listViewChanged();

    void imageCountChanged();

    void currentImageIndexChanged();

private slots:
    void onThumbnailReadFinished(ImageFile *root);

private:
    friend class MasonryLayoutQuickSearch;

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
        QSize thumbnailSize(int spacing) const;
    };

    BrickItem *createComponent();

    void rewrap();
    static qreal scaleRow(QList<MasonryBrick> &bricks, int canvasWidth, int rowTargetHeight, int spacing, int lastRowIndex, qreal rowHeight = 0);
    static void calcLayout(QList<MasonryBrick> &bricks, int canvasWidth, int rowTargetHeight, int spacing);
    void updateProperties();
    void setContentYInternal(qreal newContentY);

    void setContentHeight(int newContentHeight);

    void onDataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight, const QVector<int> &roles = QVector<int>());
    void pushToCurrentRow(int index);
    void onModelReset();
    void zoom(bool in);
    void updateNeedScroll();

    void pushBrickItem(BrickItem *item);
    BrickItem *popBrickItem();

    QSize dp(QSizeF value);
    qreal dp(qreal value);
    qreal dpValue();

    QSet<BrickItem *> _usedBrickItems;
    QSet<BrickItem *> _freeBrickItems;

    QAbstractItemModel *_model;

    QList<MasonryBrick> _bricks;
    QList<MasonryBrick> _currentLoadingRow;
    int _visibleStart;
    int _visibleEnd;
    int _topItem;
    int _topItemOffset;
    QQuickItem *_viewport;

    const QSizeF BrickSize = QSize(2, 3);
    const QSizeF BrickFolderSize = QSize(130, 3);
    const QSizeF BrickFolderViewSize = QSize(30, 3);

    int _targetHeight;
    qreal _contentY;
    int _contentHeight;
    QRect _lastViewportGeometry;
    QQmlComponent *_delegate;
    int _currentIndex;
    int _spacing;

    bool _currentScrollingMode;
    int _currentScrollingDirection;

    MasonryLayoutQuickSearch *_quickSearch;
    bool _needScroll;

    qreal _dp;
    bool _listView;
    int _imageCount;
    int _currentImageIndex;
};

#endif // MASONRYLAYOUT_H
