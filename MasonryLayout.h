#ifndef MASONRYLAYOUT_H
#define MASONRYLAYOUT_H

#include <QQuickItem>
#include <QAbstractItemModel>
#include <QHash>
#include <QFont>
#include <QPointer>
#include <QSet>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include <atomic>

#include "GalleryDelegateItem.h"
#include "GalleryDelegatePool.h"
#include "GalleryGeometryIndex.h"
#include "GalleryLayoutEngine.h"
#include "GalleryViewportMaterializer.h"
#include "GalleryThumbnailPlanner.h"
#include "MasonryLayoutQuickSearch.h"
#include "ImageFile.h"


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
    // A lightweight diagnostic counter also gives tests a deterministic way
    // to reject presentation changes which populate a disposable viewport
    // before committing the final anchored one.
    Q_PROPERTY(quint64 delegateCommitRevision READ delegateCommitRevision)
    Q_PROPERTY(int targetHeight READ targetHeight WRITE setTargetHeight NOTIFY targetHeightChanged)
    Q_PROPERTY(qreal contentY READ contentY WRITE setContentY NOTIFY contentYChanged)
    Q_PROPERTY(qreal contentHeight READ contentHeight NOTIFY contentHeightChanged)
    Q_PROPERTY(QAbstractItemModel *model READ model WRITE setModel NOTIFY modelChanged)
    Q_PROPERTY(QQmlComponent *delegate MEMBER _delegate)
    Q_PROPERTY(QQmlComponent *masonryDelegate MEMBER _masonryDelegate)
    Q_PROPERTY(QQmlComponent *columnsDelegate MEMBER _columnsDelegate)
    Q_PROPERTY(QQmlComponent *detailsDelegate MEMBER _detailsDelegate)
    Q_PROPERTY(QQmlComponent *gridDelegate MEMBER _gridDelegate)
    Q_PROPERTY(QQmlComponent *iconsDelegate MEMBER _iconsDelegate)
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
    // Embedders which hide a path-replacement transaction may stage delegate
    // rebinding in the polish pass. Geometry, hit testing and the current model
    // still change synchronously; only QML visual-slot population moves to the
    // guaranteed pre-render phase.
    Q_PROPERTY(bool deferDelegateRefreshOnReset
               READ deferDelegateRefreshOnReset
               WRITE setDeferDelegateRefreshOnReset
               NOTIFY deferDelegateRefreshOnResetChanged)

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
    // Exact logical-pixel pitch used by the fixed Columns strip. Exposing
    // the layout-owned value keeps keyboard viewport quantization on the same
    // device-pixel lattice as delegate geometry.
    Q_INVOKABLE qreal columnStride() const;

    Q_INVOKABLE void preserveCurrentItemPositionForNextModelReset();
    Q_INVOKABLE void reReadAndDecodeThumbnails();
    Q_INVOKABLE void zoomIn();
    Q_INVOKABLE void zoomOut();

    Q_INVOKABLE void setScrollingMode(bool scrollingMode, int direction = 0);
    // A presentation switch changes the native strategy, density, insets and
    // often the anchored viewport geometry in one semantic operation.  Hosts
    // can bracket those declarative writes so every setter is committed by
    // one final rewrap instead of exposing intermediate layouts.
    Q_INVOKABLE void beginLayoutUpdate();
    Q_INVOKABLE void endLayoutUpdate();
    // Stage the destination cursor and viewport before an external model
    // reset. The reset then lays out only the final visible/overscan window
    // instead of first populating delegates at the previous path's offset.
    Q_INVOKABLE void prepareViewportForModelReset(
        int cursorIndex, qreal savedOffset, bool restoreSavedOffset);
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
    quint64 delegateCommitRevision() const;

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
    bool deferDelegateRefreshOnReset() const;
    void setDeferDelegateRefreshOnReset(bool defer);

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
    void deferDelegateRefreshOnResetChanged();

private slots:
    // ZZ: This should be done in embedded view every time
    void onThumbnailReadFinished(bool animate = true);

