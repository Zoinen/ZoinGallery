#ifndef MASONRYLAYOUT_H
#define MASONRYLAYOUT_H

#include <QQuickItem>
#include <QAbstractItemModel>
#include <QHash>
#include <QFont>
#include <QSet>
#include <QVariantList>
#include <QVariantMap>

#include "MasonryLayoutQuickSearch.h"
#include "ImageFile.h"

class QParallelAnimationGroup;
class QPropertyAnimation;


class BrickItem : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(int row READ row NOTIFY rowColumnChanged)
    Q_PROPERTY(int column READ column NOTIFY rowColumnChanged)
    Q_PROPERTY(QRectF previewRect READ previewRect NOTIFY previewRectChanged)
    Q_PROPERTY(QString iconLabelText READ iconLabelText NOTIFY iconLabelTextChanged)
public:
    BrickItem(QQuickItem *parent = nullptr);

    void setRowColumn(int row, int column);
    void setGeometry(QRectF rect, bool animate, bool snapToLogicalPixels = true);
    void setPreviewRect(const QRectF &previewRect);
    void setIconLabelText(const QString &text);
    QRectF geometry() const;
    QRectF previewRect() const;
    QString iconLabelText() const;
    void stopGeometryAnimation();

    int row() const;
    int column() const;

signals:
    void rowColumnChanged();
    void previewRectChanged();
    void iconLabelTextChanged();

protected:
#if QT_VERSION < QT_VERSION_CHECK(6, 3, 0)
    void geometryChanged(const QRectF &newGeometry, const QRectF &oldGeometry) override;
#else
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry);
#endif
    void animateToRect(const QRectF &rect);

private:
    bool _isChangingGeometry;
    int _row;
    int _column;
    QRectF _previewRect;
    QString _iconLabelText;

    QParallelAnimationGroup *_geometryAnimationGroup;
    QPropertyAnimation *_xAnimation;
    QPropertyAnimation *_yAnimation;
    QPropertyAnimation *_widthAnimation;
    QPropertyAnimation *_heightAnimation;
};


class MasonryLayout : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(PresentationMode presentationMode READ presentationMode WRITE setPresentationMode NOTIFY presentationModeChanged)
    Q_PROPERTY(int columnCount READ columnCount WRITE setColumnCount NOTIFY columnCountChanged)
    Q_PROPERTY(qreal density READ density WRITE setDensity NOTIFY densityChanged)
    Q_PROPERTY(int targetExtent READ targetExtent WRITE setTargetExtent NOTIFY targetExtentChanged)
    Q_PROPERTY(int windowTopIndex READ windowTopIndex WRITE setWindowTopIndex NOTIFY windowTopIndexChanged)
    Q_PROPERTY(QVariantList visibleIndexes READ visibleIndexes NOTIFY visibleIndexesChanged)
    Q_PROPERTY(QVariantList overscanIndexes READ overscanIndexes NOTIFY overscanIndexesChanged)
    Q_PROPERTY(QVariantList layoutBands READ layoutBands NOTIFY layoutBandsChanged)
    Q_PROPERTY(quint64 layoutRevision READ layoutRevision NOTIFY layoutRevisionChanged)
    Q_PROPERTY(int targetHeight READ targetHeight WRITE setTargetHeight NOTIFY targetHeightChanged)
    Q_PROPERTY(qreal contentY READ contentY WRITE setContentY NOTIFY contentYChanged)
    Q_PROPERTY(qreal contentHeight READ contentHeight NOTIFY contentHeightChanged)
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
    Q_PROPERTY(bool showTransparentGrid READ showTransparentGrid WRITE setShowTransparentGrid NOTIFY showTransparentGridChanged)
    Q_PROPERTY(bool animateResizing READ animateResizing WRITE setAnimateResizing NOTIFY animateResizingChanged)
    Q_PROPERTY(int listRowHeight READ listRowHeight NOTIFY listRowHeightChanged)
    Q_PROPERTY(QVariantList currentImageExif READ currentImageExif NOTIFY currentIndexChanged)
    Q_PROPERTY(QQuickItem *viewport READ viewport NOTIFY viewportChanged)
    Q_PROPERTY(bool persistSettings MEMBER _persistSettings)
    Q_PROPERTY(qreal devicePixelRatio READ devicePixelRatio WRITE setDevicePixelRatio NOTIFY devicePixelRatioChanged)
    Q_PROPERTY(QFont iconLabelFont READ iconLabelFont WRITE setIconLabelFont NOTIFY iconLabelFontChanged)

    Q_PROPERTY(qreal paddingLeft READ paddingLeft WRITE setPaddingLeft NOTIFY paddingLeftChanged)
    Q_PROPERTY(qreal paddingRight READ paddingRight WRITE setPaddingRight NOTIFY paddingRightChanged)
    Q_PROPERTY(qreal paddingTop READ paddingTop WRITE setPaddingTop NOTIFY paddingTopChanged)
    Q_PROPERTY(qreal paddingBottom READ paddingBottom WRITE setPaddingBottom NOTIFY paddingBottomChanged)

