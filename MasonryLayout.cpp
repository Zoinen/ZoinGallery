#include "MasonryLayout.h"
#include "FileListModel.h"
#include "GalleryPixelGrid.h"
#include "MasonryLayoutQuickSearch.h"
#include "SvgCursor.h"

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlProperty>
#include <QQuickWindow>
#include <QSettings>
#include <QGuiApplication>
#include <QFontMetricsF>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

namespace {


ZoinGallery::GalleryPresentationMode enginePresentationMode(
    MasonryLayout::PresentationMode mode) {
    using EngineMode = ZoinGallery::GalleryPresentationMode;
    switch (mode) {
    case MasonryLayout::Masonry:
        return EngineMode::Masonry;
    case MasonryLayout::Columns:
        return EngineMode::Columns;
    case MasonryLayout::Details:
        return EngineMode::Details;
    case MasonryLayout::Grid:
        return EngineMode::Grid;
    case MasonryLayout::Icons:
        return EngineMode::Icons;
    }
    return EngineMode::Masonry;
}

} // namespace

MasonryLayout::MasonryLayout(QQuickItem *parent)
    : QQuickItem(parent),
      _layoutBands(_geometryIndex.bands()) {
    _iconLabelFont = QGuiApplication::font();
    QSettings set;
    const int persistedMode = set.value(
        QStringLiteral("layout/presentationMode"),
        static_cast<int>(Masonry)).toInt();
    _presentationMode = static_cast<PresentationMode>(
        qBound(static_cast<int>(Masonry), persistedMode,
               static_cast<int>(Icons)));
    _columnCount = qBound(2, set.value(
        QStringLiteral("layout/columnCount"), 2).toInt(), 3);
    for (int mode = static_cast<int>(Masonry);
         mode <= static_cast<int>(Icons); ++mode) {
        const auto presentation = static_cast<PresentationMode>(mode);
        const QString name = presentationModeSettingsName(presentation);
        const QString densityKey = QStringLiteral("layout/%1/density")
                                       .arg(name);
        const QString extentKey = QStringLiteral("layout/%1/targetExtent")
                                      .arg(name);
        const qreal legacyTarget = presentation == Masonry
            ? set.value(QStringLiteral("targetHeight"),
                        _modeDensities[mode]).toDouble()
            : set.value(extentKey, _modeDensities[mode]).toDouble();
        _modeDensities[mode] = normalizedDensity(
            presentation, set.value(densityKey, legacyTarget).toDouble());
    }
    _density = _modeDensities[static_cast<int>(_presentationMode)];
    _targetHeight = qRound(_density);
    // qDebug() << "ZZ TARGET HEIGHT" << _targetHeight;
    _visibleStart = -1;
    _visibleEnd = -1;
    _topItem = 0;
    _topItemOffset = 0;
    _currentIndexOffsetOverride = -1;
    _contentY = 0;
    _contentHeight = 0;
    _model = nullptr;
    _delegate = nullptr;
    _currentIndex = 0;
    _viewport = nullptr;
    _needScroll = false;
    _dp = 1;
    _spacing = 12; // Should be divisible by 4
    _listView = set.value("listView", true).toBool();
    _showTransparentGrid = set.value("showTransparentGrid", true).toBool();
    _animateResizing = set.value("animateResizing", true).toBool();
    _imageCount = 0;
    _currentImageIndex = 0;
    _listRowHeight = 30;

    _currentScrollingMode = false;
    _currentScrollingDirection = -2;

    _paddingLeft = 0;
    _paddingRight = 0;
    _paddingTop = 0;
    _paddingBottom = 0;

    _preserveCurrentItemPositionOnNextModelReset = false;
    _preservedCurrentFallbackIndex = -1;
    _preservedViewportAnchorFallbackIndex = -1;
    _preservedViewportAnchorOffset = 0;

    _quickSearch = new MasonryLayoutQuickSearch(this);
}

void MasonryLayout::componentComplete() {
    QQuickItem::componentComplete();

    connect(this, &MasonryLayout::widthChanged,
            this, [this]() { requestRewrap(); });

    connect(this, &MasonryLayout::heightChanged, this, [&] () {
        if (_layoutUpdateDepth > 0) {
            // The outer f4 Details header changes this height in the same
            // transaction as presentationMode/density. Recompute against the
            // final viewport once instead of laying out the outgoing mode.
            _layoutUpdateNeedsScrollRefresh = true;
            requestRewrap(false);
            return;
        }
        if (_presentationMode == Columns) {
            rewrap(false);
            updateNeedScroll();
            return;
        }
        const qreal newContentY = qMin<qreal>(
            _contentY, qMax<qreal>(0, contentHeight() - height()));
        if (newContentY != _contentY) {
            setContentYInternal(newContentY);
        }
        else {
            updateProperties();
        }
        updateNeedScroll();
    });

    // Declarative anchors can establish the final width during completion,
    // before the widthChanged connection above exists. Catalogs consisting of
    // folders have no later metadata change to trigger another layout pass, so
    // ensure the already-populated model is wrapped once at its completed size.
    rewrap(false);
}

