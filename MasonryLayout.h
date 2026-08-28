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

#include <atomic>

#include "MasonryLayoutQuickSearch.h"
#include "ImageFile.h"

class QParallelAnimationGroup;
class QPropertyAnimation;

// Stable per-delegate visual facade. Its QObject identity never changes, so
// replacing a catalog row invalidates only bindings for properties whose
// values actually changed instead of re-evaluating the complete QML tree via
// one QVariantMap/var dependency.
class BrickVisualRow final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool valid READ valid NOTIFY validChanged)
    Q_PROPERTY(QString entryId READ entryId NOTIFY entryIdChanged)
    Q_PROPERTY(int sourceIndex READ sourceIndex NOTIFY sourceIndexChanged)
    Q_PROPERTY(QString localPath READ localPath NOTIFY localPathChanged)
    Q_PROPERTY(QString text READ text NOTIFY textChanged)
    Q_PROPERTY(bool isFolder READ isFolder NOTIFY isFolderChanged)
    Q_PROPERTY(bool isImage READ isImage NOTIFY isImageChanged)
    Q_PROPERTY(bool isSelected READ isSelected NOTIFY isSelectedChanged)
    Q_PROPERTY(QString iconPath READ iconPath NOTIFY iconPathChanged)
    Q_PROPERTY(QVariantMap highlightStyle READ highlightStyle
               NOTIFY highlightStyleChanged)
    Q_PROPERTY(QVariantMap displayFields READ displayFields
               NOTIFY displayFieldsChanged)
    Q_PROPERTY(QString displayBaseName READ displayBaseName
               NOTIFY displayBaseNameChanged)
    Q_PROPERTY(QString displayExtension READ displayExtension
               NOTIFY displayExtensionChanged)
    Q_PROPERTY(QString sizeText READ sizeText NOTIFY sizeTextChanged)
    Q_PROPERTY(bool isHidden READ isHidden NOTIFY isHiddenChanged)
    Q_PROPERTY(QString highlightMarker READ highlightMarker
               NOTIFY highlightMarkerChanged)
    Q_PROPERTY(QString normalForeground READ normalForeground
               NOTIFY normalForegroundChanged)
    Q_PROPERTY(QString normalBackground READ normalBackground
               NOTIFY normalBackgroundChanged)
    Q_PROPERTY(QString selectedForeground READ selectedForeground
               NOTIFY selectedForegroundChanged)
    Q_PROPERTY(QString selectedBackground READ selectedBackground
               NOTIFY selectedBackgroundChanged)
    Q_PROPERTY(QString cursorForeground READ cursorForeground
               NOTIFY cursorForegroundChanged)
    Q_PROPERTY(QString cursorBackground READ cursorBackground
               NOTIFY cursorBackgroundChanged)
    Q_PROPERTY(QString selectedCursorForeground READ selectedCursorForeground
               NOTIFY selectedCursorForegroundChanged)
    Q_PROPERTY(QString selectedCursorBackground READ selectedCursorBackground
               NOTIFY selectedCursorBackgroundChanged)
    Q_PROPERTY(QString imageIdUrl READ imageIdUrl NOTIFY imageIdUrlChanged)
public:
    explicit BrickVisualRow(QObject *parent = nullptr);

    void applySnapshot(const QVariantMap &snapshot);
    bool valid() const;
    QString entryId() const;
    int sourceIndex() const;
    QString localPath() const;
    QString text() const;
    bool isFolder() const;
    bool isImage() const;
    bool isSelected() const;
    QString iconPath() const;
    QVariantMap highlightStyle() const;
    QVariantMap displayFields() const;
    QString displayBaseName() const;
    QString displayExtension() const;
    QString sizeText() const;
    bool isHidden() const;
    QString highlightMarker() const;
    QString normalForeground() const;
    QString normalBackground() const;
    QString selectedForeground() const;
    QString selectedBackground() const;
    QString cursorForeground() const;
    QString cursorBackground() const;
    QString selectedCursorForeground() const;
    QString selectedCursorBackground() const;
    QString imageIdUrl() const;