public:
    enum PresentationMode {
        Masonry = 0,
        Columns,
        Details,
        Grid,
        Icons,
    };
    Q_ENUM(PresentationMode)

    enum NavigationDirection {
        NavigateLeft = 0,
        NavigateRight,
        NavigateUp,
        NavigateDown,
    };
    Q_ENUM(NavigationDirection)

    explicit MasonryLayout(QQuickItem *parent = nullptr);
    void componentComplete() override;

    Q_INVOKABLE QQuickItem *itemAt(qreal x, qreal y) const;
    Q_INVOKABLE int indexAt(qreal x, qreal y) const;
    Q_INVOKABLE int indexAtViewport(qreal x, qreal y) const;
    Q_INVOKABLE QVariantList indexesInViewportRect(qreal x, qreal y, qreal width, qreal height) const;
    Q_INVOKABLE QRectF indexGeometry(int index) const;
    Q_INVOKABLE QRectF indexPreviewGeometry(int index) const;
    Q_INVOKABLE QString indexImageIdUrl(int index) const;
    Q_INVOKABLE QString indexText(int index) const;
    Q_INVOKABLE QString indexFullPath(int index) const;
    Q_INVOKABLE QSize indexOriginalSize(int index) const;
    Q_INVOKABLE QVariantMap indexExif(int index) const;
    Q_INVOKABLE int nextImageIndex(bool forward, bool moveToEnd);
    Q_INVOKABLE int neighborIndex(int index, NavigationDirection direction) const;
    Q_INVOKABLE int pageIndex(int index, NavigationDirection direction) const;
    Q_INVOKABLE QVariantMap navigationTarget(
        int index, NavigationDirection direction, bool page = false) const;
    Q_INVOKABLE QVariantMap masonryPagePlan(
        int currentIndex, qreal anchorX, qreal itemViewportY,
        qreal plannedContentY, qreal rowViewportY, int direction,
        qreal preferredDistance) const;
    Q_INVOKABLE int windowTopIndexForIndex(int index) const;

    Q_INVOKABLE void preserveCurrentItemPositionForNextModelReset();
    Q_INVOKABLE void reReadAndDecodeThumbnails();
    Q_INVOKABLE void zoomIn();
    Q_INVOKABLE void zoomOut();

    Q_INVOKABLE void setScrollingMode(bool scrollingMode, int direction = 0);

    PresentationMode presentationMode() const;
    void setPresentationMode(PresentationMode mode);

    int columnCount() const;
    void setColumnCount(int columnCount);

    qreal density() const;
    void setDensity(qreal density);

    int targetExtent() const;
    void setTargetExtent(int targetExtent);

    int windowTopIndex() const;
    void setWindowTopIndex(int index);

    QVariantList visibleIndexes() const;
    QVariantList overscanIndexes() const;
    QVariantList layoutBands() const;
    quint64 layoutRevision() const;

    int targetHeight() const;
    void setTargetHeight(int newTargetHeight);

    qreal contentY() const;
    void setContentY(qreal newContentY);

    qreal contentHeight() const;

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

    bool showTransparentGrid() const;
    void setShowTransparentGrid(bool newShowTransparentGrid);

    bool animateResizing() const;
    void setAnimateResizing(bool newAnimateResizing);

    qreal paddingLeft() const;
    void setPaddingLeft(qreal newPaddingLeft);

    qreal paddingRight() const;
    void setPaddingRight(qreal newPaddingRight);

    qreal paddingTop() const;
    void setPaddingTop(qreal newPaddingTop);

    qreal paddingBottom() const;
    void setPaddingBottom(qreal newPaddingBottom);

    qreal width() const;
    int listRowHeight() const;
    QVariantList currentImageExif() const;

    QQuickItem *viewport() const;

    qreal devicePixelRatio() const;
    void setDevicePixelRatio(qreal value);
    QFont iconLabelFont() const;
    void setIconLabelFont(const QFont &font);