void MasonryLayout::updatePolish() {
    QQuickItem::updatePolish();
    flushDeferredDelegateRefresh();
}



void MasonryLayout::reReadAndDecodeThumbnails() {
    _currentLoadingRow.clear();
    _thumbnailPlanner.reset();
    if (!isEmbedded() && _model) {
        dynamic_cast<ZoinGallery::GalleryCatalogSource *>(_model)
            ->cancelAllDecodeRunners();
    }

    // Metadata remains catalog-wide because justified Masonry rows need each
    // image aspect ratio. Pixel decode is viewport work: queue only the
    // visible rows and one viewport before/after. Re-entering an evicted row
    // follows this same planner and either republishes a memory-cache hit or
    // submits one fresh exact-size decode.
    emit layoutReset();
    planViewportThumbnails(_overscanIndexSet, true);
}

void MasonryLayout::preserveCurrentItemPositionForNextModelReset() {
    const int count = logicalBrickCount();
    if (count <= 0) {
        return;
    }

    if (_currentIndex >= 0 && _currentIndex < count) {
        _preservedCurrentItemFullPath = brickPath(_currentIndex);
        _preservedCurrentFallbackIndex = _currentIndex;
    }

    const int anchorIndex = _topItem;
    if (anchorIndex >= 0 && anchorIndex < count) {
        _preservedViewportAnchorFullPath = brickPath(anchorIndex);
        _preservedViewportAnchorFallbackIndex = anchorIndex;
        _preservedViewportAnchorOffset =
            indexGeometry(anchorIndex).y() - _contentY;
    }

    _preserveCurrentItemPositionOnNextModelReset =
        !_preservedCurrentItemFullPath.isEmpty() ||
        !_preservedViewportAnchorFullPath.isEmpty();
}

void MasonryLayout::preservePendingThumbnailRequestsForModelReset() {
    for (const MasonryBrick &pendingBrick : std::as_const(_currentLoadingRow)) {
        const int index = pendingBrick.globalIndex;
        if (index < 0 || index >= _bricks.size()) {
            continue;
        }
        ImageFile *image = _bricks[index].image;
        if (image && image->isImage() && image->fullSize().isValid()) {
            _preservedPendingThumbnailInfo.insert(image->fullPath(),
                                                   image->info());
        }
    }
}

void MasonryLayout::restorePendingThumbnailRequestsAfterModelReset() {
    if (_preservedPendingThumbnailInfo.isEmpty()) {
        return;
    }

    const QHash<QString, ImageInfo> pendingInfo =
        _preservedPendingThumbnailInfo;
    _preservedPendingThumbnailInfo.clear();

    auto *requestModel =
        dynamic_cast<ZoinGallery::GalleryCatalogSource *>(_model);
    if (!requestModel) {
        return;
    }

    QList<ImageDecodeRequest> requests;
    requests.reserve(pendingInfo.size());
    for (int i = 0; i < _bricks.size(); ++i) {
        const MasonryBrick &brick = _bricks[i];
        if (!brick.image || !brick.image->isImage() ||
            !brick.image->fullSize().isValid()) {
            continue;
        }
        const auto preserved = pendingInfo.constFind(brick.image->fullPath());
        if (preserved == pendingInfo.constEnd()) {
            continue;
        }

        const ImageInfo currentInfo = brick.image->info();
        const bool sameModifiedTime =
            preserved->lastModified.isValid() ==
                currentInfo.lastModified.isValid() &&
            (!preserved->lastModified.isValid() ||
             preserved->lastModified == currentInfo.lastModified);
        const bool sameFileSize = preserved->fileSize == currentInfo.fileSize;
        if (!sameModifiedTime || !sameFileSize) {
            // A new metadata request will enqueue the replacement thumbnail.
            continue;
        }

        if (!_overscanIndexSet.contains(i)) {
            continue;
        }
        planThumbnailForIndex(
            i, i >= _visibleStart && i <= _visibleEnd,
            requests, true);
    }
    if (!requests.isEmpty()) {
        requestModel->decodeImages(requests);
    }
}

void MasonryLayout::zoomIn() {
    zoom(true);
}

void MasonryLayout::zoomOut() {
    zoom(false);
}

