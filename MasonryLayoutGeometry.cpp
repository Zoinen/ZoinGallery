#include "MasonryLayout.h"
#include "GalleryIconTextMeasurer.h"
#include "GalleryPixelGrid.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace {

ZoinGallery::GalleryPresentationMode enginePresentationMode(
    MasonryLayout::PresentationMode mode) {
    using EngineMode = ZoinGallery::GalleryPresentationMode;
    switch (mode) {
    case MasonryLayout::Masonry: return EngineMode::Masonry;
    case MasonryLayout::Columns: return EngineMode::Columns;
    case MasonryLayout::Details: return EngineMode::Details;
    case MasonryLayout::Grid: return EngineMode::Grid;
    case MasonryLayout::Icons: return EngineMode::Icons;
    }
    return EngineMode::Masonry;
}

} // namespace

qreal MasonryLayout::effectiveTargetExtent() const {
    return qMax<qreal>(1.0, _density);
}

ZoinGallery::GalleryLayoutRequest MasonryLayout::layoutRequest() const {
    ZoinGallery::GalleryLayoutRequest request;
    request.mode = enginePresentationMode(_presentationMode);
    request.viewportSize = QSizeF(width(), height());
    request.insets = {
        .left = _paddingLeft,
        .right = _paddingRight,
        .top = _paddingTop,
        .bottom = _paddingBottom,
    };
    request.density = effectiveTargetExtent();
    request.spacing = _spacing;
    request.columnCount = _columnCount;
    request.devicePixelRatio = devicePixelRatio();
    request.groupHeaderHeight = _groupHeaderHeight;
    request.groups = _groupRanges;
    if (_presentationMode == Icons || sparseVirtualLayout()) {
        request.iconRowHeight = virtualGridRowHeight();
    }
    return request;
}

ZoinGallery::GalleryFixedLayoutPlan MasonryLayout::fixedLayoutPlan() const {
    if (_fixedLayoutPlanCached) {
        return _cachedFixedLayoutPlan;
    }
    ZoinGallery::GalleryLayoutRequest request = layoutRequest();
    if (_presentationMode == Masonry && sparseVirtualLayout()) {
        request.mode = ZoinGallery::GalleryPresentationMode::Grid;
    }
    _cachedFixedLayoutPlan = ZoinGallery::GalleryLayoutEngine::fixedPlan(
        request, logicalBrickCount());
    _fixedLayoutPlanCached = true;
    return _cachedFixedLayoutPlan;
}

int MasonryLayout::effectiveColumnCount() const {
    return fixedLayoutPlan().columns;
}

int MasonryLayout::rowsPerColumn() const {
    return fixedLayoutPlan().rowsPerColumn;
}

qreal MasonryLayout::columnStride() const {
    return fixedLayoutPlan().cellWidth;
}

int MasonryLayout::maximumWindowTopIndex() const {
    if (_presentationMode != Columns) {
        return 0;
    }
    const int rows = rowsPerColumn();
    const int count = logicalBrickCount();
    return count <= 0 ? 0 : ((count - 1) / rows) * rows;
}

qreal MasonryLayout::contentYForWindowTopIndex(int index) const {
    const int rows = rowsPerColumn();
    const qreal cellWidth = columnStride();
    const int firstColumn = qBound(0, index, maximumWindowTopIndex()) / rows;
    return qBound<qreal>(0, firstColumn * cellWidth,
                         maximumContentOffset());
}

int MasonryLayout::windowTopIndexForContentY(qreal contentY) const {
    const int rows = rowsPerColumn();
    const qreal cellWidth = columnStride();
    if (cellWidth <= 0) {
        return 0;
    }
    const int column = qMax(0, int(std::floor(contentY / cellWidth)));
    return qBound(0, column * rows, maximumWindowTopIndex());
}

void MasonryLayout::updateWindowTopFromContentY() {
    if (_presentationMode != Columns) {
        return;
    }
    const int top = windowTopIndexForContentY(_contentY);
    if (_windowTopIndex != top) {
        _windowTopIndex = top;
        emit windowTopIndexChanged();
    }
}

qreal MasonryLayout::maximumContentOffset() const {
    return qMax<qreal>(
        0, _contentHeight - (_presentationMode == Columns ? width() : height()));
}