signals:
    void presentationModeChanged();
    void columnCountChanged();
    void densityChanged();
    void targetExtentChanged();
    void windowTopIndexChanged();
    void visibleIndexesChanged();
    void overscanIndexesChanged();
    void layoutBandsChanged();
    void layoutRevisionChanged();
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

    void showTransparentGridChanged();

    void animateResizingChanged();

    void paddingLeftChanged();

    void paddingRightChanged();

    void paddingTopChanged();

    void paddingBottomChanged();

    void listRowHeightChanged();

    void viewportChanged();
    void devicePixelRatioChanged();
    void iconLabelFontChanged();

private slots:
    // ZZ: This should be done in embedded view every time
    void onThumbnailReadFinished(bool animate = true);

private:
    friend class MasonryLayoutQuickSearch;

    struct MasonryBrick {
        QSizeF originalSize;
        bool temporaryLineBreakAfter = false;
        bool lineBreakAfter = false;

        QSizeF normalizedSize;
        qreal x = 0;
        qreal y = 0;
        int row = 0;
        int column = 0;
        BrickItem *item = nullptr;
        ImageFile *image = nullptr;
        int globalIndex = -1;
        QRectF previewGeometry;
        QString iconLabelText;
        QSize lastPlannedTargetSize;
        QString lastPlannedTransformKey;
        QString lastPlannedSourcePath;
        QDateTime lastPlannedModified;
        qint64 lastPlannedFileSize = -1;
        qint64 lastPlannedVersionToken = 0;

        QRectF geometry() const;
        QSize thumbnailSize(int spacing) const;
    };

    struct LayoutBand {
        int row = -1;
        qreal top = 0;
        qreal bottom = 0;
        QList<int> indexes;
    };

    BrickItem *createComponent();

    bool isEmbedded() const;
    void rewrap(bool animate = true);
    void calcFixedLayout();
    void rebuildLayoutBands();
    QList<int> indexesForVerticalRange(qreal top, qreal bottom) const;
    void positionViewport();
    qreal maximumContentOffset() const;
    qreal viewportExtent() const;
    int bandIndexAt(qreal y) const;
    int effectiveColumnCount() const;
    int rowsPerColumn() const;
    qreal effectiveTargetExtent() const;
    int maximumWindowTopIndex() const;
    qreal contentYForWindowTopIndex(int index) const;
    int windowTopIndexForContentY(qreal contentY) const;
    void updateWindowTopFromContentY();
    void updateViewportIndexSets();
    void planViewportThumbnails(const QSet<int> &candidateIndexes,
                                bool force = false);
    void planThumbnailForIndex(int index, bool highPriority,
                               QList<ImageDecodeRequest> &requests,
                               bool force = false);
    QSize previewDecodeTargetSize(const MasonryBrick &brick,
                                  const QSizeF &previewBounds);
    QString previewTransformKey() const;
    static QString presentationModeSettingsName(PresentationMode mode);
    static qreal normalizedDensity(PresentationMode mode, qreal density);
    static qreal scaleRow(QList<MasonryBrick> &bricks, int canvasWidth, int rowTargetHeight, int spacing, int lastRowIndex, qreal rowHeight = 0);
    enum CalcLayoutMode {
        CalcLayoutMasonry,
        CalcLayoutSingleRow,
        CalcLayoutGrid
    };
    CalcLayoutMode layoutMode() const;
    static void calcGridLayout(QList<MasonryBrick> &bricks, int canvasWidth, int rowTargetHeight, int spacing,
                           bool lastRowMatchesPrevious, qreal paddingTop);
    static void calcLayout(QList<MasonryBrick> &bricks, int canvasWidth, int rowTargetHeight, int spacing,
                           bool lastRowMatchesPrevious, qreal paddingTop, CalcLayoutMode layoutMode);
    void updateProperties(bool animate = false);
    void setContentYInternal(qreal newContentY);
    void restorePreservedCurrentItemPosition();
    void preservePendingThumbnailRequestsForModelReset();
    void restorePendingThumbnailRequestsAfterModelReset();

    void setContentHeight(qreal newContentHeight);
    void updateCurrentImageIndex();

    void onDataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight, const QVector<int> &roles = QVector<int>());
    void pushToCurrentRow(int index, bool animate = true);
    MasonryBrick brickForImage(ImageFile *imageFile) const;
    void prepareForIncrementalModelChange();
    void applyIncrementalModelChange();
    void onModelAboutToBeReset();
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
    QSet<int> _activeBrickIndexes;

    QAbstractItemModel *_model;

    QList<MasonryBrick> _bricks;
    QList<MasonryBrick> _currentLoadingRow;
    QList<LayoutBand> _layoutBands;
    quint64 _layoutRevision = 0;
    QSet<int> _visibleIndexSet;
    QSet<int> _overscanIndexSet;
    QSet<int> _scheduledThumbnailIndexes;
    QSet<QString> _scheduledThumbnailRequestKeys;
    QSet<int> _lastThumbnailViewportIndexes;
    bool _cancelingThumbnailPlan = false;
    bool _catalogMetadataRequested = false;
    int _visibleStart;
    int _visibleEnd;
    int _topItem;
    qreal _topItemOffset;
    qreal _currentIndexOffsetOverride;
    QQuickItem *_viewport;

    const QSizeF GridView_Folder = QSizeF(1, 1);

    int _targetHeight;
    PresentationMode _presentationMode = Masonry;
    int _columnCount = 2;
    qreal _density = 150.0;
    int _windowTopIndex = 0;
    bool _preserveViewportAnchorForNextRewrap = false;
    qreal _modeDensities[5] = {150.0, 30.0, 30.0, 160.0, 128.0};
    qreal _contentY;
    qreal _contentHeight;
    QRect _lastViewportGeometry;
    QQmlComponent *_delegate;
    int _currentIndex;
    int _spacing;

    bool _currentScrollingMode;
    int _currentScrollingDirection;

    MasonryLayoutQuickSearch *_quickSearch;
    bool _needScroll;

    qreal _dp;
    qreal _devicePixelRatioOverride = 0;
    QFont _iconLabelFont;
    bool _listView;
    int _imageCount;
    int _currentImageIndex;
    bool _showTransparentGrid;
    bool _animateResizing;
    bool _persistSettings = true;
    int _listRowHeight;

    qreal _paddingLeft;
    qreal _paddingRight;
    qreal _paddingTop;
    qreal _paddingBottom;

    bool _preserveCurrentItemPositionOnNextModelReset;
    QString _preservedCurrentItemFullPath;
    int _preservedCurrentFallbackIndex;
    QString _preservedViewportAnchorFullPath;
    int _preservedViewportAnchorFallbackIndex;
    qreal _preservedViewportAnchorOffset;
    QHash<QString, ImageInfo> _preservedPendingThumbnailInfo;
    bool _preserveDecodeQueueForCurrentRebuild = false;
    bool _skipThumbnailBackfillUntilFlush = false;
    int _incrementalModelChangeDepth = 0;
};

#endif // MASONRYLAYOUT_H