void MasonryLayout::setScrollingMode(bool scrollingMode, int direction) {
    if (_currentScrollingMode == scrollingMode && _currentScrollingDirection == direction) {
        return;
    }

    if (scrollingMode) {
        QString path;
        if (direction == -1) {
            path = ":/ZoinGallery/resources/ScrollModeUp.svg";
        }
        else if (direction == 1) {
            path = ":/ZoinGallery/resources/ScrollModeDown.svg";
        }
        else {
            path = ":/ZoinGallery/resources/ScrollMode.svg";
        }
        SvgCursor::setOverrideCursor(path, dpValue());
    }
    else {
        SvgCursor::setOverrideCursor();
    }
    _currentScrollingMode = scrollingMode;
    _currentScrollingDirection = direction;
}

BrickItem *MasonryLayout::createComponent(
    PresentationMode mode, const QVariantMap &delegateProperties) {
    if (!_viewport) {
        QQmlComponent component(qmlEngine(this));
        component.setData(R"QML(
        import QtQuick

        Item {
        }
        )QML", QUrl());
        if (component.status() != QQmlComponent::Ready) {
            qDebug() << "Error in component:" << component.status() << component.errors();
        }

        _viewport = qobject_cast<QQuickItem*>(component.create(QQmlEngine::contextForObject(this)));
        _viewport->setParentItem(this);
        _viewport->setParent(this);
        positionViewport();
        emit viewportChanged();
    }

    QQmlComponent *component = delegateComponent(mode);
    if (!component) {
        qDebug() << "Empty delegate";
        return nullptr;
    }

    if (component->status() != QQmlComponent::Ready) {
        qDebug() << "Error in component:" << component->status()
                 << component->errors();
        return nullptr;
    }

    // Construct the delegate in its final presentation. Creating every mode's
    // subtree as Masonry and changing it immediately afterwards makes a cold
    // mode switch pay for two complete QML binding evaluations per slot.
    QVariantMap initialProperties = delegateProperties;
    initialProperties.insert(QStringLiteral("presentationMode"),
                             static_cast<int>(mode));
    BrickItem *object = qobject_cast<BrickItem *>(
        component->createWithInitialProperties(
            initialProperties, QQmlEngine::contextForObject(this)));
    if (!object) {
        qDebug() << "Could not create delegate for presentation" << mode
                 << component->errors();
        return nullptr;
    }
    object->setProperty(
        "_zoinGalleryDelegateComponent",
        QVariant::fromValue(reinterpret_cast<quintptr>(component)));
    // A newly allocated viewport slot becomes visible only after its row
    // bindings and final geometry have been committed.
    object->setVisible(false);
    object->setParentItem(_viewport);
    object->setParent(_viewport);
    return object;
}

QQmlComponent *MasonryLayout::delegateComponent(
    PresentationMode mode) const {
    QQmlComponent *component = nullptr;
    switch (mode) {
    case Masonry:
        component = _masonryDelegate;
        break;
    case Columns:
        component = _columnsDelegate;
        break;
    case Details:
        component = _detailsDelegate;
        break;
    case Grid:
        component = _gridDelegate;
        break;
    case Icons:
        component = _iconsDelegate;
        break;
    }
    return component ? component : _delegate;
}

bool MasonryLayout::isEmbedded() const {
    const auto *source =
        dynamic_cast<const ZoinGallery::GalleryCatalogSource *>(_model);
    return source && source->rootItem() != nullptr;
}

QString MasonryLayout::presentationModeSettingsName(PresentationMode mode) {
    switch (mode) {
    case Masonry:
        return QStringLiteral("masonry");
    case Columns:
        return QStringLiteral("columns");
    case Details:
        return QStringLiteral("details");
    case Grid:
        return QStringLiteral("grid");
    case Icons:
        return QStringLiteral("icons");
    }
    return QStringLiteral("masonry");
}

qreal MasonryLayout::normalizedDensity(
    PresentationMode mode, qreal density) {
    return ZoinGallery::GalleryDensityPolicy::normalized(
        enginePresentationMode(mode), density);
}


bool MasonryLayout::applyPreparedResetViewport() {
    if (!_preparedResetViewportPending) {
        return false;
    }
    _preparedResetViewportPending = false;

    const int rowCount = logicalBrickCount();
    _currentIndex = rowCount > 0
        ? qBound(0, _preparedResetCursorIndex, rowCount - 1) : -1;

    qreal target = 0;
    if (_preparedResetViewportRestoresOffset) {
        target = _preparedResetScrollOffset;
    }
    else if (_currentIndex >= 0) {
        const QRectF geometry = indexGeometry(_currentIndex);
        if (geometry.isValid() && !geometry.isEmpty()) {
            if (_presentationMode == Columns) {
                target = geometry.center().x() - width() / 2.0;
            }
            else {
                target = geometry.center().y() - height() / 2.0;
            }
        }
    }
    _contentY = qBound<qreal>(0, target, maximumContentOffset());
    return true;
}