qreal MasonryLayout::viewportExtent() const {
    return _presentationMode == Columns ? width() : height();
}

void MasonryLayout::positionViewport() {
    // The scrolling surface owns the scene-space phase for every delegate.
    // Observe host placement once per panel; QML leaves need only local
    // insets/centering, with no dependency on contentY or the host hierarchy.
    const QPointF origin = mapToScene(QPointF());
    const QPointF offset = mapFromScene(
        ZoinGallery::PixelGrid::snapDevicePoint(origin, devicePixelRatio()));
    const auto *parent = parentItem();
    const QPointF parentOffset = parent ? parent->mapFromScene(
        ZoinGallery::PixelGrid::snapDevicePoint(
            parent->mapToScene(QPointF()), devicePixelRatio())) : QPointF();
    if (_parentPixelGridOffset != parentOffset) {
        _parentPixelGridOffset = parentOffset;
        emit parentPixelGridOffsetChanged();
    }
    if (!_viewport) {
        return;
    }
    const QPointF scrollPosition(
        _paddingLeft - (_presentationMode == Columns ? _contentY : 0),
        _presentationMode == Columns ? 0 : -_contentY);
    _viewport->setPosition(offset + ZoinGallery::PixelGrid::snapDevicePoint(
        scrollPosition, devicePixelRatio()));
    const QPointF viewportOrigin = _viewport->mapToScene(QPointF());
    if (_viewportSceneOrigin != viewportOrigin) {
        _viewportSceneOrigin = viewportOrigin;
        emit viewportSceneOriginChanged();
    }
}

void MasonryLayout::observePixelGridAncestors() {
    for (const auto &connection : std::as_const(_pixelGridConnections))
        disconnect(connection);
    _pixelGridConnections.clear();
    for (QQuickItem *item = this; item; item = item->parentItem()) {
        _pixelGridConnections.append(connect(item, &QQuickItem::parentChanged,
            this, &MasonryLayout::observePixelGridAncestors));
        _pixelGridConnections.append(connect(item, &QQuickItem::xChanged,
            this, &MasonryLayout::positionViewport));
        _pixelGridConnections.append(connect(item, &QQuickItem::yChanged,
            this, &MasonryLayout::positionViewport));
        _pixelGridConnections.append(connect(item, &QQuickItem::widthChanged,
            this, &MasonryLayout::positionViewport));
        _pixelGridConnections.append(connect(item, &QQuickItem::heightChanged,
            this, &MasonryLayout::positionViewport));
        _pixelGridConnections.append(connect(item, &QQuickItem::scaleChanged,
            this, &MasonryLayout::positionViewport));
        _pixelGridConnections.append(connect(item, &QQuickItem::rotationChanged,
            this, &MasonryLayout::positionViewport));
        _pixelGridConnections.append(connect(item, &QQuickItem::transformOriginChanged,
            this, &MasonryLayout::positionViewport));
    }
    positionViewport();
}

void MasonryLayout::calcFixedLayout() {
    // Columns, Details, Grid and Icons are fixed layouts. Their geometry is
    // derived when a viewport row is materialized; a presentation switch
    // must never rewrite one MasonryBrick per catalog entry. Icons reserve a
    // uniform four-line label row so wrapping remains bounded and geometry
    // stays analytical.
}

bool MasonryLayout::sparseFixedLayout() const {
    return _sparseCatalogRows &&
        (_presentationMode == Details || _presentationMode == Columns ||
         _presentationMode == Grid);
}

bool MasonryLayout::sparseVirtualLayout() const {
    return _sparseCatalogRows &&
        (_presentationMode == Masonry || _presentationMode == Icons);
}

int MasonryLayout::virtualGridColumnCount() const {
    const qreal canvasWidth = qMax<qreal>(
        1.0, width() - _paddingLeft - _paddingRight);
    const qreal target = qMax<qreal>(1.0, effectiveTargetExtent());
    return qMax(1, static_cast<int>(std::floor(canvasWidth / target)));
}