private:
    friend class MasonryLayoutQuickSearch;

    void updatePolish() override;

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
        QString lastPlannedVersionToken;
        // External catalogs expose these values as cheap model roles. Keep
        // them with the geometry so no presentation mode has to construct one
        // ImageFile QObject per catalog row during a reset.
        QString modelIdentity;
        QString modelPath;
        QString modelText;
        QSize modelKnownSize;
        int modelSourceIndex = -1;
        bool modelIsImage = false;
        bool modelIsFolder = false;
        quint64 modelVisualRevision = 1;

        QRectF geometry() const;
        QSize thumbnailSize(int spacing) const;
    };

    struct DelegateCommitContext;
    struct DelegateRowCommit;
    struct IncrementalModelChangeContext;
    struct MasonryPageState;
    struct ModelDelta;
    struct ModelResetTrace;
    struct RewrapTrace;
    struct ViewportAnchor;

    using LayoutBand = ZoinGallery::GalleryLayoutBand;

    BrickItem *createComponent(
        PresentationMode mode,
        const QVariantMap &initialProperties = {});
    QQmlComponent *delegateComponent(PresentationMode mode) const;

    bool isEmbedded() const;
    void requestRewrap(bool animate = true);
    void capturePresentationViewportAnchor(qreal *viewportY,
                                           bool *wasVisible) const;
    void completePresentationModeChange(qreal previousCurrentViewportY,
                                        bool previousCurrentWasVisible);
    void rewrap(bool animate = true);
    void rewrapFixed(bool animate, RewrapTrace *trace);
    void rewrapSparseMasonry(bool animate, qreal currentIndexOffset);
    void rewrapMasonry(bool animate, qreal currentIndexOffset);
    ViewportAnchor fixedViewportAnchor(bool preserve) const;
    int updateFixedContentExtent(
        const ZoinGallery::GalleryFixedLayoutPlan &plan);
    void restoreFixedViewportAnchor(const ViewportAnchor &anchor);
    void commitFixedViewport(qreal oldContentY, bool animate,
                             RewrapTrace *trace);
    void traceFixedRewrap(const RewrapTrace &trace) const;
    bool applyPreparedResetViewport();
    void calcFixedLayout();
    ZoinGallery::GalleryLayoutRequest layoutRequest() const;
    ZoinGallery::GalleryFixedLayoutPlan fixedLayoutPlan() const;
    bool sparseFixedLayout() const;
    // Sparse external catalogs keep only the rows needed by the active
    // viewport. Masonry and Icons still need a deterministic position for
    // every logical row, so these helpers provide a bounded, placeholder
    // geometry without allocating a brick for the whole catalog.
    bool sparseVirtualLayout() const;
    int virtualGridColumnCount() const;
    qreal virtualGridRowHeight() const;
    QRectF virtualGridGeometry(int index) const;
    void applyVirtualGridGeometry(MasonryBrick &brick, int index) const;
    int logicalBrickCount() const;
    MasonryBrick *brickAt(int index);
    const MasonryBrick *brickAt(int index) const;
    MasonryBrick &ensureBrickAt(int index);
    void discardSparseBrick(int index);
    QRectF analyticFixedGeometry(int index) const;
    void applyAnalyticFixedGeometry(MasonryBrick &brick, int index) const;
    QList<int> indexesForHorizontalRange(qreal left, qreal right) const;
    QList<int> materializedModelRows() const;
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
    int columnsNavigationIndex(int index, NavigationDirection direction,
                               bool page, int lastIndex) const;
    int fixedNavigationIndex(int index, NavigationDirection direction,
                             bool page, int lastIndex) const;
    int masonryNavigationIndex(int index, NavigationDirection direction,
                               int lastIndex) const;
    QVariantMap initialMasonryPageResult(
        const MasonryPageState &state) const;
    QVariantMap sparseMasonryPagePlan(
        MasonryPageState state, QVariantMap result) const;
    QVariantMap denseMasonryPagePlan(
        MasonryPageState state, QVariantMap result) const;
    int closestLayoutBand(qreal target, int first, int last,
                          bool preferHigherOnTie = false) const;
    bool prepareDenseMasonryPage(MasonryPageState *state) const;
    void resolveDenseMasonryPageTarget(MasonryPageState *state) const;
    QVariantMap commitDenseMasonryPage(
        const MasonryPageState &state, QVariantMap result) const;
    void updateWindowTopFromContentY();
    ZoinGallery::GalleryViewportWindow viewportMaterializationPlan() const;
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
    enum CalcLayoutMode {
        CalcLayoutMasonry,
        CalcLayoutSingleRow,
        CalcLayoutGrid
    };
    CalcLayoutMode layoutMode() const;
    static void calcGridLayout(QList<MasonryBrick> &bricks, int canvasWidth, int rowTargetHeight, int spacing,
                           bool lastRowMatchesPrevious, qreal paddingTop);
    static void calcLayout(QList<MasonryBrick> &bricks, int canvasWidth,
                           int rowTargetHeight, int spacing,
                           bool lastRowMatchesPrevious, qreal paddingTop,
                           CalcLayoutMode layoutMode);
    void updateProperties(bool animate = false);
    QList<int> delegateIndexesForCurrentViewport() const;
    void retireInactiveDelegates(const QSet<int> &retainedIndexes,
                                 DelegateCommitContext *context);
    bool prepareDelegateRow(int index, DelegateRowCommit *row,
                            DelegateCommitContext *context);
    void bindDelegateRowIdentity(DelegateRowCommit *row,
                                 DelegateCommitContext *context);
    void bindDelegateRowVisual(DelegateRowCommit *row,
                               DelegateCommitContext *context);
    void applyDelegateRowLayout(DelegateRowCommit *row, bool animate,
                                DelegateCommitContext *context);
    void finalizeDelegateRow(DelegateRowCommit *row,
                             DelegateCommitContext *context);
    void releaseUnclaimedDelegates(DelegateCommitContext *context);
    void traceDeferredDelegateCommit(
        const QList<int> &delegateIndexes,
        const DelegateCommitContext &context) const;
    void traceDelegateCommit(const QList<int> &delegateIndexes,
                             const DelegateCommitContext &context) const;
    void setContentYInternal(qreal newContentY);
    void restorePreservedCurrentItemPosition();
    void preservePendingThumbnailRequestsForModelReset();
    void restorePendingThumbnailRequestsAfterModelReset();

    void setContentHeight(qreal newContentHeight);
    void updateCurrentImageIndex();

    void onDataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight, const QVector<int> &roles = QVector<int>());
    void applySparseModelDelta(const ModelDelta &delta);
    void applyMaterializedModelDelta(const ModelDelta &delta);
    void updateMaterializedBrickState(const ModelDelta &delta);
    void refreshImageCountFromCatalog();
    void updateVisualRowsForDelta(const ModelDelta &delta);
    bool handleLightweightMasonryDelta(const ModelDelta &delta);
    bool handleFixedLayoutDelta(const ModelDelta &delta);
    void handleMasonryDelta(const ModelDelta &delta);
    void applyMasonryImageSizeDelta(const ModelDelta &delta);
    void planDeltaThumbnails(const ModelDelta &delta);
    void pushToCurrentRow(int index, bool animate = true);
    MasonryBrick brickForImage(ImageFile *imageFile) const;
    void prepareForIncrementalModelChange(int insertedFirst = -1,
                                          int insertedLast = -1);
    void applyIncrementalModelChange();
    bool applySparseTailInsert();
    IncrementalModelChangeContext beginIncrementalModelRebuild();
    void rebuildIncrementalBricks(
        IncrementalModelChangeContext *context);
    void retireRemovedIncrementalBricks(
        IncrementalModelChangeContext *context);
    void remapIncrementalLoadingRows();
    void restoreIncrementalViewport(
        const IncrementalModelChangeContext &context);
    void finishIncrementalModelChange(
        const IncrementalModelChangeContext &context);
    void onModelAboutToBeReset();
    void onModelReset();
    void rebuildBricksAfterModelReset();
    void restoreCursorAfterModelReset();
    bool commitModelResetLayout();
    void finishModelResetViewport();
    void refreshImageCountAfterModelReset();
    void finishModelResetSignals(int previousCurrentIndex,
                                 const QString &preservedCurrentPath);
    void traceModelReset(const ModelResetTrace &trace) const;
    void updateModelRoleCache();
    bool canUseLightweightRows() const;
    MasonryBrick lightweightBrickForModelRow(int row) const;
    void populateBrickModelState(MasonryBrick &brick, int row) const;
    ImageFile *materializeImageForIndex(int index);
    bool brickIsImage(int index) const;
    bool brickIsFolder(int index) const;
    QString brickPath(int index) const;
    bool resetSlotLayoutMatches() const;
    void releaseResetSlotItems(bool clearBindings = true);
    void scheduleDeferredDelegateRefresh();
    void flushDeferredDelegateRefresh();
    void cancelDeferredDelegateMaterialization();
    void scheduleDeferredDelegateMaterialization();
    void enqueueDeferredDelegateMaterialization(int row);
    void beginDeferredDelegateMaterialization(quint64 generation);
    void materializeDeferredDelegateBatch(quint64 generation);
    QVariantMap visualSnapshotForIndex(int index) const;
    void updateVisualSnapshotForIndex(int index);
    void scheduleLightweightRewrap();
    void flushLightweightRewrap();
    void zoom(bool in);
    void updateNeedScroll();

    void pushBrickItem(BrickItem *item);
    BrickItem *popBrickItem(
        int viewIndex = -1,
        const QVariantMap &initialProperties = {});
    void trimFreeDelegatePool();

    QSize dp(QSizeF value);
    qreal dp(qreal value);
    qreal dpValue();

    ZoinGallery::GalleryDelegatePool _delegatePool;
    quint64 _delegateCatalogGeneration = 1;
    QSet<int> _activeBrickIndexes;
    // A model reset is synchronous on the GUI thread. Keep the currently
    // painted delegate assigned to its visual row until endResetModel(), so
    // a directory replacement performs one old->new binding change instead
    // of old->null->new and does not scramble stable row geometry through an
    // unordered free pool. This map exists only for the active reset.
    QHash<int, BrickItem *> _resetSlotItems;
    QHash<int, QPointer<ImageFile>> _resetSlotModels;
    PresentationMode _resetSlotPresentationMode = Masonry;
    qreal _resetSlotWidth = 0;
    qreal _resetSlotHeight = 0;
    qreal _resetSlotDensity = 0;
    QQmlComponent *_resetSlotDelegate = nullptr;
    QQuickItem *_resetSlotViewport = nullptr;
    bool _resetSlotReusePending = false;
    bool _deferDelegateRefreshOnReset = false;
    bool _delegateRefreshPending = false;
    quint64 _delegateRefreshGeneration = 0;
    bool _visualSnapshotRefresh = false;
    bool _delegateMaterializationPending = false;
    QMetaObject::Connection _delegateFrameSwappedConnection;
    QMetaObject::Connection _delegateAfterSynchronizingConnection;
    std::atomic<quint64> _delegateSynchronizedGeneration{0};
    QList<QPair<int, QString>> _delegateMaterializationRows;
    QHash<int, QString> _delegateMaterializationEntries;
    qsizetype _delegateMaterializationCursor = 0;

    QAbstractItemModel *_model;
    // Role numbers are properties of the current model, not retained catalog
    // contents. A model advertising this contract supports lightweight rows
    // and stable identity/path lookup without ImageFile materialization.
    int _entryIdRole = -1;
    int _sourceIndexRole = -1;
    int _localPathRole = -1;
    int _entryNameRole = -1;
    int _knownImageSizeRole = -1;
    int _visualSnapshotRole = -1;
    bool _lightweightRewrapPending = false;
    quint64 _lightweightRewrapGeneration = 0;
    bool _sparseCatalogRows = false;

    QList<MasonryBrick> _bricks;
    // A paged external catalog has a large logical row count but only a
    // viewport-sized materialized window.  Never mirror that logical count
    // with empty MasonryBrick objects: sparse rows exist only while they own
    // an active/overscan delegate.  The model remains the source of row data.
    QHash<int, MasonryBrick> _sparseBricks;
    QList<MasonryBrick> _currentLoadingRow;
    ZoinGallery::GalleryGeometryIndex _geometryIndex;
    const QVector<LayoutBand> &_layoutBands;
    quint64 _layoutRevision = 0;
    quint64 _delegateCommitRevision = 0;
    QSet<int> _visibleIndexSet;
    QSet<int> _overscanIndexSet;
    ZoinGallery::GalleryThumbnailPlanner _thumbnailPlanner;
    bool _cancelingThumbnailPlan = false;
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
    int _layoutUpdateDepth = 0;
    bool _layoutUpdateNeedsRewrap = false;
    bool _layoutUpdateAnimate = true;
    bool _layoutUpdateNeedsPositionViewport = false;
    bool _layoutUpdateNeedsScrollRefresh = false;
    bool _layoutUpdatePresentationModeChanged = false;
    bool _layoutUpdateModelResetSnapshotRefresh = false;
    qreal _layoutUpdateCurrentViewportY = 0;
    bool _layoutUpdateCurrentWasVisible = false;
    bool _deferDelegateWindowCommit = false;
    bool _presentationModeCommitInProgress = false;
    bool _preparedResetViewportPending = false;
    bool _preparedResetViewportRestoresOffset = false;
    int _preparedResetCursorIndex = 0;
    qreal _preparedResetScrollOffset = 0;
    qreal _modeDensities[5] = {150.0, 30.0, 30.0, 160.0, 128.0};
    qreal _contentY;
    qreal _contentHeight;
    QRect _lastViewportGeometry;
    QQmlComponent *_delegate = nullptr;
    QQmlComponent *_masonryDelegate = nullptr;
    QQmlComponent *_columnsDelegate = nullptr;
    QQmlComponent *_detailsDelegate = nullptr;
    QQmlComponent *_gridDelegate = nullptr;
    QQmlComponent *_iconsDelegate = nullptr;
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
    int _incrementalModelPreviousCount = 0;
    int _incrementalInsertedFirst = -1;
    int _incrementalInsertedLast = -1;
};

#endif // MASONRYLAYOUT_H