signals:
    void validChanged();
    void entryIdChanged();
    void sourceIndexChanged();
    void localPathChanged();
    void textChanged();
    void isFolderChanged();
    void isImageChanged();
    void isSelectedChanged();
    void iconPathChanged();
    void highlightStyleChanged();
    void displayFieldsChanged();
    void displayBaseNameChanged();
    void displayExtensionChanged();
    void sizeTextChanged();
    void isHiddenChanged();
    void highlightMarkerChanged();
    void normalForegroundChanged();
    void normalBackgroundChanged();
    void selectedForegroundChanged();
    void selectedBackgroundChanged();
    void cursorForegroundChanged();
    void cursorBackgroundChanged();
    void selectedCursorForegroundChanged();
    void selectedCursorBackgroundChanged();
    void imageIdUrlChanged();

private:
    bool _valid = false;
    QString _entryId;
    int _sourceIndex = -1;
    QString _localPath;
    QString _text;
    bool _isFolder = false;
    bool _isImage = false;
    bool _isSelected = false;
    QString _iconPath;
    QVariantMap _highlightStyle;
    QVariantMap _displayFields;
    QString _displayBaseName;
    QString _displayExtension;
    QString _sizeText;
    bool _isHidden = false;
    QString _highlightMarker;
    QString _normalForeground;
    QString _normalBackground;
    QString _selectedForeground;
    QString _selectedBackground;
    QString _cursorForeground;
    QString _cursorBackground;
    QString _selectedCursorForeground;
    QString _selectedCursorBackground;
    QString _imageIdUrl;
};


class BrickItem : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(int row READ row NOTIFY rowColumnChanged)
    Q_PROPERTY(int column READ column NOTIFY rowColumnChanged)
    Q_PROPERTY(QRectF previewRect READ previewRect NOTIFY previewRectChanged)
    Q_PROPERTY(QString iconLabelText READ iconLabelText NOTIFY iconLabelTextChanged)
    Q_PROPERTY(QVariantMap visualRow READ visualRow NOTIFY visualRowChanged)
    Q_PROPERTY(QObject *visualModel READ visualModel CONSTANT)
    Q_PROPERTY(bool visualFacadeReady READ visualFacadeReady
               NOTIFY visualFacadeReadyChanged)
    Q_PROPERTY(bool geometryAnimationRunning READ geometryAnimationRunning
               NOTIFY geometryAnimationRunningChanged)
public:
    BrickItem(QQuickItem *parent = nullptr);

    void setRowColumn(int row, int column);
    void setGeometry(QRectF rect, bool animate, bool snapToLogicalPixels = true);
    void setPreviewRect(const QRectF &previewRect);
    void setIconLabelText(const QString &text);
    void setVisualRow(const QVariantMap &visualRow);
    void setVisualFacadeReady(bool ready);
    QRectF geometry() const;
    QRectF previewRect() const;
    QString iconLabelText() const;
    QVariantMap visualRow() const;
    QObject *visualModel() const;
    bool visualFacadeReady() const;
    bool geometryAnimationRunning() const;
    void stopGeometryAnimation();

    int row() const;
    int column() const;

signals:
    void rowColumnChanged();
    void previewRectChanged();
    void iconLabelTextChanged();
    void visualRowChanged();
    void visualFacadeReadyChanged();
    void geometryAnimationRunningChanged();

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
    QVariantMap _visualRow;
    BrickVisualRow *_visualModel = nullptr;
    bool _visualFacadeReady = false;

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
    void requestRewrap(bool animate = true);
    void capturePresentationViewportAnchor(qreal *viewportY,
                                           bool *wasVisible) const;
    void completePresentationModeChange(qreal previousCurrentViewportY,
                                        bool previousCurrentWasVisible);
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
    BrickItem *popBrickItem();

    QSize dp(QSizeF value);
    qreal dp(qreal value);
    qreal dpValue();

    QSet<BrickItem *> _usedBrickItems;
    QSet<BrickItem *> _freeBrickItems;
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
    int _layoutUpdateDepth = 0;
    bool _layoutUpdateNeedsRewrap = false;
    bool _layoutUpdateAnimate = true;
    bool _layoutUpdateNeedsPositionViewport = false;
    bool _layoutUpdateNeedsScrollRefresh = false;
    bool _layoutUpdatePresentationModeChanged = false;
    qreal _layoutUpdateCurrentViewportY = 0;
    bool _layoutUpdateCurrentWasVisible = false;
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