qreal MasonryLayout::virtualGridRowHeight() const {
    if (_presentationMode != Icons) {
        return qMax<qreal>(1.0, effectiveTargetExtent());
    }

    const int columns = virtualGridColumnCount();
    const qreal canvasWidth = qMax<qreal>(
        0, width() - _paddingLeft - _paddingRight);
    const qreal cellWidth = columns > 0
        ? canvasWidth / columns : canvasWidth;
    // Unknown rows cannot be measured without materializing their catalog
    // payload. Reserve the same maximum of four wrapped label lines for
    // every virtual row, keeping all row positions stable as pages arrive.
    ZoinGallery::GalleryIconTextMeasurer textMeasurer(_iconLabelFont);
    return qMax<qreal>(1.0, cellWidth + 3 * textMeasurer.lineHeight());
}

QRectF MasonryLayout::virtualGridGeometry(int index) const {
    if (index < 0 || index >= logicalBrickCount()) {
        return {};
    }
    return fixedLayoutPlan().geometryFor(index);
}

void MasonryLayout::applyVirtualGridGeometry(
    MasonryBrick &brick, int index) const {
    const QRectF geometry = virtualGridGeometry(index);
    if (!geometry.isValid() || geometry.isEmpty()) {
        return;
    }
    const ZoinGallery::GalleryFixedLayoutPlan plan = fixedLayoutPlan();
    brick.row = plan.rowFor(index);
    brick.column = plan.columnFor(index);
    brick.x = geometry.x();
    brick.y = geometry.y();
    brick.normalizedSize = geometry.size();

    if (_presentationMode == Icons) {
        const qreal labelWidth = qMax<qreal>(1, geometry.width() - 8);
        const qreal oneLinePreviewHeight = qMax<qreal>(
            1, geometry.width() - 3
                - ZoinGallery::GalleryIconTextMeasurer(
                      _iconLabelFont).lineHeight() - 3);
        ZoinGallery::GalleryIconTextMeasurer textMeasurer(_iconLabelFont);
        const QString sourceText = brick.image
            ? brick.image->text() : brick.modelText;
        brick.iconLabelText = textMeasurer.layout(
            sourceText, labelWidth).text;
        brick.previewGeometry = QRectF(
            geometry.x(), geometry.y(), geometry.width(),
            oneLinePreviewHeight);
    }
    else {
        brick.previewGeometry = geometry.adjusted(
            _spacing / 2.0, _spacing / 2.0,
            -_spacing / 2.0, -_spacing / 2.0);
    }
}

int MasonryLayout::logicalBrickCount() const {
    return _sparseCatalogRows && _model
        ? _model->rowCount() : _bricks.size();
}

MasonryLayout::MasonryBrick *MasonryLayout::brickAt(int index) {
    if (index < 0 || index >= logicalBrickCount()) {
        return nullptr;
    }
    if (!_sparseCatalogRows) {
        return index < _bricks.size() ? &_bricks[index] : nullptr;
    }
    const auto found = _sparseBricks.find(index);
    return found == _sparseBricks.end() ? nullptr : &found.value();
}

const MasonryLayout::MasonryBrick *MasonryLayout::brickAt(int index) const {
    if (index < 0 || index >= logicalBrickCount()) {
        return nullptr;
    }
    if (!_sparseCatalogRows) {
        return index < _bricks.size() ? &_bricks[index] : nullptr;
    }
    const auto found = _sparseBricks.constFind(index);
    return found == _sparseBricks.cend() ? nullptr : &found.value();
}

MasonryLayout::MasonryBrick &MasonryLayout::ensureBrickAt(int index) {
    Q_ASSERT(index >= 0 && index < logicalBrickCount());
    if (!_sparseCatalogRows) {
        MasonryBrick &brick = _bricks[index];
        if (_presentationMode != Masonry) {
            applyAnalyticFixedGeometry(brick, index);
        }
        return brick;
    }
    auto found = _sparseBricks.find(index);
    if (found == _sparseBricks.end()) {
        MasonryBrick brick;
        if (_model && index < _model->rowCount() && _entryIdRole >= 0 &&
            !_model->index(index, 0).data(_entryIdRole).toString().isEmpty()) {
            brick = lightweightBrickForModelRow(index);
        }
        brick.globalIndex = index;
        found = _sparseBricks.insert(index, std::move(brick));
    }
    if (sparseFixedLayout() || sparseVirtualLayout()) {
        applyAnalyticFixedGeometry(found.value(), index);
    }
    return found.value();
}