MasonryLayout::CalcLayoutMode MasonryLayout::layoutMode() const {
    return !isEmbedded() ? CalcLayoutMasonry : _listView ? CalcLayoutSingleRow : CalcLayoutGrid;
}

QRectF fitRectInCell(const QRectF &cellRect, const QSizeF &originalSize) {
    QSizeF scaledSize;
    if (originalSize.width() / originalSize.height() > cellRect.width() / cellRect.height()) {
        // Scale based on cell's width
        scaledSize.setWidth(cellRect.width());
        scaledSize.setHeight(cellRect.width() * originalSize.height() / originalSize.width());
    }
    else {
        // Scale based on cell's height
        scaledSize.setWidth(cellRect.height() * originalSize.width() / originalSize.height());
        scaledSize.setHeight(cellRect.height());
    }

    QPointF offset((cellRect.width() - scaledSize.width()) / 2.0, (cellRect.height() - scaledSize.height()) / 2.0);
    return QRectF(cellRect.topLeft() + offset, scaledSize);
}


void MasonryLayout::calcGridLayout(QList<MasonryBrick> &bricks, int canvasWidth, int rowTargetHeight, int spacing,
                                   bool lastRowMatchesPrevious, qreal paddingTop) {
    int dimensions = canvasWidth < 80 ? 1 :
                     canvasWidth < 150 ? 2 :
                     canvasWidth < 300 ? 3 : 4;
    int rows = dimensions;
    int columns = dimensions;
    qreal cellWidth = (canvasWidth - spacing * (columns + 1)) / qreal(columns);
    qreal cellHeight = (rowTargetHeight - spacing * (rows + 1)) / qreal(rows);
    for (int i = 0; i < bricks.size(); i++) {
        if (i >= rows * columns) {
            bricks[i].normalizedSize = QSizeF();
            bricks[i].row = 0;
            bricks[i].column = 0;
            continue;
        }
        int currentRow = i / columns;
        int currentColumn = i % columns;

        bricks[i].row = currentRow;
        bricks[i].column = currentColumn;
        QRectF cellRect(currentColumn * cellWidth + spacing * (currentColumn + 1), currentRow * cellHeight + spacing * (currentRow + 1),
                        cellWidth, cellHeight);
        QRectF imageRect = fitRectInCell(cellRect, bricks[i].originalSize);
        bricks[i].x = imageRect.x();
        bricks[i].y = imageRect.y();
        bricks[i].normalizedSize = imageRect.size();
    }
}

void MasonryLayout::calcLayout(QList<MasonryBrick> &bricks, int canvasWidth, int rowTargetHeight, int spacing,
                               bool lastRowMatchesPrevious, qreal paddingTop, CalcLayoutMode layoutMode) {
    if (layoutMode == CalcLayoutGrid) {
        calcGridLayout(bricks, canvasWidth, rowTargetHeight, spacing, lastRowMatchesPrevious, paddingTop);
        return;
    }

    ZoinGallery::GalleryLayoutRequest request;
    request.mode = ZoinGallery::GalleryPresentationMode::Masonry;
    request.viewportSize = QSizeF(qMax(0, canvasWidth), rowTargetHeight);
    request.insets.top = paddingTop;
    request.density = rowTargetHeight;
    request.spacing = spacing;
    request.lastRowMatchesPrevious = lastRowMatchesPrevious;
    request.singleRow = layoutMode == CalcLayoutSingleRow;

    QVector<ZoinGallery::GalleryLayoutEntry> entries;
    entries.reserve(bricks.size());
    for (const MasonryBrick &brick : std::as_const(bricks)) {
        entries.append({
            .originalSize = brick.originalSize,
            .temporaryLineBreakAfter = brick.temporaryLineBreakAfter,
            .lineBreakAfter = brick.lineBreakAfter,
        });
    }
    const ZoinGallery::GalleryLayoutResult result =
        ZoinGallery::JustifiedMasonryStrategy::layout(request, entries);
    Q_ASSERT(result.cells.size() == bricks.size());
    for (int index = 0; index < bricks.size(); ++index) {
        MasonryBrick &brick = bricks[index];
        const ZoinGallery::GalleryLayoutCell &cell = result.cells.at(index);
        brick.x = cell.geometry.x();
        brick.y = cell.geometry.y();
        brick.normalizedSize = cell.geometry.size();
        brick.row = cell.row;
        brick.column = cell.column;
        brick.temporaryLineBreakAfter = false;
    }
}

QString rectToString(QRectF rect) {
    QRect rectI = rect.toRect();
    return QString("%1,%2\n%3x%4").arg(rectI.x()).arg(rectI.y()).arg(rectI.width()).arg(rectI.height());
}
