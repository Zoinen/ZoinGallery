#include "MasonryLayout.h"
#include "GalleryIconTextMeasurer.h"

#include <algorithm>
#include <cmath>
#include <optional>

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
    return request;
}

ZoinGallery::GalleryFixedLayoutPlan MasonryLayout::fixedLayoutPlan() const {
    return ZoinGallery::GalleryLayoutEngine::fixedPlan(
        layoutRequest(), logicalBrickCount());
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
    if (!_viewport) {
        return;
    }
    _viewport->setX(_paddingLeft
                    - (_presentationMode == Columns ? _contentY : 0));
    _viewport->setY(_presentationMode == Columns ? 0 : -_contentY);
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
    const int columns = virtualGridColumnCount();
    if (columns <= 0) {
        return {};
    }
    const qreal canvasWidth = qMax<qreal>(
        0, width() - _paddingLeft - _paddingRight);
    const qreal cellWidth = canvasWidth / columns;
    const qreal rowHeight = virtualGridRowHeight();
    const int row = index / columns;
    const int column = index % columns;
    return QRectF(column * cellWidth,
                  _paddingTop + row * rowHeight,
                  cellWidth, rowHeight);
}

void MasonryLayout::applyVirtualGridGeometry(
    MasonryBrick &brick, int index) const {
    const QRectF geometry = virtualGridGeometry(index);
    if (!geometry.isValid() || geometry.isEmpty()) {
        return;
    }
    const int columns = virtualGridColumnCount();
    brick.row = index / columns;
    brick.column = index % columns;
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

void MasonryLayout::rebuildGroupSections(
    const ZoinGallery::GalleryFixedLayoutPlan &plan)
{
    _groupSections.clear();
    if (!groupingActive()
        || (_presentationMode == Masonry && !_sparseCatalogRows)
        || _groupIndex.groupCount() == 0) {
        _groupIndex.setSectionGeometry({}, {}, 0);
        return;
    }

    const int groupCount = _groupIndex.groupCount();
    _groupSections.resize(groupCount);
    QVector<qreal> offsets;
    QVector<qreal> extents;
    offsets.reserve(groupCount);
    extents.reserve(groupCount);

    const qreal headerExtent = groupHeaderHeight();
    if (_presentationMode == Columns) {
        const int prefixCount = _groupIndex.descriptor(0)->startIndex;
        const int prefixColumns = prefixCount > 0
            ? (prefixCount + qMax(1, plan.rowsPerColumn) - 1)
                / qMax(1, plan.rowsPerColumn)
            : 0;
        const qreal availableHeight = qMax<qreal>(
            1, height() - _paddingTop - _paddingBottom);
        const int rowsPerGroupColumn = qMax(1, static_cast<int>(std::floor(
            qMax<qreal>(1, availableHeight - headerExtent)
            / qMax<qreal>(1, plan.extent))));
        qreal offset = prefixColumns * plan.cellWidth;
        for (int group = 0; group < groupCount; ++group) {
            const auto *descriptor = _groupIndex.descriptor(group);
            const int columns = qMax(1, (descriptor->count
                + rowsPerGroupColumn - 1) / rowsPerGroupColumn);
            GroupSection &section = _groupSections[group];
            section.offset = offset;
            section.columns = columns;
            section.rowsPerColumn = rowsPerGroupColumn;
            section.extent = columns * plan.cellWidth;
            if (_groupIndex.isCollapsed(group)) {
                section.extent = plan.cellWidth;
            }
            offsets.append(section.offset);
            extents.append(section.extent);
            offset += section.extent;
        }
    }
    else {
        const bool virtualRows = _presentationMode == Icons
            || (_presentationMode == Masonry && _sparseCatalogRows);
        const int columns = virtualRows ? virtualGridColumnCount()
                                  : qMax(1, plan.columns);
        const qreal rowExtent = virtualRows ? virtualGridRowHeight()
                                     : qMax<qreal>(1, plan.extent);
        const qreal virtualCanvasWidth = qMax<qreal>(
            0, width() - _paddingLeft - _paddingRight);
        const qreal cellWidth = virtualRows
            ? qMax<qreal>(1, virtualCanvasWidth / qMax(1, columns))
            : qMax<qreal>(1, plan.cellWidth);
        const int prefixCount = _groupIndex.descriptor(0)->startIndex;
        const int prefixRows = prefixCount > 0
            ? (prefixCount + columns - 1) / columns : 0;
        qreal offset = _paddingTop + prefixRows * rowExtent;
        for (int group = 0; group < groupCount; ++group) {
            const auto *descriptor = _groupIndex.descriptor(group);
            const int rows = qMax(1, (descriptor->count + columns - 1)
                / columns);
            GroupSection &section = _groupSections[group];
            section.offset = offset;
            section.columns = columns;
            section.rowsPerColumn = rows;
            section.extent = headerExtent + rows * rowExtent;
            if (_groupIndex.isCollapsed(group)) {
                section.extent = headerExtent;
            }
            offsets.append(section.offset);
            extents.append(section.extent);
            offset += section.extent;
        }
    }
    _groupIndex.setSectionGeometry(offsets, extents, headerExtent);
}

QRectF MasonryLayout::groupedFixedGeometry(int index) const
{
    if (!groupingActive()
        || (_presentationMode == Masonry && !_sparseCatalogRows)) {
        return {};
    }
    const int group = _groupIndex.groupForSourceIndex(index);
    if (group < 0 || _groupIndex.isCollapsed(group)
        || group >= _groupSections.size()) {
        return {};
    }
    const auto *descriptor = _groupIndex.descriptor(group);
    const GroupSection &section = _groupSections.at(group);
    const int local = index - descriptor->startIndex;
    const auto plan = fixedLayoutPlan();
    if (_presentationMode == Columns) {
        const int rows = qMax(1, section.rowsPerColumn);
        const int column = local / rows;
        const int row = local % rows;
        return QRectF(
            section.offset + column * plan.cellWidth,
            _paddingTop + groupHeaderHeight() + row * plan.extent,
            plan.cellWidth, plan.extent);
    }

    const bool virtualRows = _presentationMode == Icons
        || (_presentationMode == Masonry && _sparseCatalogRows);
    const int columns = qMax(1, section.columns);
    const qreal rowExtent = virtualRows ? virtualGridRowHeight()
                                 : qMax<qreal>(1, plan.extent);
    const qreal virtualCanvasWidth = qMax<qreal>(
        0, width() - _paddingLeft - _paddingRight);
    const qreal cellWidth = virtualRows
        ? qMax<qreal>(1, virtualCanvasWidth / columns)
        : qMax<qreal>(1, plan.cellWidth);
    const int row = local / columns;
    const int column = local % columns;
    const qreal width = _presentationMode == Details ? plan.canvasWidth
                                                      : cellWidth;
    return QRectF(
        column * cellWidth,
        section.offset + groupHeaderHeight() + row * rowExtent,
        width, rowExtent);
}

QRectF MasonryLayout::groupedFixedPreviewGeometry(int index) const
{
    const QRectF geometry = groupedFixedGeometry(index);
    if (!geometry.isValid() || geometry.isEmpty()) {
        return {};
    }
    if (_presentationMode == Icons) {
        const qreal lineHeight =
            ZoinGallery::GalleryIconTextMeasurer(_iconLabelFont).lineHeight();
        return QRectF(geometry.x(), geometry.y(), geometry.width(),
                      qMax<qreal>(1, geometry.width() - 3 - lineHeight - 3));
    }
    return geometry.adjusted(_spacing / 2.0, _spacing / 2.0,
                             -_spacing / 2.0, -_spacing / 2.0);
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
    if (groupingActive()
        && (_presentationMode != Masonry || _sparseCatalogRows)) {
        const int group = _groupIndex.groupForSourceIndex(index);
        if (group >= 0) {
            return groupedFixedGeometry(index);
        }
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
    if (groupingActive()) {
        const QRectF geometry = groupedFixedGeometry(index);
        const int group = _groupIndex.groupForSourceIndex(index);
        if (group >= 0 && (!geometry.isValid() || geometry.isEmpty())) {
            brick.normalizedSize = {};
            brick.previewGeometry = {};
            return;
        }
        if (geometry.isValid() && !geometry.isEmpty()) {
            const int local = group >= 0
                ? index - _groupIndex.descriptor(group)->startIndex : index;
            if (_presentationMode == Columns) {
                const int rows = qMax(1, _groupSections.at(group).rowsPerColumn);
                brick.row = local % rows;
                brick.column = local / rows;
            }
            else {
                const int columns = qMax(1, _groupSections.at(group).columns);
                brick.row = local / columns;
                brick.column = local % columns;
            }
            brick.x = geometry.x();
            brick.y = geometry.y();
            brick.normalizedSize = geometry.size();
            brick.previewGeometry = groupedFixedPreviewGeometry(index);
            return;
        }
    }
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
    if (groupingActive()) {
        if (right < left) {
            std::swap(left, right);
        }
        QList<int> result;
        const auto plan = fixedLayoutPlan();
        const int count = logicalBrickCount();
        const int prefixCount = _groupIndex.groupCount() > 0
            ? _groupIndex.descriptor(0)->startIndex : 0;
        const QList<int> prefix = plan.indexesIntersecting(left, right).toList();
        for (const int index : prefix) {
            if (index < prefixCount) {
                result.append(index);
            }
        }
        for (int group = 0; group < _groupSections.size(); ++group) {
            const GroupSection &section = _groupSections.at(group);
            if (section.offset + section.extent < left
                || section.offset > right) {
                continue;
            }
            const int firstColumn = qMax(0, static_cast<int>(std::floor(
                (left - section.offset) / qMax<qreal>(1, plan.cellWidth))));
            const int lastColumn = qMax(0, static_cast<int>(std::floor(
                (right - section.offset) / qMax<qreal>(1, plan.cellWidth))));
            const int beginColumn = qMin(firstColumn, section.columns - 1);
            const int endColumn = qMin(lastColumn, section.columns - 1);
            if (endColumn < 0 || beginColumn > endColumn
                || _groupIndex.isCollapsed(group)) {
                continue;
            }
            const auto *descriptor = _groupIndex.descriptor(group);
            for (int column = beginColumn; column <= endColumn; ++column) {
                const int first = descriptor->startIndex
                    + column * section.rowsPerColumn;
                const int last = qMin(descriptor->startIndex + descriptor->count,
                                      first + section.rowsPerColumn);
                for (int index = first; index < last && index < count; ++index) {
                    if (indexGeometry(index).intersects(
                            QRectF(left, 0, right - left, height()))) {
                        result.append(index);
                    }
                }
            }
        }
        return result;
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
        return;
    }
    QVector<ZoinGallery::GalleryGeometryRecord> records;
    records.reserve(_bricks.size());
    for (int index = 0; index < _bricks.size(); ++index) {
        if (groupingActive()) {
            const int group = _groupIndex.groupForSourceIndex(index);
            if (group >= 0 && _groupIndex.isCollapsed(group)) {
                continue;
            }
        }
        const MasonryBrick &brick = _bricks.at(index);
        records.append({
            .index = index,
            .row = brick.row,
            .geometry = brick.geometry(),
        });
    }
    if (!_geometryIndex.rebuild(records))
        return;
    ++_layoutRevision;
    emit layoutRevisionChanged();
    emit layoutBandsChanged();
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
    if (groupingActive() && _presentationMode != Columns
        && (_presentationMode != Masonry || _sparseCatalogRows)) {
        QList<int> result;
        const auto plan = fixedLayoutPlan();
        const int prefixCount = _groupIndex.groupCount() > 0
            ? _groupIndex.descriptor(0)->startIndex : 0;
        const bool virtualRows = _presentationMode == Icons
            || (_presentationMode == Masonry && _sparseCatalogRows);
        if (virtualRows) {
            const QRectF range(0, top, width(), bottom - top);
            for (int index = 0; index < prefixCount; ++index) {
                if (virtualGridGeometry(index).intersects(range)) {
                    result.append(index);
                }
            }
        }
        else {
            for (const int index : plan.indexesIntersecting(top, bottom)) {
                if (index < prefixCount) {
                    result.append(index);
                }
            }
        }
        const QRectF range(0, top, width(), bottom - top);
        for (int group = 0; group < _groupSections.size(); ++group) {
            const GroupSection &section = _groupSections.at(group);
            if (_groupIndex.isCollapsed(group)
                || section.offset + section.extent < top
                || section.offset > bottom) {
                continue;
            }
            const auto *descriptor = _groupIndex.descriptor(group);
            const bool virtualRows = _presentationMode == Icons
                || (_presentationMode == Masonry && _sparseCatalogRows);
            const qreal rowExtent = virtualRows ? virtualGridRowHeight()
                                         : qMax<qreal>(1, plan.extent);
            const int columns = virtualRows
                ? virtualGridColumnCount() : qMax(1, section.columns);
            const int firstRow = qMax(0, static_cast<int>(std::floor(
                (top - section.offset - groupHeaderHeight()) / rowExtent)));
            const int lastRow = qMax(0, static_cast<int>(std::floor(
                (bottom - section.offset - groupHeaderHeight()) / rowExtent)));
            for (int row = firstRow; row <= lastRow; ++row) {
                const int first = descriptor->startIndex + row * columns;
                const int last = qMin(descriptor->startIndex + descriptor->count,
                                      first + columns);
                for (int index = first; index < last; ++index) {
                    if (indexGeometry(index).intersects(range)) {
                        result.append(index);
                    }
                }
            }
        }
        return result;
    }
    if (_presentationMode == Details || _presentationMode == Grid
        || _presentationMode == Icons || sparseVirtualLayout()) {
        const bool virtualLayout = _presentationMode == Icons
            || sparseVirtualLayout();
        if (!virtualLayout) {
            return fixedLayoutPlan().indexesIntersecting(top, bottom).toList();
        }
        const int count = logicalBrickCount();
        const qreal extent = qMax<qreal>(1.0, effectiveTargetExtent());
        const qreal rowExtent = virtualLayout
            ? virtualGridRowHeight() : extent;
        const int columns = virtualLayout
            ? virtualGridColumnCount()
            : (_presentationMode == Grid ? effectiveColumnCount() : 1);
        const int rowCount = columns > 0
            ? (count + columns - 1) / columns : 0;
        if (rowCount <= 0) {
            return indexes;
        }
        int firstRow = int(std::floor((top - _paddingTop) / rowExtent));
        int lastRow = int(std::floor((bottom - _paddingTop) / rowExtent));
        firstRow = qBound(0, firstRow, rowCount - 1);
        lastRow = qBound(0, lastRow, rowCount - 1);
        if (lastRow < firstRow) {
            return indexes;
        }
        indexes.reserve((lastRow - firstRow + 1) * columns);
        for (int row = firstRow; row <= lastRow; ++row) {
            const int begin = row * columns;
            const int end = qMin(count, begin + columns);
            for (int index = begin; index < end; ++index) {
                const QRectF geometry = virtualLayout
                    ? virtualGridGeometry(index)
                    : analyticFixedGeometry(index);
                if (geometry.bottom() >= top && geometry.top() <= bottom) {
                    indexes.append(index);
                }
            }
        }
        return indexes;
    }
    const QVector<int> candidates = _geometryIndex.indexesIntersecting(
        top, bottom);
    if (!groupingActive()) {
        return candidates.toList();
    }
    QList<int> result;
    result.reserve(candidates.size());
    for (const int index : candidates) {
        const int group = _groupIndex.groupForSourceIndex(index);
        if (group < 0 || !_groupIndex.isCollapsed(group)) {
            result.append(index);
        }
    }
    return result;
}