void MasonryLayout::discardSparseBrick(int index) {
    if (!_sparseCatalogRows) {
        return;
    }
    const auto found = _sparseBricks.find(index);
    if (found != _sparseBricks.end() && !found->item) {
        _sparseBricks.erase(found);
    }
}

QRectF MasonryLayout::analyticFixedGeometry(int index) const {
    if (index < 0 || index >= logicalBrickCount()) {
        return {};
    }
    if (sparseVirtualLayout() || _presentationMode == Icons) {
        return virtualGridGeometry(index);
    }
    if (_presentationMode == Details || _presentationMode == Columns
        || _presentationMode == Grid) {
        return fixedLayoutPlan().geometryFor(index);
    }
    const MasonryBrick *brick = brickAt(index);
    return brick ? brick->geometry() : QRectF();
}

void MasonryLayout::applyAnalyticFixedGeometry(
    MasonryBrick &brick, int index) const {
    if (sparseVirtualLayout() || _presentationMode == Icons) {
        applyVirtualGridGeometry(brick, index);
        return;
    }
    const QRectF geometry = analyticFixedGeometry(index);
    if (!geometry.isValid() || geometry.isEmpty()) {
        return;
    }
    if (_presentationMode == Columns) {
        const int rows = rowsPerColumn();
        brick.row = index % rows;
        brick.column = index / rows;
    }
    else if (_presentationMode == Grid) {
        const int columns = effectiveColumnCount();
        brick.row = index / columns;
        brick.column = index % columns;
    }
    else {
        brick.row = index;
        brick.column = 0;
    }
    brick.x = geometry.x();
    brick.y = geometry.y();
    brick.normalizedSize = geometry.size();
    brick.previewGeometry = fixedLayoutPlan().previewGeometryFor(index);
}

QList<int> MasonryLayout::indexesForHorizontalRange(
    qreal left, qreal right) const {
    if (_presentationMode != Columns) {
        return {};
    }
    return fixedLayoutPlan().indexesIntersecting(left, right).toList();
}

QList<int> MasonryLayout::materializedModelRows() const {
    QList<int> rows;
    if (!_model || !_model->property("sparseCatalog").toBool()) {
        return rows;
    }
    const QVariantList values = _model->property("materializedRows").toList();
    rows.reserve(values.size());
    for (const QVariant &value : values) {
        bool ok = false;
        const int row = value.toInt(&ok);
        if (ok && row >= 0 && row < _model->rowCount()) {
            rows.append(row);
        }
    }
    return rows;
}

void MasonryLayout::rebuildLayoutBands() {
    if (_presentationMode != Masonry || sparseVirtualLayout()) {
        _geometryIndex.clear();
        // Fixed sparse layouts are arithmetic. Building one heap-backed band
        // (and one heavyweight brick lookup) per logical row defeats model
        // paging even though only a viewport-sized range can be painted.
        ++_layoutRevision;
        emit layoutRevisionChanged();
        emit layoutBandsChanged();
        emit groupHeaderGeometriesChanged();
        return;
    }
    QVector<ZoinGallery::GalleryGeometryRecord> records;
    records.reserve(_bricks.size());
    for (int index = 0; index < _bricks.size(); ++index) {
        const MasonryBrick &brick = _bricks.at(index);
        records.append({
            .index = index,
            .row = brick.row,
            .geometry = brick.geometry(),
        });
    }
    if (!_geometryIndex.rebuild(records)) {
        emit groupHeaderGeometriesChanged();
        return;
    }
    ++_layoutRevision;
    emit layoutRevisionChanged();
    emit layoutBandsChanged();
    emit groupHeaderGeometriesChanged();
}

int MasonryLayout::bandIndexAt(qreal y) const {
    return _geometryIndex.firstBandIntersecting(y);
}

QList<int> MasonryLayout::indexesForVerticalRange(
    qreal top, qreal bottom) const {
    QList<int> indexes;
    if (bottom < top) {
        std::swap(top, bottom);
    }
    if (_presentationMode == Details || _presentationMode == Grid
        || _presentationMode == Icons || sparseVirtualLayout()) {
        return fixedLayoutPlan().indexesIntersecting(top, bottom).toList();
    }
    return _geometryIndex.indexesIntersecting(top, bottom).toList();
}
