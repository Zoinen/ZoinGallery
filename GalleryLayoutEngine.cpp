#include "GalleryLayoutEngine.h"

#include <QtMath>

#include <algorithm>
#include <cmath>

namespace ZoinGallery {
namespace {

qreal canvasWidth(const GalleryLayoutRequest &request) {
    return qMax<qreal>(0, request.viewportSize.width()
                             - request.insets.left - request.insets.right);
}

qreal usableHeight(const GalleryLayoutRequest &request) {
    return qMax<qreal>(1, request.viewportSize.height()
                             - request.insets.top - request.insets.bottom);
}

int uniformColumnCount(const GalleryLayoutRequest &request) {
    const qreal extent = qMax<qreal>(1, request.density);
    return qMax(1, static_cast<int>(std::floor(
                       qMax<qreal>(1, canvasWidth(request)) / extent)));
}

QSizeF scaleToWidthWithSpacing(const QSizeF &size, qreal width,
                               qreal spacing) {
    if (size.width() <= 0 || size.height() <= 0) {
        return QSizeF(qMax<qreal>(0, width), qMax<qreal>(0, width));
    }
    const qreal aspect = size.width() / size.height();
    return QSizeF(qMax<qreal>(0, width),
                  qMax<qreal>(0, (width - spacing) / aspect + spacing));
}

QSizeF scaleToHeightWithSpacing(const QSizeF &size, qreal height,
                                qreal spacing) {
    if (size.width() <= 0 || size.height() <= 0) {
        return QSizeF(qMax<qreal>(0, height), qMax<qreal>(0, height));
    }
    const qreal aspect = size.width() / size.height();
    return QSizeF((height - spacing) * aspect + spacing, height);
}

qreal scaleMasonryRow(QVector<GalleryLayoutCell> &cells,
                      const QVector<GalleryLayoutEntry> &entries,
                      qreal width, qreal targetHeight, qreal spacing,
                      int lastIndex, qreal requestedHeight = 0) {
    const int cellsInRow = cells.at(lastIndex).column + 1;
    qreal rowHeight = requestedHeight;
    if (rowHeight <= 0) {
        const qreal rowRight = cells.at(lastIndex).geometry.right();
        const qreal totalWithoutSpacing =
            rowRight - cellsInRow * spacing;
        if (totalWithoutSpacing > 0) {
            const qreal stretch = (width - cellsInRow * spacing)
                / totalWithoutSpacing;
            rowHeight = (targetHeight - spacing) * stretch + spacing;
        } else {
            rowHeight = targetHeight;
        }
    }

    const int firstIndex = lastIndex - cellsInRow + 1;
    for (int index = firstIndex; index <= lastIndex; ++index) {
        const QSizeF source = entries.at(index).originalSize.isEmpty()
            ? cells.at(index).geometry.size() : entries.at(index).originalSize;
        const QSizeF size = scaleToHeightWithSpacing(
            source, rowHeight, spacing);
        const qreal x = index == firstIndex
            ? 0 : cells.at(index - 1).geometry.right();
        cells[index].geometry = QRectF(
            x, cells.at(index).geometry.y(), size.width(), size.height());
        cells[index].previewGeometry = cells[index].geometry.adjusted(
            spacing / 2, spacing / 2, -spacing / 2, -spacing / 2);
    }
    return rowHeight;
}

QSizeF masonryEntrySize(const GalleryLayoutEntry &entry,
                        qreal width, qreal targetHeight,
                        qreal spacing) {
    if (entry.originalSize.width() == 0
        && entry.originalSize.height() == 0) {
        return QSizeF(width, targetHeight);
    }
    if (entry.originalSize.width() == 0) {
        return QSizeF(width, entry.originalSize.height());
    }
    return scaleToHeightWithSpacing(
        entry.originalSize, targetHeight, spacing);
}

void matchFinalMasonryRow(
    QVector<GalleryLayoutCell> &cells,
    const QVector<GalleryLayoutEntry> &entries,
    qreal width, qreal targetHeight, qreal spacing,
    int index, int currentRow, qreal lastY) {
    for (int previous = index - 1; previous >= 0; --previous) {
        if (cells.at(previous).row == currentRow) {
            continue;
        }
        scaleMasonryRow(
            cells, entries, width, targetHeight, spacing, index,
            cells.at(previous).geometry.height());
        if (cells.at(index).geometry.right() <= width) {
            return;
        }
        for (int item = previous + 1; item <= index; ++item) {
            const QSizeF resetSize = scaleToHeightWithSpacing(
                entries.at(item).originalSize, targetHeight, spacing);
            const qreal resetX = item == previous + 1
                ? 0 : cells.at(item - 1).geometry.right();
            cells[item].geometry = QRectF(
                resetX, lastY, resetSize.width(), resetSize.height());
        }
        scaleMasonryRow(
            cells, entries, width, targetHeight, spacing, index);
        return;
    }
}

QVector<GalleryLayoutBand> buildBands(
    const QVector<GalleryLayoutCell> &cells) {
    QVector<GalleryLayoutBand> bands;
    for (int index = 0; index < cells.size(); ++index) {
        const GalleryLayoutCell &cell = cells.at(index);
        if (!cell.geometry.isValid() || cell.geometry.isEmpty()) {
            continue;
        }
        if (bands.isEmpty() || bands.last().row != cell.row) {
            GalleryLayoutBand band;
            band.row = cell.row;
            band.top = cell.geometry.top();
            band.bottom = cell.geometry.bottom();
            bands.append(std::move(band));
        }
        GalleryLayoutBand &band = bands.last();
        band.top = qMin(band.top, cell.geometry.top());
        band.bottom = qMax(band.bottom, cell.geometry.bottom());
        band.indexes.append(index);
    }
    return bands;
}

} // namespace

bool GalleryFixedLayoutPlan::horizontal() const {
    return mode == GalleryPresentationMode::Columns;
}

QRectF GalleryFixedLayoutPlan::geometryFor(int index) const {
    if (index < 0 || index >= entryCount) {
        return {};
    }
    if (mode == GalleryPresentationMode::Columns) {
        return QRectF((index / rowsPerColumn) * cellWidth,
                      insets.top + (index % rowsPerColumn) * extent,
                      cellWidth, extent);
    }
    if (mode == GalleryPresentationMode::Details) {
        return QRectF(0, insets.top + index * extent,
                      canvasWidth, extent);
    }
    const int row = index / columns;
    const int column = index % columns;
    return QRectF(column * cellWidth, insets.top + row * extent,
                  cellWidth, extent);
}

QRectF GalleryFixedLayoutPlan::previewGeometryFor(int index) const {
    const QRectF geometry = geometryFor(index);
    if (geometry.isEmpty()) {
        return {};
    }
    const qreal halfSpacing = spacing / 2;
    if (mode == GalleryPresentationMode::Grid) {
        const QRectF inner = geometry.adjusted(
            halfSpacing, halfSpacing, -halfSpacing, -halfSpacing);
        const qreal labelHeight = qMin<qreal>(
            34, qMax<qreal>(18, extent * 0.20));
        return QRectF(inner.left(), inner.top(), inner.width(),
                      qMax<qreal>(1, inner.height() - labelHeight));
    }
    const qreal side = qMax<qreal>(
        1, qMin(geometry.height() - spacing,
                geometry.width() - spacing));
    return QRectF(geometry.x() + halfSpacing,
                  geometry.y() + halfSpacing, side, side);
}

QVector<int> GalleryFixedLayoutPlan::indexesIntersecting(
    qreal start, qreal end) const {
    QVector<int> result;
    if (entryCount <= 0 || cellWidth <= 0 || extent <= 0) {
        return result;
    }
    if (end < start) {
        std::swap(start, end);
    }
    if (horizontal()) {
        const int lastColumn = (entryCount - 1) / rowsPerColumn;
        const int first = qBound(0, int(std::floor(start / cellWidth)),
                                 lastColumn);
        const int last = qBound(0, int(std::floor(end / cellWidth)),
                                lastColumn);
        result.reserve((last - first + 1) * rowsPerColumn);
        for (int column = first; column <= last; ++column) {
            const int begin = column * rowsPerColumn;
            const int finish = qMin(entryCount, begin + rowsPerColumn);
            for (int index = begin; index < finish; ++index) {
                result.append(index);
            }
        }
        return result;
    }

    const int rowCount = (entryCount + columns - 1) / columns;
    int firstRow = int(std::floor((start - insets.top) / extent));
    int lastRow = int(std::floor((end - insets.top) / extent));
    firstRow = qBound(0, firstRow, rowCount - 1);
    lastRow = qBound(0, lastRow, rowCount - 1);
    if (lastRow < firstRow) {
        return result;
    }
    result.reserve((lastRow - firstRow + 1) * columns);
    for (int row = firstRow; row <= lastRow; ++row) {
        const int begin = row * columns;
        const int finish = qMin(entryCount, begin + columns);
        for (int index = begin; index < finish; ++index) {
            result.append(index);
        }
    }
    return result;
}

qreal GalleryDensityPolicy::normalized(
    GalleryPresentationMode mode, qreal density) {
    if (!qIsFinite(density)) {
        density = mode == GalleryPresentationMode::Masonry ? 150
            : mode == GalleryPresentationMode::Grid ? 160
            : mode == GalleryPresentationMode::Icons ? 64 : 30;
    }
    switch (mode) {
    case GalleryPresentationMode::Masonry:
        return qBound<qreal>(30, density, 500);
    case GalleryPresentationMode::Columns:
    case GalleryPresentationMode::Details:
        return qBound<qreal>(22, density, 72);
    case GalleryPresentationMode::Grid:
        return qBound<qreal>(96, density, 320);
    case GalleryPresentationMode::Icons:
        return qBound<qreal>(18, density, 256);
    }
    return density;
}

GalleryFixedLayoutPlan ColumnMajorStrategy::plan(
    const GalleryLayoutRequest &request, int entryCount) {
    GalleryFixedLayoutPlan result;
    result.mode = GalleryPresentationMode::Columns;
    result.entryCount = qMax(0, entryCount);
    result.columns = qBound(2, request.columnCount, 3);
    result.extent = qMax<qreal>(1, request.density);
    result.rowsPerColumn = qMax(1, static_cast<int>(std::floor(
        usableHeight(request) / result.extent)));
    result.canvasWidth = canvasWidth(request);
    result.insets = request.insets;
    result.spacing = request.spacing;
    const qreal dpr = qMax<qreal>(0.01, request.devicePixelRatio);
    const qreal physicalCanvas = std::floor(result.canvasWidth * dpr
                                             + 0.000001);
    const qreal physicalStride = std::floor(physicalCanvas / result.columns);
    result.cellWidth = physicalStride < 1
        ? result.canvasWidth / result.columns : physicalStride / dpr;
    const int totalColumns = result.entryCount <= 0 ? 0
        : (result.entryCount + result.rowsPerColumn - 1)
              / result.rowsPerColumn;
    const qreal trailingCanvasRemainder = qMax<qreal>(
        0, result.canvasWidth - result.columns * result.cellWidth);
    result.contentExtent = request.insets.left
        + totalColumns * result.cellWidth + trailingCanvasRemainder
        + request.insets.right;
    return result;
}

GalleryFixedLayoutPlan DetailsStrategy::plan(
    const GalleryLayoutRequest &request, int entryCount) {
    GalleryFixedLayoutPlan result;
    result.mode = GalleryPresentationMode::Details;
    result.entryCount = qMax(0, entryCount);
    result.columns = 1;
    result.rowsPerColumn = result.entryCount;
    result.canvasWidth = canvasWidth(request);
    result.extent = qMax<qreal>(1, request.density);
    result.cellWidth = result.canvasWidth;
    const int completeVisibleRows = int(std::floor(
        usableHeight(request) / result.extent + 0.000000001));
    const qreal trailingViewportRemainder = completeVisibleRows > 0
        ? qMax<qreal>(0, usableHeight(request)
                            - completeVisibleRows * result.extent)
        : 0;
    result.contentExtent = request.insets.top
        + result.entryCount * result.extent + trailingViewportRemainder
        + request.insets.bottom;
    result.insets = request.insets;
    result.spacing = request.spacing;
    return result;
}

GalleryFixedLayoutPlan UniformGridStrategy::analyticalPlan(
    const GalleryLayoutRequest &request, int entryCount) {
    GalleryFixedLayoutPlan result;
    result.mode = request.mode;
    result.entryCount = qMax(0, entryCount);
    result.columns = uniformColumnCount(request);
    result.rowsPerColumn = 1;
    result.canvasWidth = canvasWidth(request);
    result.extent = qMax<qreal>(1, request.density);
    result.cellWidth = result.columns > 0
        ? result.canvasWidth / result.columns : result.canvasWidth;
    const int rows = result.columns > 0
        ? (result.entryCount + result.columns - 1) / result.columns : 0;
    result.contentExtent = request.insets.top + rows * result.extent
        + request.insets.bottom;
    result.insets = request.insets;
    result.spacing = request.spacing;
    return result;
}

GalleryLayoutResult UniformGridStrategy::layout(
    const GalleryLayoutRequest &request,
    const QVector<GalleryLayoutEntry> &entries,
    UniformGridPolicy policy,
    qreal oneLineHeight) {
    GalleryLayoutResult result;
    const GalleryFixedLayoutPlan plan = analyticalPlan(request, entries.size());
    result.cells.resize(entries.size());
    if (entries.isEmpty()) {
        result.contentExtent = request.insets.top + request.insets.bottom;
        return result;
    }

    if (policy == UniformGridPolicy::Icons) {
        const int rowCount = (entries.size() + plan.columns - 1)
            / plan.columns;
        QVector<qreal> rowHeights(rowCount, plan.cellWidth);
        const qreal previewHeight = qMax<qreal>(
            1, plan.cellWidth - 3 - oneLineHeight - 3);
        for (int index = 0; index < entries.size(); ++index) {
            const int row = index / plan.columns;
            const qreal required = previewHeight + 3
                + qMax(oneLineHeight, entries.at(index).labelHeight) + 3;
            rowHeights[row] = qMax(rowHeights[row], required);
        }
        QVector<qreal> rowTops(rowCount, request.insets.top);
        for (int row = 1; row < rowCount; ++row) {
            rowTops[row] = rowTops[row - 1] + rowHeights[row - 1];
        }
        for (int index = 0; index < entries.size(); ++index) {
            GalleryLayoutCell &cell = result.cells[index];
            cell.row = index / plan.columns;
            cell.column = index % plan.columns;
            cell.geometry = QRectF(cell.column * plan.cellWidth,
                                   rowTops[cell.row], plan.cellWidth,
                                   rowHeights[cell.row]);
            cell.previewGeometry = QRectF(
                cell.geometry.x(), cell.geometry.y(),
                plan.cellWidth, previewHeight);
            cell.displayLabel = entries.at(index).displayLabel;
        }
    } else {
        for (int index = 0; index < entries.size(); ++index) {
            GalleryLayoutCell &cell = result.cells[index];
            cell.row = index / plan.columns;
            cell.column = index % plan.columns;
            cell.geometry = plan.geometryFor(index);
            cell.previewGeometry = plan.previewGeometryFor(index);
            cell.displayLabel = entries.at(index).displayLabel;
        }
    }
    result.bands = buildBands(result.cells);
    result.contentExtent = result.cells.isEmpty()
        ? request.insets.top + request.insets.bottom
        : result.cells.last().geometry.bottom() + request.insets.bottom;
    return result;
}

GalleryLayoutResult JustifiedMasonryStrategy::layout(
    const GalleryLayoutRequest &request,
    const QVector<GalleryLayoutEntry> &entries) {
    GalleryLayoutResult result;
    result.cells.resize(entries.size());
    if (entries.isEmpty()) {
        result.contentExtent = request.insets.top + request.insets.bottom;
        return result;
    }

    const qreal width = canvasWidth(request);
    const qreal targetHeight = qMax<qreal>(1, request.density);
    const qreal spacing = request.spacing;
    int currentRow = 0;
    int currentColumn = 0;
    qreal lastX = 0;
    qreal lastY = request.insets.top;

    for (int index = 0; index < entries.size(); ++index) {
        const GalleryLayoutEntry &entry = entries.at(index);
        GalleryLayoutCell &cell = result.cells[index];
        QSizeF size = masonryEntrySize(
            entry, width, targetHeight, spacing);
        cell.row = currentRow;
        cell.column = currentColumn;
        cell.geometry = QRectF(lastX, lastY, size.width(), size.height());
        cell.previewGeometry = cell.geometry.adjusted(
            spacing / 2, spacing / 2, -spacing / 2, -spacing / 2);

        bool lineBreak = false;
        if (index > 0) {
            const GalleryLayoutEntry &previous = entries.at(index - 1);
            lineBreak = previous.temporaryLineBreakAfter
                || previous.lineBreakAfter;
        }
        if ((lastX + size.width() < width
             && (!lineBreak || currentColumn == 0))
            || request.singleRow) {
            lastX += size.width();
            if (index == entries.size() - 1 && currentRow > 0
                && request.lastRowMatchesPrevious) {
                matchFinalMasonryRow(
                    result.cells, entries, width, targetHeight,
                    spacing, index, currentRow, lastY);
            }
        } else if (currentColumn == 0) {
            if (!entry.originalSize.isEmpty()) {
                size = scaleToWidthWithSpacing(
                    entry.originalSize, width, spacing);
                cell.geometry.setSize(size);
                cell.previewGeometry = cell.geometry.adjusted(
                    spacing / 2, spacing / 2,
                    -spacing / 2, -spacing / 2);
            }
            ++currentRow;
            currentColumn = -1;
            lastX = 0;
            if (index != entries.size() - 1) {
                lastY += cell.geometry.height();
            }
        } else {
            const qreal rowHeight = request.singleRow
                ? targetHeight
                : scaleMasonryRow(result.cells, entries, width,
                                   targetHeight, spacing, index - 1);
            ++currentRow;
            currentColumn = -1;
            lastX = 0;
            lastY += rowHeight;
            --index;
        }
        ++currentColumn;
    }

    result.bands = buildBands(result.cells);
    result.contentExtent = result.cells.isEmpty()
        ? request.insets.top + request.insets.bottom
        : result.cells.last().geometry.bottom() + request.insets.bottom;
    return result;
}

GalleryFixedLayoutPlan GalleryLayoutEngine::fixedPlan(
    const GalleryLayoutRequest &request, int entryCount) {
    switch (request.mode) {
    case GalleryPresentationMode::Columns:
        return ColumnMajorStrategy::plan(request, entryCount);
    case GalleryPresentationMode::Details:
        return DetailsStrategy::plan(request, entryCount);
    case GalleryPresentationMode::Grid:
    case GalleryPresentationMode::Icons:
        return UniformGridStrategy::analyticalPlan(request, entryCount);
    case GalleryPresentationMode::Masonry:
        break;
    }
    return {};
}

GalleryLayoutResult GalleryLayoutEngine::layout(
    const GalleryLayoutRequest &request,
    const QVector<GalleryLayoutEntry> &entries,
    qreal oneLineHeight) {
    switch (request.mode) {
    case GalleryPresentationMode::Grid:
        return UniformGridStrategy::layout(
            request, entries, UniformGridPolicy::Grid, oneLineHeight);
    case GalleryPresentationMode::Icons:
        return UniformGridStrategy::layout(
            request, entries, UniformGridPolicy::Icons, oneLineHeight);
    case GalleryPresentationMode::Masonry:
        return JustifiedMasonryStrategy::layout(request, entries);
    case GalleryPresentationMode::Columns:
    case GalleryPresentationMode::Details: {
        const GalleryFixedLayoutPlan plan = fixedPlan(request, entries.size());
        GalleryLayoutResult result;
        result.cells.resize(entries.size());
        for (int index = 0; index < entries.size(); ++index) {
            GalleryLayoutCell &cell = result.cells[index];
            cell.geometry = plan.geometryFor(index);
            cell.previewGeometry = plan.previewGeometryFor(index);
            if (request.mode == GalleryPresentationMode::Columns) {
                cell.row = index % plan.rowsPerColumn;
                cell.column = index / plan.rowsPerColumn;
            } else {
                cell.row = index;
                cell.column = 0;
            }
        }
        result.contentExtent = plan.contentExtent;
        result.bands = buildBands(result.cells);
        return result;
    }
    }
    return {};
}

} // namespace ZoinGallery
