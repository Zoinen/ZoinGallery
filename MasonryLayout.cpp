#include "MasonryLayout.h"
#include "FileListModel.h"
#include "MasonryLayoutQuickSearch.h"
#include "SvgCursor.h"

#include <QParallelAnimationGroup>
#include <QPropertyAnimation>
#include <QPropertyAnimation>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlProperty>
#include <QQuickWindow>
#include <QDir>
#include <QSettings>
#include <QMap>
#include <QGuiApplication>
#include <QFontMetricsF>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

QRectF roundRect(const QRectF &rectF) {
    // Rounding to floor
    return rectF.toRect();
    //return QRect(rectF.x(), rectF.y(), rectF.width(), rectF.height());
}

static ImageFile *imageFileFromModelIndex(const QModelIndex &index) {
    return index.data(FileListModel::ImageFileRole).value<ImageFile *>();
}

static bool modelRequestsViewStatePreservation(QAbstractItemModel *model) {
    const auto *requestModel =
        dynamic_cast<const ThumbnailsRequestInterface *>(model);
    return requestModel && requestModel->preserveViewStateOnReset();
}

namespace {

struct IconLabelLayout {
    QString text;
    int lineCount = 1;
    qreal height = 0;
};

class IconTextMeasurer {
public:
    explicit IconTextMeasurer(const QFont &font)
        : _metrics(font),
          _lineHeight(static_cast<qreal>(qCeil(_metrics.height()))) {}

    qreal lineHeight() const {
        return _lineHeight;
    }

    IconLabelLayout layout(const QString &sourceText, qreal width) {
        constexpr int maximumLines = 4;
        // A small safety inset absorbs shaping/kerning differences between
        // QFontMetricsF glyph advances and QQuickText's scene-graph layout.
        width = qMax<qreal>(1, width - 1);
        const int sourceLines = wrappedLineCount(sourceText, width);
        if (sourceLines <= maximumLines) {
            return {sourceText, sourceLines, sourceLines * _lineHeight};
        }

        const QString ellipsis(QChar(0x2026));
        auto candidateForKeptCharacters = [&](int kept) {
            const int prefixLength = (kept + 1) / 2;
            const int suffixLength = kept / 2;
            return sourceText.left(prefixLength) + ellipsis
                + sourceText.right(suffixLength);
        };

        QString best = ellipsis;
        int bestLines = 1;
        int low = 0;
        int high = qMax(0, sourceText.size() - 1);
        while (low <= high) {
            const int middle = low + (high - low) / 2;
            const QString candidate = candidateForKeptCharacters(middle);
            const int lines = wrappedLineCount(candidate, width);
            if (lines <= maximumLines) {
                best = candidate;
                bestLines = lines;
                low = middle + 1;
            }
            else {
                high = middle - 1;
            }
        }
        return {best, bestLines, bestLines * _lineHeight};
    }

private:
    qreal glyphWidth(QChar character) {
        auto found = _glyphWidths.constFind(character);
        if (found != _glyphWidths.constEnd()) {
            return *found;
        }
        return *_glyphWidths.insert(
            character, _metrics.horizontalAdvance(QString(character)));
    }

    int wrappedLineCount(const QString &text, qreal width) {
        if (text.isEmpty()) {
            return 1;
        }
        int lines = 1;
        qreal currentWidth = 0;
        for (const QChar character : text) {
            if (character == QChar::LineFeed) {
                ++lines;
                currentWidth = 0;
                continue;
            }
            const qreal advance = glyphWidth(character);
            if (currentWidth > 0 && currentWidth + advance > width) {
                ++lines;
                currentWidth = 0;
            }
            currentWidth += advance;
        }
        return lines;
    }

    QFontMetricsF _metrics;
    qreal _lineHeight;
    QHash<QChar, qreal> _glyphWidths;
};

} // namespace

MasonryLayout::MasonryLayout(QQuickItem *parent)
    : QQuickItem(parent) {
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
            this, [this]() { rewrap(); });

    connect(this, &MasonryLayout::heightChanged, this, [&] () {
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

QQuickItem *MasonryLayout::itemAt(qreal x, qreal y) const {
    const int index = indexAt(x, y);
    return index >= 0 && index < _bricks.size()
        ? _bricks[index].item : nullptr;
}

int MasonryLayout::indexAt(qreal x, qreal y) const {
    const int firstBand = bandIndexAt(y);
    if (firstBand < 0) {
        return -1;
    }
    // Bands normally do not overlap, but animated/reused masonry geometry can
    // share an edge. Check every band intersecting the queried scan line.
    for (int bandIndex = firstBand; bandIndex < _layoutBands.size(); ++bandIndex) {
        const LayoutBand &band = _layoutBands.at(bandIndex);
        if (band.top > y) {
            break;
        }
        if (band.bottom < y) {
            continue;
        }
        for (const int index : band.indexes) {
            if (index >= 0 && index < _bricks.size() &&
                _bricks[index].geometry().contains(x, y)) {
                return index;
            }
        }
    }
    return -1;
}

int MasonryLayout::indexAtViewport(qreal x, qreal y) const {
    return _presentationMode == Columns
        ? indexAt(x - _paddingLeft + _contentY, y)
        : indexAt(x - _paddingLeft, y + _contentY);
}

QVariantList MasonryLayout::indexesInViewportRect(qreal x, qreal y, qreal width, qreal height) const {
    QRectF viewportRect(x, y, width, height);
    viewportRect = viewportRect.normalized();
    QRectF contentRect = _presentationMode == Columns
        ? QRectF(viewportRect.x() - _paddingLeft + _contentY,
                 viewportRect.y(), viewportRect.width(), viewportRect.height())
        : QRectF(viewportRect.x() - _paddingLeft,
                 viewportRect.y() + _contentY,
                 viewportRect.width(), viewportRect.height());

    QVariantList result;
    const QList<int> candidates = _presentationMode == Columns
        ? [&]() {
            QList<int> all;
            all.reserve(_bricks.size());
            for (int index = 0; index < _bricks.size(); ++index) {
                all.append(index);
            }
            return all;
        }()
        : indexesForVerticalRange(contentRect.top(), contentRect.bottom());
    for (const int index : candidates) {
        if (_bricks[index].geometry().intersects(contentRect)) {
            result.append(index);
        }
    }
    return result;
}

QRectF MasonryLayout::indexGeometry(int index) const {
    if (index >= 0 && index < _bricks.size()) {
        if (!_bricks[index].normalizedSize.isValid() ||
            _bricks[index].normalizedSize.isEmpty()) {
            return QRectF();
        }
        if (_presentationMode != Masonry) {
            return _bricks[index].geometry();
        }
        if (_bricks[index].row == _bricks[_bricks.size() - 1].row) {
            return _bricks[index].geometry().adjusted(0, 0, 0, _paddingBottom);
        }
        if (!_bricks[index].row) {
            return _bricks[index].geometry().adjusted(0, -_paddingTop, 0, 0);
        }
        return _bricks[index].geometry();
    }
    return QRectF();
}

QRectF MasonryLayout::indexPreviewGeometry(int index) const {
    if (index < 0 || index >= _bricks.size()) {
        return QRectF();
    }
    return _bricks[index].previewGeometry;
}

QString MasonryLayout::indexImageIdUrl(int index) const {
    if (index >= 0 && index < _bricks.size()) {
        QString imageIdUrl;
        if (!_bricks[index].image->imageIdUrl().isEmpty()) {
            imageIdUrl = _bricks[index].image->imageIdUrl();
        }
        return imageIdUrl;
    }
    return QString();
}

QString MasonryLayout::indexText(int index) const {
    if (index >= 0 && index < _bricks.size()) {
        return _bricks[index].image->fileName();
    }
    return QString();
}

QString MasonryLayout::indexFullPath(int index) const {
    if (index >= 0 && index < _bricks.size()) {
        return QDir::toNativeSeparators(_bricks[index].image->fullPath());
    }
    return QString();
}

QSize MasonryLayout::indexOriginalSize(int index) const {
    if (index >= 0 && index < _bricks.size()) {
        return _bricks[index].originalSize.toSize();
    }
    return QSize();
}

QVariantMap MasonryLayout::indexExif(int index) const {
    if (index >= 0 && index < _bricks.size() && _bricks[index].image) {
        return _bricks[index].image->info().exif;
    }
    return QVariantMap();
}

int MasonryLayout::nextImageIndex(bool forward, bool moveToEnd) {
    int nextIndex = _currentIndex;
    for (int i = _currentIndex + (forward ? 1 : -1); i >= 0 && i < _bricks.size(); i += (forward ? 1 : -1)) {
        if (_bricks[i].image->isImage()) {
            nextIndex = i;
            if (!moveToEnd) {
                break;
            }
        }
    }
    return nextIndex;
}

int MasonryLayout::neighborIndex(
    int index, NavigationDirection direction) const {
    return navigationTarget(index, direction, false)
        .value(QStringLiteral("targetIndex"), index).toInt();
}

int MasonryLayout::pageIndex(
    int index, NavigationDirection direction) const {
    return navigationTarget(index, direction, true)
        .value(QStringLiteral("targetIndex"), index).toInt();
}

QVariantMap MasonryLayout::navigationTarget(
    int index, NavigationDirection direction, bool page) const {
    QVariantMap result{
        {QStringLiteral("targetIndex"), index},
        {QStringLiteral("windowTopIndex"), _windowTopIndex},
    };
    if (_bricks.isEmpty() || index < 0 || index >= _bricks.size()) {
        return result;
    }

    const int lastIndex = _bricks.size() - 1;
    int target = index;

    if (_presentationMode == Columns) {
        const int rows = rowsPerColumn();
        const int column = index / rows;
        int nextTop = _windowTopIndex;

        if (page) {
            const bool backwards = direction == NavigateLeft ||
                                   direction == NavigateUp;
            const int step = rows * effectiveColumnCount();
            target = qBound(0, index + (backwards ? -step : step),
                            lastIndex);
        }
        else {
            switch (direction) {
            case NavigateLeft:
                // There is no column to the left of the first one. Treat
                // Left there as the natural strip boundary action and move
                // to the very first entry, regardless of the current row.
                target = column > 0 ? index - rows : 0;
                break;
            case NavigateRight:
                target = qMin(lastIndex, index + rows);
                break;
            case NavigateUp:
                // Columns are one continuous column-major list. Crossing
                // the top edge continues at the bottom of the preceding
                // column, exactly as advancing through the underlying list.
                target = qMax(0, index - 1);
                break;
            case NavigateDown:
                // Likewise, the item after a column's bottom row is the top
                // item of the following column.
                target = qMin(lastIndex, index + 1);
                break;
            }
        }
        target = qBound(0, target, lastIndex);
        // The QML viewport owns reveal.  Keeping the current leading column
        // here prevents navigation between already-visible columns from
        // snapping the horizontal strip prematurely.
        result[QStringLiteral("targetIndex")] = target;
        result[QStringLiteral("windowTopIndex")] = nextTop;
        return result;
    }

    const qreal extent = qMax<qreal>(1.0, effectiveTargetExtent());
    const int columns = (_presentationMode == Grid ||
                         _presentationMode == Icons)
        ? effectiveColumnCount() : 1;
    const int viewportRows = qMax(
        1, static_cast<int>(std::floor(
               qMax<qreal>(1.0, height() - _paddingTop - _paddingBottom) /
               extent)));
    const int capacity = qMax(1, viewportRows * columns);

    if (page) {
        const bool backwards = direction == NavigateLeft ||
                               direction == NavigateUp;
        target = qBound(0, index + (backwards ? -capacity : capacity),
                        lastIndex);
    }
    else if (_presentationMode == Details) {
        switch (direction) {
        case NavigateLeft:
            target = qMax(0, index - capacity);
            break;
        case NavigateRight:
            target = qMin(lastIndex, index + capacity);
            break;
        case NavigateUp:
            target = qMax(0, index - 1);
            break;
        case NavigateDown:
            target = qMin(lastIndex, index + 1);
            break;
        }
    }
    else if (_presentationMode == Grid || _presentationMode == Icons) {
        switch (direction) {
        case NavigateLeft:
            target = qMax(0, index - 1);
            break;
        case NavigateRight:
            target = qMin(lastIndex, index + 1);
            break;
        case NavigateUp:
            target = qMax(0, index - columns);
            break;
        case NavigateDown:
            target = qMin(lastIndex, index + columns);
            break;
        }
    }
    else {
        // Preserve the established MasonryMode contract: horizontal movement
        // follows model order while vertical movement retains the physical X
        // anchor and probes the adjacent justified row.
        if (direction == NavigateLeft) {
            target = qMax(0, index - 1);
        }
        else if (direction == NavigateRight) {
            target = qMin(lastIndex, index + 1);
        }
        else {
            const QRectF current = _bricks[index].geometry();
            if (current.isValid() && !current.isEmpty()) {
                const qreal x = current.center().x();
                const qreal y = direction == NavigateUp
                    ? current.top() - 2 : current.bottom() + 2;
                const int adjacent = indexAt(x, y);
                if (adjacent >= 0) {
                    target = adjacent;
                }
            }
        }
    }

    result[QStringLiteral("targetIndex")] = target;
    return result;
}

QVariantMap MasonryLayout::masonryPagePlan(
    int currentIndex, qreal anchorX, qreal itemViewportY,
    qreal plannedContentY, qreal rowViewportY, int direction,
    qreal preferredDistance) const {
    const qreal maximum = qMax<qreal>(0, _contentHeight - height());
    const qreal planned = qBound<qreal>(0, plannedContentY, maximum);
    QVariantMap result{
        {QStringLiteral("valid"), false},
        {QStringLiteral("layoutRevision"),
         QVariant::fromValue<qulonglong>(_layoutRevision)},
        {QStringLiteral("targetIndex"), currentIndex},
        {QStringLiteral("contentY"), planned},
        {QStringLiteral("rowViewportY"), rowViewportY},
        {QStringLiteral("sourceBandIndex"), -1},
        {QStringLiteral("targetBandIndex"), -1},
        {QStringLiteral("sourceBandTop"), 0.0},
        {QStringLiteral("targetBandTop"), 0.0},
        {QStringLiteral("hitEdge"), false},
        {QStringLiteral("terminalClamp"), false},
    };
    if (_presentationMode != Masonry || _layoutBands.isEmpty() ||
        _bricks.isEmpty() || currentIndex < 0 ||
        currentIndex >= _bricks.size() || direction == 0) {
        return result;
    }

    const int pageDirection = direction < 0 ? -1 : 1;
    const qreal distance = qMax<qreal>(1, qAbs(preferredDistance));
    const auto closestBand = [this](qreal target, int first, int last,
                                    bool preferHigherOnTie = false) {
        first = qMax(0, first);
        last = qMin(last, _layoutBands.size() - 1);
        if (first > last) {
            return -1;
        }
        const auto begin = _layoutBands.cbegin() + first;
        const auto end = _layoutBands.cbegin() + last + 1;
        const auto next = std::lower_bound(
            begin, end, target,
            [](const LayoutBand &band, qreal value) {
                return band.top < value;
            });
        if (next == begin) {
            return first;
        }
        if (next == end) {
            return last;
        }
        const int nextIndex = int(std::distance(_layoutBands.cbegin(), next));
        const int previousIndex = nextIndex - 1;
        const qreal previousDistance =
            qAbs(_layoutBands.at(previousIndex).top - target);
        const qreal nextDistance =
            qAbs(_layoutBands.at(nextIndex).top - target);
        if (qFuzzyCompare(previousDistance, nextDistance)) {
            return preferHigherOnTie ? nextIndex : previousIndex;
        }
        return previousDistance < nextDistance ? previousIndex : nextIndex;
    };

    const bool hasViewportRow = std::isfinite(rowViewportY);
    const qreal sourceProbe = hasViewportRow
        ? planned + rowViewportY : planned;
    int sourceBandIndex = -1;
    if (hasViewportRow) {
        sourceBandIndex = closestBand(
            sourceProbe, 0, _layoutBands.size() - 1);
    }
    else {
        // The page phase belongs to the row which begins at or above the
        // viewport top. Choosing the numerically nearest future row would make
        // the preserved phase flip sign as the viewport crosses a midpoint.
        const auto after = std::upper_bound(
            _layoutBands.cbegin(), _layoutBands.cend(), planned,
            [](qreal value, const LayoutBand &band) {
                return value < band.top;
            });
        sourceBandIndex = after == _layoutBands.cbegin()
            ? 0 : int(std::distance(_layoutBands.cbegin(), after)) - 1;
    }
    if (sourceBandIndex < 0) {
        return result;
    }
    const LayoutBand &sourceBand = _layoutBands.at(sourceBandIndex);
    if (!hasViewportRow) {
        rowViewportY = sourceBand.top - planned;
    }

    const int firstCandidate = pageDirection < 0 ? 0 : sourceBandIndex + 1;
    const int lastCandidate = pageDirection < 0
        ? sourceBandIndex - 1 : _layoutBands.size() - 1;
    const qreal idealTop = sourceBand.top + pageDirection * distance;
    const int targetBandIndex = closestBand(
        idealTop, firstCandidate, lastCandidate,
        pageDirection < 0);

    qreal destination = pageDirection < 0 ? 0 : maximum;
    qreal targetBandTop = sourceBand.top;
    if (targetBandIndex >= 0) {
        targetBandTop = _layoutBands.at(targetBandIndex).top;
        destination = targetBandTop - rowViewportY;
    }
    const bool terminalClamp = targetBandIndex < 0 ||
        destination < 0 || destination > maximum;
    destination = qBound<qreal>(0, destination, maximum);

    const qreal epsilon = 0.01;
    const bool atStart = destination <= epsilon;
    const bool atEnd = destination >= maximum - epsilon;
    const qreal lastProbeY = qMax<qreal>(
        0, std::nextafter(_contentHeight,
                          -std::numeric_limits<qreal>::infinity()));
    const qreal probeY = qBound<qreal>(
        0, destination + itemViewportY, lastProbeY);
    int targetIndex = indexAt(anchorX, probeY);

    // At an interior row boundary a retained viewport Y can occasionally hit
    // an exact horizontal/vertical edge. Resolve that ambiguity within the
    // closest actual row rather than spuriously jumping to a catalog edge.
    if (targetIndex < 0 && !atStart && !atEnd) {
        const int probeBandIndex = closestBand(
            probeY, 0, _layoutBands.size() - 1);
        if (probeBandIndex >= 0) {
            qreal closestDistance = std::numeric_limits<qreal>::max();
            for (const int index : _layoutBands.at(probeBandIndex).indexes) {
                const QRectF geometry = _bricks.at(index).geometry();
                const qreal horizontalDistance =
                    anchorX < geometry.left()
                        ? geometry.left() - anchorX
                        : anchorX > geometry.right()
                            ? anchorX - geometry.right() : 0;
                if (horizontalDistance < closestDistance) {
                    closestDistance = horizontalDistance;
                    targetIndex = index;
                }
            }
        }
    }
    if (targetIndex < 0) {
        targetIndex = pageDirection < 0 ? 0 : _bricks.size() - 1;
    }

    // Retain the established MasonryMode partial-terminal behavior. Reaching
    // the content clamp does not force the last model item unless the retained
    // X/Y probe cannot advance; a following Page press can still adopt the
    // true terminal item and reset the physical anchor deliberately.
    bool hitStart = targetIndex == 0;
    bool hitEnd = targetIndex >= _bricks.size() - 1;
    if (pageDirection < 0 && targetIndex == currentIndex && atStart) {
        targetIndex = indexAt(anchorX, qMin<qreal>(lastProbeY, 1));
        hitStart = true;
        if (targetIndex == currentIndex || targetIndex < 0) {
            targetIndex = 0;
        }
    }
    else if (pageDirection > 0 && targetIndex == currentIndex && atEnd) {
        targetIndex = indexAt(anchorX, lastProbeY);
        hitEnd = true;
        if (targetIndex < 0) {
            targetIndex = indexAt(
                anchorX,
                qMax<qreal>(0, _contentHeight -
                               effectiveTargetExtent() * 0.5));
        }
        if (targetIndex == currentIndex || targetIndex < 0) {
            targetIndex = _bricks.size() - 1;
        }
    }
    targetIndex = qBound(0, targetIndex, _bricks.size() - 1);
    hitStart = hitStart || targetIndex == 0;
    hitEnd = hitEnd || targetIndex == _bricks.size() - 1;

    result[QStringLiteral("valid")] = true;
    result[QStringLiteral("targetIndex")] = targetIndex;
    result[QStringLiteral("contentY")] = destination;
    result[QStringLiteral("rowViewportY")] = rowViewportY;
    result[QStringLiteral("sourceBandIndex")] = sourceBandIndex;
    result[QStringLiteral("targetBandIndex")] = targetBandIndex;
    result[QStringLiteral("sourceBandTop")] = sourceBand.top;
    result[QStringLiteral("targetBandTop")] = targetBandTop;
    result[QStringLiteral("hitEdge")] = hitStart || hitEnd;
    result[QStringLiteral("terminalClamp")] = terminalClamp;
    return result;
}

int MasonryLayout::windowTopIndexForIndex(int index) const {
    if (_presentationMode != Columns || _bricks.isEmpty()) {
        return 0;
    }
    index = qBound(0, index, _bricks.size() - 1);
    const int rows = rowsPerColumn();
    const int columnTop = qBound(0, (index / rows) * rows,
                                 maximumWindowTopIndex());
    return windowTopIndexForContentY(
        contentYForWindowTopIndex(columnTop));
}

void MasonryLayout::reReadAndDecodeThumbnails() {
    _currentLoadingRow.clear();
    _scheduledThumbnailIndexes.clear();
    _scheduledThumbnailRequestKeys.clear();
    _lastThumbnailViewportIndexes.clear();
    if (!isEmbedded() && _model) {
        dynamic_cast<ThumbnailsRequestInterface *>(_model)->cancelAllDecodeRunners();
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
    if (_bricks.isEmpty()) {
        return;
    }

    if (_currentIndex >= 0 && _currentIndex < _bricks.size() &&
        _bricks[_currentIndex].image) {
        _preservedCurrentItemFullPath =
            _bricks[_currentIndex].image->fullPath();
        _preservedCurrentFallbackIndex = _currentIndex;
    }

    const int anchorIndex = _topItem;
    if (anchorIndex >= 0 && anchorIndex < _bricks.size() &&
        _bricks[anchorIndex].image) {
        const MasonryBrick &anchorBrick = _bricks[anchorIndex];
        _preservedViewportAnchorFullPath =
            anchorBrick.image->fullPath();
        _preservedViewportAnchorFallbackIndex = anchorIndex;
        _preservedViewportAnchorOffset = anchorBrick.y - _contentY;
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

    auto *requestModel = dynamic_cast<ThumbnailsRequestInterface *>(_model);
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

BrickItem *MasonryLayout::createComponent() {
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

    if (!_delegate) {
        qDebug() << "Empty delegate";
    }

    if (_delegate->status() != QQmlComponent::Ready) {
        qDebug() << "Error in component:" << _delegate->status() << _delegate->errors();
    }

    BrickItem *object = qobject_cast<BrickItem*>(_delegate->create(QQmlEngine::contextForObject(this)));
    object->setParentItem(_viewport);
    object->setParent(_viewport);
    return object;
}

bool MasonryLayout::isEmbedded() const {
    return _model && dynamic_cast<ThumbnailsRequestInterface *>(_model)->rootItem() != nullptr;
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
    if (!qIsFinite(density)) {
        density = mode == Masonry ? 150.0
            : mode == Grid ? 160.0
            : mode == Icons ? 64.0 : 30.0;
    }
    switch (mode) {
    case Masonry:
        return qBound<qreal>(30.0, density, 500.0);
    case Columns:
    case Details:
        return qBound<qreal>(22.0, density, 72.0);
    case Grid:
        return qBound<qreal>(96.0, density, 320.0);
    case Icons:
        return qBound<qreal>(18.0, density, 256.0);
    }
    return density;
}

qreal MasonryLayout::effectiveTargetExtent() const {
    return qMax<qreal>(1.0, _density);
}

int MasonryLayout::effectiveColumnCount() const {
    if (_presentationMode == Columns) {
        return qBound(2, _columnCount, 3);
    }
    if (_presentationMode == Grid || _presentationMode == Icons) {
        const qreal extent = qMax<qreal>(1.0, effectiveTargetExtent());
        return qMax(1, static_cast<int>(std::floor(
            qMax<qreal>(1.0, width() - _paddingLeft - _paddingRight) /
            extent)));
    }
    return 1;
}

int MasonryLayout::rowsPerColumn() const {
    const qreal usableHeight = qMax<qreal>(
        1.0, height() - _paddingTop - _paddingBottom);
    return qMax(1, static_cast<int>(std::floor(
        usableHeight / qMax<qreal>(1.0, effectiveTargetExtent()))));
}

int MasonryLayout::maximumWindowTopIndex() const {
    if (_presentationMode != Columns) {
        return 0;
    }
    const int rows = rowsPerColumn();
    return _bricks.isEmpty()
        ? 0 : ((_bricks.size() - 1) / rows) * rows;
}

qreal MasonryLayout::contentYForWindowTopIndex(int index) const {
    const int rows = rowsPerColumn();
    const qreal canvasWidth = qMax<qreal>(
        0, width() - _paddingLeft - _paddingRight);
    const qreal cellWidth = canvasWidth / effectiveColumnCount();
    const int firstColumn = qBound(0, index, maximumWindowTopIndex()) / rows;
    return qBound<qreal>(0, firstColumn * cellWidth,
                         maximumContentOffset());
}

int MasonryLayout::windowTopIndexForContentY(qreal contentY) const {
    const int rows = rowsPerColumn();
    const qreal canvasWidth = qMax<qreal>(
        0, width() - _paddingLeft - _paddingRight);
    const qreal cellWidth = canvasWidth / effectiveColumnCount();
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
    const qreal canvasWidth = qMax<qreal>(
        0, width() - _paddingLeft - _paddingRight);
    const qreal extent = effectiveTargetExtent();
    const qreal halfSpacing = _spacing / 2.0;
    const int columns = effectiveColumnCount();

    const auto resetBrick = [](MasonryBrick &brick) {
        brick.normalizedSize = {};
        brick.previewGeometry = {};
        brick.x = 0;
        brick.y = 0;
        brick.row = -1;
        brick.column = -1;
        brick.iconLabelText.clear();
    };

    if (_presentationMode == Columns) {
        // One continuous column-major strip: fill a column top-to-bottom,
        // continue in the next column, and translate the strip horizontally.
        // The configured count controls how many columns fit in the viewport;
        // it does not divide the catalog into atomic virtual pages.
        const int rows = rowsPerColumn();
        const qreal cellWidth = columns > 0
            ? canvasWidth / columns : canvasWidth;
        for (int index = 0; index < _bricks.size(); ++index) {
            MasonryBrick &brick = _bricks[index];
            const int rowInColumn = index % rows;
            brick.row = rowInColumn;
            brick.column = index / rows;
            brick.x = brick.column * cellWidth;
            brick.y = _paddingTop + rowInColumn * extent;
            brick.normalizedSize = QSizeF(cellWidth, extent);
            const qreal side = qMax<qreal>(
                1, qMin(extent - _spacing, cellWidth - _spacing));
            brick.previewGeometry = QRectF(
                brick.x + halfSpacing, brick.y + halfSpacing, side, side);
        }
        return;
    }

    for (MasonryBrick &brick : _bricks) {
        resetBrick(brick);
    }

    if (_presentationMode == Details) {
        for (int index = 0; index < _bricks.size(); ++index) {
            MasonryBrick &brick = _bricks[index];
            brick.row = index;
            brick.column = 0;
            brick.x = 0;
            brick.y = _paddingTop + index * extent;
            brick.normalizedSize = QSizeF(canvasWidth, extent);
            const qreal side = qMax<qreal>(
                1, qMin(extent - _spacing, canvasWidth - _spacing));
            brick.previewGeometry = QRectF(
                brick.x + halfSpacing, brick.y + halfSpacing, side, side);
        }
        return;
    }

    const qreal cellWidth = columns > 0 ? canvasWidth / columns : canvasWidth;
    if (_presentationMode == Icons) {
        const int rowCount = columns > 0
            ? (_bricks.size() + columns - 1) / columns : 0;
        // The distributed cell width can be slightly larger than density
        // because the remaining canvas pixels are shared between columns.
        // Use that real width as the one-line baseline so an Icons brick is
        // exactly square; wrapped labels only grow the row vertically.
        QVector<qreal> rowHeights(rowCount, cellWidth);
        const QFont labelFont = _iconLabelFont;
        IconTextMeasurer textMeasurer(labelFont);
        const qreal labelWidth = qMax<qreal>(1, cellWidth - 8);
        // A one-line Icons brick is exactly square. Reserve precisely one
        // rendered line plus the label gaps; the preview consumes the full
        // cell width and starts at the cell origin. Additional label lines
        // grow the brick below this square baseline and never shrink the
        // preview. In particular, do not leave the former card inset around
        // the icon/thumbnail.
        const qreal oneLinePreviewHeight = qMax<qreal>(
            1, cellWidth - 3 - textMeasurer.lineHeight() - 3);

        for (int index = 0; index < _bricks.size(); ++index) {
            MasonryBrick &brick = _bricks[index];
            const QString sourceText = brick.image
                ? brick.image->text() : QString();
            const IconLabelLayout label = textMeasurer.layout(
                sourceText, labelWidth);
            brick.iconLabelText = label.text;
            const qreal requiredHeight = oneLinePreviewHeight + 3
                + label.height + 3;
            const int row = index / columns;
            rowHeights[row] = qMax(rowHeights[row], requiredHeight);
        }

        QVector<qreal> rowTops(rowCount, _paddingTop);
        for (int row = 1; row < rowCount; ++row) {
            rowTops[row] = rowTops[row - 1] + rowHeights[row - 1];
        }
        for (int index = 0; index < _bricks.size(); ++index) {
            MasonryBrick &brick = _bricks[index];
            brick.row = index / columns;
            brick.column = index % columns;
            brick.x = brick.column * cellWidth;
            brick.y = rowTops[brick.row];
            brick.normalizedSize = QSizeF(cellWidth,
                                          rowHeights[brick.row]);
            brick.previewGeometry = QRectF(
                brick.x, brick.y, cellWidth, oneLinePreviewHeight);
        }
        return;
    }

    for (int index = 0; index < _bricks.size(); ++index) {
        MasonryBrick &brick = _bricks[index];
        brick.row = index / columns;
        brick.column = index % columns;
        brick.x = brick.column * cellWidth;
        brick.y = _paddingTop + brick.row * extent;
        brick.normalizedSize = QSizeF(cellWidth, extent);

        const QRectF inner = brick.geometry().adjusted(
            halfSpacing, halfSpacing, -halfSpacing, -halfSpacing);
        const qreal labelHeight = qMin<qreal>(
            34, qMax<qreal>(18, extent * (_presentationMode == Icons
                                              ? 0.24 : 0.20)));
        const qreal availableHeight = qMax<qreal>(
            1, inner.height() - labelHeight);
        brick.previewGeometry = QRectF(
            inner.left(), inner.top(), inner.width(), availableHeight);
    }
}

void MasonryLayout::rebuildLayoutBands() {
    _layoutBands.clear();
    QMap<int, LayoutBand> byRow;
    int firstIndex = 0;
    int endIndex = _bricks.size();
    for (int index = firstIndex; index < endIndex; ++index) {
        const MasonryBrick &brick = _bricks.at(index);
        const QRectF geometry = brick.geometry();
        if (!geometry.isValid() || geometry.isEmpty()) {
            continue;
        }
        LayoutBand &band = byRow[brick.row];
        if (band.indexes.isEmpty()) {
            band.row = brick.row;
            band.top = geometry.top();
            band.bottom = geometry.bottom();
        }
        else {
            band.top = qMin(band.top, geometry.top());
            band.bottom = qMax(band.bottom, geometry.bottom());
        }
        band.indexes.append(index);
    }
    _layoutBands = byRow.values();
    std::sort(_layoutBands.begin(), _layoutBands.end(),
              [](const LayoutBand &left, const LayoutBand &right) {
                  if (!qFuzzyCompare(left.top, right.top)) {
                      return left.top < right.top;
                  }
                  return left.bottom < right.bottom;
              });
    ++_layoutRevision;
    emit layoutRevisionChanged();
    emit layoutBandsChanged();
}

int MasonryLayout::bandIndexAt(qreal y) const {
    if (_layoutBands.isEmpty()) {
        return -1;
    }
    int low = 0;
    int high = _layoutBands.size();
    while (low < high) {
        const int middle = low + (high - low) / 2;
        if (_layoutBands.at(middle).bottom < y) {
            low = middle + 1;
        }
        else {
            high = middle;
        }
    }
    return low < _layoutBands.size() ? low : -1;
}

QList<int> MasonryLayout::indexesForVerticalRange(
    qreal top, qreal bottom) const {
    QList<int> indexes;
    if (bottom < top) {
        std::swap(top, bottom);
    }
    const int first = bandIndexAt(top);
    if (first < 0) {
        return indexes;
    }
    for (int bandIndex = first; bandIndex < _layoutBands.size(); ++bandIndex) {
        const LayoutBand &band = _layoutBands.at(bandIndex);
        if (band.top > bottom) {
            break;
        }
        if (band.bottom >= top) {
            indexes.append(band.indexes);
        }
    }
    return indexes;
}

void MasonryLayout::rewrap(bool animate) {
   // qDebug() << "rewrap" << width() << _bricks.size();
   //  if (width() <= 0) {
   //     qDebug() << "no rewrap, zero width";
   //     return;
   // }
    qreal currentIndexOffset = _currentIndexOffsetOverride;
    _currentIndexOffsetOverride = -1;
    if (currentIndexOffset == -1 && _currentIndex != -1 && _currentIndex >= _visibleStart && _currentIndex <= _visibleEnd) {
        currentIndexOffset = _contentY - _bricks[_currentIndex].y;
    }

    if (_presentationMode != Masonry) {
        const qreal oldContentY = _contentY;
        const bool preserveViewportAnchor =
            std::exchange(_preserveViewportAnchorForNextRewrap, false);
        // Fixed strategies used to retain only the raw numeric contentY
        // while density changed.  That coordinate has no stable meaning when
        // Grid/Icons change their column count or row pitch (and Columns may
        // change rows per column), so pinch zoom visibly jumped to unrelated
        // entries.  setContentY() continuously tracks one semantic viewport
        // anchor and its exact fractional offset; carry that same brick
        // through every rewrap just like the original Masonry path does.
        int viewportAnchorIndex = preserveViewportAnchor ? _topItem : -1;
        qreal viewportAnchorOffset = preserveViewportAnchor
            ? _topItemOffset : 0;
        if (preserveViewportAnchor && _presentationMode != Columns) {
            // Anchor the row actually crossing the top edge, including a
            // partially clipped row.  _topItem intentionally points at the
            // first row beginning below contentY for historical Masonry
            // navigation, which is not the visual contract expected from a
            // regular Grid/Icons/Details zoom.
            constexpr qreal edgeEpsilon = 0.001;
            for (const LayoutBand &band : std::as_const(_layoutBands)) {
                if (band.indexes.isEmpty() ||
                    band.bottom <= _contentY + edgeEpsilon) {
                    continue;
                }
                viewportAnchorIndex = band.indexes.constFirst();
                viewportAnchorOffset = band.top - _contentY;
                break;
            }
        }
        const qreal extent = effectiveTargetExtent();
        const int columns = effectiveColumnCount();
        int virtualRows = 0;
        if (_presentationMode == Details) {
            virtualRows = _bricks.size();
        }
        else if (_presentationMode == Columns) {
            const int rows = rowsPerColumn();
            const int totalColumns = rows > 0
                ? (_bricks.size() + rows - 1) / rows : 0;
            setContentHeight(_paddingLeft + totalColumns
                             * (qMax<qreal>(0, width() - _paddingLeft
                                           - _paddingRight) / columns)
                             + _paddingRight);
        }
        else {
            virtualRows = columns > 0
                ? (_bricks.size() + columns - 1) / columns : 0;
        }
        if (_presentationMode != Icons && _presentationMode != Columns) {
            setContentHeight(_paddingTop + virtualRows * extent +
                             _paddingBottom);
        }
        if (_presentationMode == Columns) {
            _contentY = qBound<qreal>(0, _contentY, maximumContentOffset());
        }
        else if (_presentationMode != Icons) {
            _contentY = qBound<qreal>(
                0, _contentY, qMax<qreal>(0, _contentHeight - height()));
        }
        calcFixedLayout();
        if (_presentationMode == Icons) {
            const qreal iconContentHeight = _bricks.isEmpty()
                ? 0
                : _bricks.constLast().geometry().bottom() + _paddingBottom;
            setContentHeight(iconContentHeight);
            _contentY = qBound<qreal>(
                0, _contentY, qMax<qreal>(0, _contentHeight - height()));
        }
        rebuildLayoutBands();
        if (viewportAnchorIndex >= 0 &&
            viewportAnchorIndex < _bricks.size()) {
            const QRectF anchorGeometry =
                _bricks.at(viewportAnchorIndex).geometry();
            if (anchorGeometry.isValid() && !anchorGeometry.isEmpty()) {
                const qreal anchorPosition = _presentationMode == Columns
                    ? anchorGeometry.left() : anchorGeometry.top();
                _contentY = qBound<qreal>(
                    0, anchorPosition - viewportAnchorOffset,
                    maximumContentOffset());
                _topItem = viewportAnchorIndex;
                _topItemOffset = anchorPosition - _contentY;
            }
        }
        if (_presentationMode == Columns) {
            updateWindowTopFromContentY();
        }
        positionViewport();
        updateProperties(animate);
        // A strategy switch can preserve the same numeric content height
        // while changing from an embedded Masonry surface (which owns no
        // scrollbar) to a fixed viewport. Re-evaluate overflow even when
        // setContentHeight() therefore emitted no change.
        updateNeedScroll();
        if (!qFuzzyCompare(oldContentY, _contentY)) {
            emit contentYChanged();
        }
        return;
    }

    calcLayout(_bricks, width() - _paddingLeft - _paddingRight, _targetHeight, _spacing, !_listView, _paddingTop, layoutMode());
    for (MasonryBrick &brick : _bricks) {
        const QRectF geometry = brick.geometry();
        brick.previewGeometry = geometry.isValid() && !geometry.isEmpty()
            ? geometry.adjusted(_spacing / 2.0, _spacing / 2.0,
                                -_spacing / 2.0, -_spacing / 2.0)
            : QRectF();
    }
    rebuildLayoutBands();
   // qDebug() << "--------------------";
   // for (int i = 0; i < _bricks.size(); i++) {
   //     qDebug() << _bricks[i].image->fullPath() << _bricks[i].originalSize << _bricks[i].normalizedSize;
   // }


    if (_bricks.size()) {
        // Preserve the original Masonry endpoint contract.  Before
        // contentHeight became qreal, this expression was passed to an int
        // setter and therefore truncated toward zero.  Details needs exact
        // fractional extents, but changing Masonry's terminal pixel would
        // subtly alter its scrollbar thumb and end-of-list navigation.
        setContentHeight(static_cast<int>(
            _bricks.last().y + _bricks.last().normalizedSize.height()
            + _paddingBottom));
    }
    else {
        setContentHeight(0);
    }

    qreal newContentY = _contentY;
    // If selected index is on screen, we keep view relative to it. Otherwise, we keep top item
    if (currentIndexOffset != -1) {
        newContentY = qMax<qreal>(0, qMin<qreal>(_bricks[_currentIndex].y + currentIndexOffset, contentHeight() - height()));
    }
    else {
        if (_topItem < _bricks.size()) {
            newContentY = qMax<qreal>(0, qMin<qreal>(_bricks[_topItem].y - _topItemOffset, contentHeight() - height()));
        }
    }
    if (newContentY != _contentY) {
        setContentYInternal(newContentY);
    }
    else {
        updateProperties(animate);
    }
}

QSizeF scaleToWidthWithSpacing(const QSizeF &size, qreal toWidth, int spacing) {
    qreal aspect = size.width() / size.height();
    return QSizeF(qMax(0.0, toWidth), qMax(0.0, (toWidth - spacing) / aspect + spacing));
}

QSizeF scaleToHeightWithSpacing(const QSizeF &size, qreal toHeight, int spacing) {
    qreal aspect = size.width() / size.height();
    return QSizeF((toHeight - spacing) * aspect + spacing, toHeight);
}

qreal MasonryLayout::scaleRow(QList<MasonryBrick> &bricks, int canvasWidth, int rowTargetHeight, int spacing,
                             int lastRowIndex, qreal rowHeight) {
    int i = lastRowIndex;
    int bricksInRow = bricks[i].column + 1;

    if (!rowHeight) {
        qreal totalWidthWithoutSpacing = bricks[i].x + bricks[i].normalizedSize.width() - (bricksInRow * spacing);
        qreal stretchFactor = (canvasWidth - bricksInRow * spacing) / totalWidthWithoutSpacing;
        rowHeight = (rowTargetHeight - spacing) * stretchFactor + spacing;
    }

    for (int rowIndex = i - bricksInRow + 1; rowIndex <= i; rowIndex++) {
        bricks[rowIndex].normalizedSize = scaleToHeightWithSpacing(bricks[rowIndex].normalizedSize -
                                                                   QSize(spacing, spacing), rowHeight, spacing);
        if (bricks[rowIndex].column) {
            bricks[rowIndex].x = bricks[rowIndex - 1].x + bricks[rowIndex - 1].normalizedSize.width();
        }
    }
    return rowHeight;
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

    canvasWidth = qMax(0, canvasWidth);
    int currentRow = 0;
    int currentColumn = 0;
    qreal lastX = 0;
    qreal lastY = paddingTop;
    // qDebug() << "REWRAP -----------------";

    for (int i = 0; i < bricks.size(); i++) {
        if (bricks[i].originalSize.width() == 0 && bricks[i].originalSize.height() == 0) {
            bricks[i].normalizedSize = QSizeF(canvasWidth, rowTargetHeight);
        }
        else if (bricks[i].originalSize.width() == 0 && bricks[i].originalSize.height() != 0) {
            bricks[i].normalizedSize = QSizeF(canvasWidth, bricks[i].originalSize.height());
        }
        else {
            bricks[i].normalizedSize = scaleToHeightWithSpacing(bricks[i].originalSize, rowTargetHeight, spacing);
        }
        bricks[i].row = currentRow;
        bricks[i].column = currentColumn;
        bricks[i].x = lastX;
        bricks[i].y = lastY;

        // qDebug() << i << bricks[i].row << bricks[i].column;
        bool lineBreak = false;
        if (i) {
            lineBreak = bricks[i - 1].temporaryLineBreakAfter || bricks[i - 1].lineBreakAfter;
            bricks[i - 1].temporaryLineBreakAfter = false;
        }

        // Row is not filled enough yet, growing
        if (lastX + bricks[i].normalizedSize.width() < canvasWidth && (!lineBreak || !bricks[i].column)
            || layoutMode == CalcLayoutSingleRow) {
            lastX += bricks[i].normalizedSize.width();

            // Last row should have the same height as the previous one, or just fit in width if last height is too much
            if (i == bricks.size() - 1 && currentRow && lastRowMatchesPrevious) {
                for (int rowIndex = i-1; rowIndex >= 0; rowIndex--) {
                    if (bricks[rowIndex].row != currentRow) {
                        scaleRow(bricks, canvasWidth, rowTargetHeight, spacing,
                                 i, bricks[rowIndex].normalizedSize.height());
                        if (bricks[i].x + bricks[i].normalizedSize.width() > canvasWidth) {
                            for (int j = rowIndex + 1; j <= i; j++) {
                                if (bricks[j].column) {
                                    bricks[j].x = bricks[j - 1].x + bricks[j - 1].normalizedSize.width();
                                }
                                bricks[j].normalizedSize = scaleToHeightWithSpacing(bricks[j].originalSize,
                                                                                    rowTargetHeight, spacing);
                            }
                            scaleRow(bricks, canvasWidth, rowTargetHeight, spacing, i);
                        }
                        break;
                    }
                }
            }
        }
        else if (!currentColumn) { // Single-item row
            if (bricks[i].originalSize.width() != 0 && bricks[i].originalSize.height() != 0) {
                bricks[i].normalizedSize = scaleToWidthWithSpacing(bricks[i].originalSize, canvasWidth, spacing);
            }

            currentRow++;
            currentColumn = -1;
            lastX = 0;

            if (i != bricks.size() - 1) {
                lastY += bricks[i].normalizedSize.height();
            }
        } // Can't grow more, expanding current row and advancing to the next one
        else {
            if (currentColumn != bricks[i-1].column + 1 || currentColumn != bricks[i].column) {
                qDebug() << "------------- ALARM ALARM!!!!" << currentColumn << bricks[i-1].column + 1;
            }
            qreal newRowHeight;
            if (layoutMode == CalcLayoutMasonry) {
                newRowHeight = scaleRow(bricks, canvasWidth, rowTargetHeight, spacing, i - 1);
            }
            else {
                newRowHeight = rowTargetHeight;
            }
            currentRow++;
            currentColumn = -1;
            lastX = 0;
            lastY += newRowHeight;
            i--;
        }

        currentColumn++;

    }
}

QString rectToString(QRectF rect) {
    QRect rectI = rect.toRect();
    return QString("%1,%2\n%3x%4").arg(rectI.x()).arg(rectI.y()).arg(rectI.width()).arg(rectI.height());
}

void MasonryLayout::updateProperties(bool animate) {
    updateViewportIndexSets();

    QList<int> delegateIndexes;
    if (_presentationMode == Columns) {
        const qreal left = _contentY - 46;
        const qreal right = _contentY + width() + 46;
        for (int index = 0; index < _bricks.size(); ++index) {
            const QRectF geometry = _bricks.at(index).geometry();
            if (geometry.right() >= left && geometry.left() <= right) {
                delegateIndexes.append(index);
            }
        }
    }
    else {
        delegateIndexes = indexesForVerticalRange(
            _contentY - 46, _contentY + height() + 46);
    }
    const QSet<int> delegateIndexSet(delegateIndexes.cbegin(),
                                     delegateIndexes.cend());
    QSet<BrickItem *> itemsToHide;
    const QSet<int> oldActiveIndexes = _activeBrickIndexes;
    for (const int index : oldActiveIndexes) {
        if (delegateIndexSet.contains(index) || index < 0 ||
            index >= _bricks.size()) {
            continue;
        }
        if (_bricks[index].item) {
            pushBrickItem(_bricks[index].item);
            itemsToHide.insert(_bricks[index].item);
            _bricks[index].item = nullptr;
        }
        _activeBrickIndexes.remove(index);
    }

    for (const int i : delegateIndexes) {
        if (i >= 0 && i < _bricks.size() &&
            _bricks[i].normalizedSize.isValid() &&
            !_bricks[i].normalizedSize.isEmpty()) {
            bool itemPopped = false;
            if (!_bricks[i].item) {
                itemPopped = true;
                if (isEmbedded()) {
                    // qDebug() << "POP ITEM" << i << dynamic_cast<ThumbnailsRequestInterface *>(_model)->rootItem();
                }
                _bricks[i].item = popBrickItem();
                _bricks[i].item->setProperty("model", QVariant::fromValue(_bricks[i].image));
                _bricks[i].item->setProperty("viewIndex", i);
                _bricks[i].item->setProperty("sourceIndex", _bricks[i].image ? _bricks[i].image->index() : -1);
            }
            else {
                _bricks[i].item->setProperty("viewIndex", i);
                _bricks[i].item->setProperty("sourceIndex", _bricks[i].image ? _bricks[i].image->index() : -1);
            }
            _bricks[i].item->setRowColumn(_bricks[i].row, _bricks[i].column);
            _bricks[i].item->setIconLabelText(
                _bricks[i].iconLabelText);
            _bricks[i].item->setPreviewRect(
                _bricks[i].previewGeometry.translated(
                    -_bricks[i].x, -_bricks[i].y));

            // The classic Details view deliberately derives a fractional row
            // extent from the host font metrics (for example 24.2 logical
            // pixels in f4).  Snapping each row independently makes that
            // fractional phase accumulate as visible drift.  Other modes keep
            // their established pixel-snapped delegate geometry.
            const bool preserveFractionalGeometry =
                _presentationMode == Details;
            const bool geometryDiffers = preserveFractionalGeometry
                ? _bricks[i].item->geometry() != _bricks[i].geometry()
                : roundRect(_bricks[i].item->geometry()) !=
                      roundRect(_bricks[i].geometry());

            if (!_animateResizing) {
                if (geometryDiffers) {
                    _bricks[i].item->setGeometry(
                        _bricks[i].geometry(), false,
                        !preserveFractionalGeometry);
                }
            } else {
                if (itemPopped || geometryDiffers) {
                    bool animateGeometry = _bricks[i].item->isVisible() && animate && _bricks[i].item->geometry().isValid();
                    if (animateGeometry) {
                        if (isEmbedded() && dynamic_cast<ThumbnailsRequestInterface *>(_model)->rootItem()->fileName() == "2015.07.09 Каланча") {
                            qDebug() << "ANIMATE" << i << dynamic_cast<ThumbnailsRequestInterface *>(_model)->rootItem() <<
                                roundRect(_bricks[i].item->geometry()) << roundRect(_bricks[i].geometry()) <<
                                _bricks[i].image->info().path;
                        }
                    }
                    _bricks[i].item->setGeometry(
                        _bricks[i].geometry(), animateGeometry,
                        !preserveFractionalGeometry);
                }
            }

            _activeBrickIndexes.insert(i);

            if (itemsToHide.contains(_bricks[i].item)) {
                itemsToHide.remove(_bricks[i].item);
            }
            else {
                if (!_bricks[i].item->isVisible()) {
                    _bricks[i].item->setVisible(true);
                }
            }
        }
    }

    for (BrickItem *item : itemsToHide) {
        item->setVisible(false);
        item->setProperty("model", QVariant());
        item->setProperty("viewIndex", -1);
        item->setProperty("sourceIndex", -1);
    }
}

void MasonryLayout::updateViewportIndexSets() {
    QList<int> visible;
    QList<int> overscan;
    if (_presentationMode == Columns) {
        const qreal viewportWidth = qMax<qreal>(1, width());
        const qreal visibleLeft = _contentY;
        const qreal visibleRight = _contentY + width();
        const qreal overscanLeft = visibleLeft - viewportWidth;
        const qreal overscanRight = visibleRight + viewportWidth;
        for (int index = 0; index < _bricks.size(); ++index) {
            const QRectF geometry = _bricks.at(index).geometry();
            if (geometry.right() >= overscanLeft
                    && geometry.left() <= overscanRight) {
                overscan.append(index);
                if (geometry.right() >= visibleLeft
                        && geometry.left() <= visibleRight) {
                    visible.append(index);
                }
            }
        }
    }
    else {
        visible = indexesForVerticalRange(_contentY,
                                          _contentY + height());
        const qreal viewportHeight = qMax<qreal>(1, height());
        overscan = indexesForVerticalRange(
            _contentY - viewportHeight,
            _contentY + height() + viewportHeight);
    }
    const QSet<int> nextVisible(visible.cbegin(), visible.cend());
    const QSet<int> nextOverscan(overscan.cbegin(), overscan.cend());
    const bool visibleChanged = nextVisible != _visibleIndexSet;
    const bool overscanChanged = nextOverscan != _overscanIndexSet;
    _visibleIndexSet = nextVisible;
    _overscanIndexSet = nextOverscan;

    if (_visibleIndexSet.isEmpty()) {
        _visibleStart = -1;
        _visibleEnd = -1;
    }
    else {
        const auto bounds = std::minmax_element(
            _visibleIndexSet.cbegin(), _visibleIndexSet.cend());
        _visibleStart = *bounds.first;
        _visibleEnd = *bounds.second;
    }
    if (visibleChanged) {
        emit visibleIndexesChanged();
    }
    if (overscanChanged) {
        emit overscanIndexesChanged();
    }
    if (visibleChanged || overscanChanged) {
        planViewportThumbnails(_overscanIndexSet);
    }
}

void MasonryLayout::planThumbnailForIndex(
    int index, bool highPriority, QList<ImageDecodeRequest> &requests,
    bool force) {
    if (!_model || index < 0 || index >= _bricks.size()) {
        return;
    }
    MasonryBrick &brick = _bricks[index];
    ImageFile *image = brick.image;
    if (!image || !image->isImage() || !image->fullSize().isValid()) {
        return;
    }
    QSizeF previewSize = brick.previewGeometry.size();
    if ((!previewSize.isValid() || previewSize.isEmpty()) &&
        _presentationMode == Columns) {
        const qreal cellWidth = qMax<qreal>(
            1, (width() - _paddingLeft - _paddingRight) /
                   effectiveColumnCount());
        const qreal side = qMax<qreal>(
            1, qMin(effectiveTargetExtent() - _spacing,
                    cellWidth - _spacing));
        previewSize = QSizeF(side, side);
    }
    if (!previewSize.isValid() || previewSize.isEmpty()) {
        return;
    }
    const QSize targetSize = previewDecodeTargetSize(brick, previewSize);
    if (!targetSize.isValid() || targetSize.width() <= 0 ||
        targetSize.height() <= 0) {
        return;
    }
    const ImageInfo info = image->info();
    const QString transformKey = previewTransformKey();
    const bool sameSource =
        brick.lastPlannedSourcePath == info.path &&
        brick.lastPlannedModified == info.lastModified &&
        brick.lastPlannedFileSize == info.fileSize &&
        brick.lastPlannedVersionToken == info.sourceVersionToken;
    const bool plannedFrameCovers = sameSource &&
        brick.lastPlannedTransformKey == transformKey &&
        brick.lastPlannedTargetSize.isValid() &&
        brick.lastPlannedTargetSize.width() >= targetSize.width() &&
        brick.lastPlannedTargetSize.height() >= targetSize.height();
    const auto rememberPlan = [&brick, &info, &transformKey](
                                  const QSize &plannedSize) {
        brick.lastPlannedTargetSize = plannedSize;
        brick.lastPlannedTransformKey = transformKey;
        brick.lastPlannedSourcePath = info.path;
        brick.lastPlannedModified = info.lastModified;
        brick.lastPlannedFileSize = info.fileSize;
        brick.lastPlannedVersionToken = info.sourceVersionToken;
    };
    // A published image at least as large as the exact preview rect is shared
    // by every presentation mode. If eviction cleared its provider ID, do not
    // trust the retained QImage alone: resubmit so the model/cache can publish
    // a fresh URL synchronously or schedule a decode.
    if (!force && !image->imageIdUrl().isEmpty()) {
        // External sessions publish shared-cache provider IDs without keeping
        // a second QImage copy in every ImageFile. That URL is authoritative:
        // source/version changes and LRU eviction clear it, while geometry or
        // DPR changes explicitly invoke the forced re-read path below.
        if (image->image().isNull() && plannedFrameCovers) {
            return;
        }
        if (image->imageMatchesSource(image->info()) &&
            image->image().width() >= targetSize.width() &&
            image->image().height() >= targetSize.height()) {
            rememberPlan(image->image().size());
            return;
        }
    }
    requests.append(ImageDecodeRequest{
        .info = info,
        .targetSize = targetSize,
        .viewerRequest = false,
        .checkCache = info.isCached,
        .highPriority = highPriority,
        .thumbnailTransformKey = transformKey,
    });
    rememberPlan(targetSize);
}

QSize MasonryLayout::previewDecodeTargetSize(
    const MasonryBrick &brick, const QSizeF &previewBounds) {
    if (!brick.image || !brick.image->fullSize().isValid() ||
        brick.image->fullSize().isEmpty() ||
        previewBounds.width() <= 0 || previewBounds.height() <= 0) {
        return {};
    }

    const QSize source = brick.image->fullSize();
    const QSizeF physicalBounds(
        dp(previewBounds.width()), dp(previewBounds.height()));
    if (physicalBounds.width() <= 0 || physicalBounds.height() <= 0) {
        return {};
    }

    const qreal horizontalScale = physicalBounds.width() / source.width();
    const qreal verticalScale = physicalBounds.height() / source.height();
    // Only Masonry gives the preview surface the source aspect ratio itself.
    // Every fixed presentation displays the complete image with
    // PreserveAspectFit, so decode the smallest aspect-preserving frame that
    // covers that fitted surface instead of the old Grid crop tier.
    qreal scale = qMin(horizontalScale, verticalScale);
    // Thumbnail decoders never need to synthesize pixels. If the native
    // image is smaller than the preview, publish native dimensions and let
    // the scene graph perform the unavoidable display upscale.
    scale = qBound<qreal>(0, scale, 1.0);
    if (scale <= 0) {
        return {};
    }

    const auto scaledDimension = [scale, this](int dimension) {
        const qreal scaled = dimension * scale;
        return qMax(1, qFloor(scaled));
    };
    return QSize(scaledDimension(source.width()),
                 scaledDimension(source.height()));
}

QString MasonryLayout::previewTransformKey() const {
    // Pixel processing is identical for every presentation: decode one
    // oriented, source-aspect frame and let the QML surface fit it.
    return QStringLiteral("thumbnail-aspect-v1");
}

void MasonryLayout::planViewportThumbnails(
    const QSet<int> &candidateIndexes, bool force) {
    auto *requestModel = dynamic_cast<ThumbnailsRequestInterface *>(_model);
    if (!requestModel || candidateIndexes.isEmpty() ||
        _cancelingThumbnailPlan) {
        return;
    }
    QSet<int> desiredIndexes;
    desiredIndexes.reserve(candidateIndexes.size());
    for (const int index : candidateIndexes) {
        if (index >= 0 && index < _bricks.size()) {
            desiredIndexes.insert(index);
        }
    }
    if (desiredIndexes.isEmpty()) {
        return;
    }
    const bool tracksViewportWindow = force ||
        desiredIndexes == _overscanIndexSet;

    const auto cancelThumbnailPlan = [&] {
        _scheduledThumbnailIndexes = desiredIndexes;
        _scheduledThumbnailRequestKeys.clear();
        _lastThumbnailViewportIndexes = desiredIndexes;
        _cancelingThumbnailPlan = true;
        requestModel->cancelAllDecodeRunners();
        _cancelingThumbnailPlan = false;
    };

    if (force) {
        _scheduledThumbnailIndexes = desiredIndexes;
        _scheduledThumbnailRequestKeys.clear();
    }
    else if (tracksViewportWindow) {
        QSet<int> overlap = _lastThumbnailViewportIndexes;
        overlap.intersect(desiredIndexes);
        QSet<int> scheduledUnion = _scheduledThumbnailIndexes;
        scheduledUnion.unite(desiredIndexes);
        const int scheduledLimit = qMax(
            desiredIndexes.size() + 1, desiredIndexes.size() * 3);
        const bool disjointJump =
            !_lastThumbnailViewportIndexes.isEmpty() && overlap.isEmpty();
        const bool queueWindowExceeded =
            scheduledUnion.size() > scheduledLimit;
        if (!isEmbedded() && (disjointJump || queueWindowExceeded)) {
            // DecodeManager has session-scoped thumbnail cancellation, while
            // Viewer requests live in a separate tier. Rebase only after a
            // disjoint jump or three accumulated overscan windows so small
            // smooth scrolls retain useful in-flight work.
            cancelThumbnailPlan();
            force = true;
        }
        else {
            _scheduledThumbnailIndexes = std::move(scheduledUnion);
        }
    }
    if (tracksViewportWindow) {
        _lastThumbnailViewportIndexes = desiredIndexes;
    }

    QList<int> visible;
    visible.reserve(desiredIndexes.size());
    QList<int> background;
    background.reserve(desiredIndexes.size());
    for (const int index : desiredIndexes) {
        if (_visibleIndexSet.contains(index)) {
            visible.append(index);
        }
        else {
            background.append(index);
        }
    }
    std::sort(visible.begin(), visible.end());
    std::sort(background.begin(), background.end());

    // Fixed layouts are fully deterministic without image metadata, so only
    // rows entering the active window need probes. Masonry additionally needs
    // every aspect ratio to settle justified rows, but that catalog-wide tier
    // is submitted once and behind the visible high-priority rows.
    requestModel->requestImageMetadata(visible, true, false);
    requestModel->requestImageMetadata(background, false, false);
    if (_presentationMode == Masonry) {
        // ExternalCatalogModel treats the cheap catalogWide marker as the
        // current presentation's background-scan lease. Every viewport/data
        // update first replaces visible/overscan queues with catalogWide=false,
        // so repeat the marker on every Masonry plan; otherwise an eviction or
        // metadata dataChanged would accidentally pause the catalog aspect
        // scan after its first batch.
        _catalogMetadataRequested = true;
        requestModel->requestImageMetadata({}, false, true);
    }

    const auto buildRequests = [&](bool forceRequests) {
        QList<ImageDecodeRequest> result;
        result.reserve(visible.size() + background.size());
        for (const int index : std::as_const(visible)) {
            planThumbnailForIndex(index, true, result, forceRequests);
        }
        for (const int index : std::as_const(background)) {
            planThumbnailForIndex(index, false, result, forceRequests);
        }
        return result;
    };
    const auto requestKeys = [](const QList<ImageDecodeRequest> &requests) {
        QSet<QString> keys;
        keys.reserve(requests.size());
        for (const ImageDecodeRequest &request : requests) {
            keys.insert(request.info.path + QChar(0x1f) +
                        QString::number(request.info.sourceVersionToken) +
                        QChar(0x1f) +
                        QString::number(request.info.fileSize) +
                        QChar(0x1f) +
                        QString::number(request.targetSize.width()) +
                        QLatin1Char('x') +
                        QString::number(request.targetSize.height()) +
                        QChar(0x1f) + request.thumbnailTransformKey);
        }
        return keys;
    };

    QList<ImageDecodeRequest> requests = buildRequests(force);
    QSet<QString> currentRequestKeys = requestKeys(requests);
    if (!force && tracksViewportWindow) {
        QSet<QString> requestUnion = _scheduledThumbnailRequestKeys;
        requestUnion.unite(currentRequestKeys);
        const int requestLimit = qMax(
            desiredIndexes.size() + 1, desiredIndexes.size() * 3);
        if (!isEmbedded() && requestUnion.size() > requestLimit) {
            // A continuous resize can keep the same model indexes while
            // producing successively larger exact targets. Bound those tiers
            // with the same three-window policy used for spatial scrolling.
            cancelThumbnailPlan();
            force = true;
            requests = buildRequests(true);
            currentRequestKeys = requestKeys(requests);
            _scheduledThumbnailRequestKeys = currentRequestKeys;
        }
        else {
            _scheduledThumbnailRequestKeys = std::move(requestUnion);
        }
    }
    else if (force) {
        _scheduledThumbnailRequestKeys = currentRequestKeys;
    }
    if (!requests.isEmpty()) {
        requestModel->decodeImages(requests);
    }
}

void MasonryLayout::setContentHeight(qreal newContentHeight) {
    if (qFuzzyCompare(_contentHeight, newContentHeight)) {
        return;
    }
    _contentHeight = newContentHeight;
    if (_viewport) {
        if (_presentationMode == Columns) {
            _viewport->setWidth(_contentHeight);
            _viewport->setHeight(height());
        }
        else {
            _viewport->setWidth(width());
            _viewport->setHeight(_contentHeight);
        }
    }
    emit contentHeightChanged();
    updateNeedScroll();
}

void MasonryLayout::onDataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight, const QVector<int> &roles) {
    if (!isEmbedded() && imageFileFromModelIndex(topLeft.parent()) != dynamic_cast<ThumbnailsRequestInterface *>(_model)->rootItem()) {
        return;
    }
    int index = topLeft.row();
    int indexTo = bottomRight.row();
    int changedIndexes = indexTo - index;
    if (index >= 0 && index < _bricks.size() && indexTo >= 0 &&
        indexTo < _bricks.size()) {
        if (_presentationMode != Masonry) {
            if (roles.isEmpty() ||
                roles.contains(FileListModel::ImageFullSizeRole)) {
                for (int row = index; row <= indexTo; ++row) {
                    if (_bricks[row].image &&
                        _bricks[row].image->fullSize().isValid()) {
                        _bricks[row].originalSize =
                            _bricks[row].image->fullSize();
                    }
                }
            }
            if (roles.isEmpty() ||
                roles.contains(FileListModel::ImageFullSizeRole) ||
                roles.contains(FileListModel::ImageIdUrlRole)) {
                QSet<int> candidates;
                for (int row = index; row <= indexTo; ++row) {
                    if (_overscanIndexSet.contains(row)) {
                        candidates.insert(row);
                    }
                }
                planViewportThumbnails(candidates);
            }
            return;
        }
        if (roles.isEmpty() ||
            roles.contains(FileListModel::ImageIdUrlRole)) {
            // ThumbnailMemoryCache eviction clears the provider URL on every
            // ImageFile that referenced the evicted frame. Re-plan only an
            // active masonry row; the retained QImage is deliberately not
            // treated as published until the cache/provider issues a fresh
            // ID. This is the same bounded path used by fixed layouts.
            QSet<int> candidates;
            for (int row = index; row <= indexTo; ++row) {
                if (_overscanIndexSet.contains(row)) {
                    candidates.insert(row);
                }
            }
            planViewportThumbnails(candidates);
        }
        if (!isEmbedded() &&
            modelRequestsViewStatePreservation(_model) &&
            (roles.contains(FileListModel::LastModifiedRole) ||
             roles.contains(FileListModel::FileSizeRole))) {
            // A watcher version refresh is followed asynchronously by an
            // ImageFullSizeRole batch. Treat that batch as an incremental
            // replacement; rebuilding the loading row from index 0 would
            // re-enqueue every thumbnail before the changed file.
            for (int i = index; i <= indexTo; ++i) {
                if (_bricks[i].image && _bricks[i].image->isImage()) {
                    _skipThumbnailBackfillUntilFlush = true;
                    break;
                }
            }
        }
        if (roles.contains(FileListModel::ImageFullSizeRole)) {
            bool animateLayoutChange = false;
            for (int i = index; i <= indexTo; i++) {
                if (_bricks[i].image && _bricks[i].image->fullSize().isValid()
                    && !_bricks[i].image->info().isCached) {
                    animateLayoutChange = true;
                    break;
                }
            }
            if (isEmbedded() || _skipThumbnailBackfillUntilFlush ||
                (changedIndexes > 1 && !_currentLoadingRow.size())) {
                for (int i = index; i <= indexTo; i++) {
                    if (_bricks[i].image && _bricks[i].image->fullSize().isValid()) {
                        _bricks[i].originalSize = _bricks[i].image->fullSize();
                    }
                }
                rewrap(animateLayoutChange);

                if (!isEmbedded()) {
                    QSet<int> candidates;
                    for (int row = index; row <= indexTo; ++row) {
                        if (_overscanIndexSet.contains(row)) {
                            candidates.insert(row);
                        }
                    }
                    planViewportThumbnails(candidates);
                }
            }
            else {
                // qDebug() << "ZZ LOADING ROW" << _currentLoadingRow.size();
                for (int i = index; i <= indexTo; i++) {
                    // TODO: this all comes too early when size is 3x2, fix somehow
                    pushToCurrentRow(i, animateLayoutChange);
                }
                // External catalogs intentionally keep the legacy incremental
                // row builder so layout rewraps remain batched. Thumbnail
                // pixels cannot wait for that batch's TimeToFlush marker,
                // though: worker completion order can deliver it before a
                // slower image, leaving the final active row stranded. Plan
                // just the changed overscan rows immediately; a later rewrap
                // will request a larger exact-size tier if its geometry grew.
                if (!isEmbedded() &&
                    modelRequestsViewStatePreservation(_model)) {
                    QSet<int> candidates;
                    for (int row = index; row <= indexTo; ++row) {
                        if (_overscanIndexSet.contains(row)) {
                            candidates.insert(row);
                        }
                    }
                    planViewportThumbnails(candidates);
                }
            }
        }
        if (roles.contains(FileListModel::FolderViewRole)) {
            QSize folderViewSize = _listView ? QSize(0, 0) : GridView_Folder.toSize();
            if (_bricks[index].originalSize != folderViewSize) {
                _bricks[index].originalSize = folderViewSize;
                rewrap(!_bricks[index].image->info().isCached);
            }
        }
        if (roles.contains(FileListModel::TimeToFlushRole)) {
            const bool animateLayoutChange = !_bricks[index].image || !_bricks[index].image->info().isCached;
            onThumbnailReadFinished(animateLayoutChange);
        }
    }
}
#include <QThread>
void MasonryLayout::pushToCurrentRow(int index, bool animate) {
    bool flushMode = index >= _bricks.count();
    if (!_currentLoadingRow.count() || index - _currentLoadingRow.last().globalIndex > 1) {
        int lastIndex = -1;
        if (_currentLoadingRow.count()) {
            lastIndex = _currentLoadingRow.last().globalIndex;
        }
        if (_skipThumbnailBackfillUntilFlush) {
            lastIndex = index - 1;
        }
        int indexToInsert = _currentLoadingRow.count();
        for (int i = index - 1; i >= 0; i--) {
            if (i > lastIndex) {
               // qDebug() << "adding index" << i << "from" << index << i << lastIndex;
                _currentLoadingRow.insert(indexToInsert, _bricks[i]);
                _currentLoadingRow[indexToInsert].globalIndex = i;
            }
            else {
                break;
            }
        }
    }
    if (!flushMode) {
        _currentLoadingRow.append(MasonryBrick {
            .originalSize = _bricks[index].image->fullSize(),
            .image = _bricks[index].image,
        });
                                  // (_bricks[index].image->fullSize().width(), _bricks[index].image->fullSize().height()));
        _currentLoadingRow.last().globalIndex = index;
    }

    // qDebug() << "==";
    // for (int k = 0; k < _currentLoadingRow.count(); k++) {
    //     qDebug() << "In row" << _currentLoadingRow[k].globalIndex;
    // }
    // qDebug() << "==";

    calcLayout(_currentLoadingRow, width() - _paddingLeft - _paddingRight, _targetHeight, _spacing, !_listView, 0, layoutMode());
    if (_currentLoadingRow.last().row > 0 || flushMode) {
        // qDebug() << "//// pushing" << _currentLoadingRow.first().globalIndex << "-" << _currentLoadingRow.last().globalIndex << flushMode << _currentLoadingRow.size();
        // qDebug() << "REWRAP";
        QList<int> requestsIndexes;
        for (int i = 0; i < _currentLoadingRow.size(); i++) {
            // qDebug() << "i" << i << _currentLoadingRow[i].globalIndex << _currentLoadingRow[i].row << _currentLoadingRow.last().row;
            if (_currentLoadingRow[i].row != _currentLoadingRow.last().row || flushMode) {
                int updIndex = _currentLoadingRow[i].globalIndex;
                if (_bricks[updIndex].image && _bricks[updIndex].image->fullSize().isValid()) {
                    // qDebug() << "Full size is valid, updating" << updIndex;
                    _bricks[updIndex].originalSize = _bricks[updIndex].image->fullSize();
                    if (_bricks[updIndex].image->isImage()) {
                        _bricks[updIndex].image->setIsShowAsImage(true);
                    }
                    // When pushing single item that fills the whole row we need to add a line break
                    if (!flushMode && !_bricks[updIndex].column && i == _currentLoadingRow.size() - 2) {
                        // qDebug() << "Last in row, forcing line break" << updIndex << "at" << i;
                        _bricks[updIndex].temporaryLineBreakAfter = true;

                        for (int delIndex = 0; delIndex <= i; delIndex++) {
                            _currentLoadingRow.removeFirst();
                        }
                        requestsIndexes.append(updIndex);
                        break;
                    }
                    requestsIndexes.append(updIndex);
                }
            }
            else {
                if (i) {
                    int updIndex = _currentLoadingRow[i - 1].globalIndex;
                    if (updIndex >= 0) {
                        // qDebug() << "Second line break source" << updIndex << ", removing 0 to" << i - 1;
                        _bricks[updIndex].temporaryLineBreakAfter = true;
                    }
                }

                for (int delIndex = 0; delIndex < i; delIndex++) {
                    _currentLoadingRow.removeFirst();
                }
                break;
            }
        }
        if (flushMode) {
            _currentLoadingRow.clear();
        }
        rewrap(animate);

        QList<ImageDecodeRequest> requests;
        // qDebug() << "-------------- 2" << _currentLoadingRow.last().row << flushMode;
        for (int i = 0; i < requestsIndexes.size(); i++) {
            int index = requestsIndexes[i];
            if (!_overscanIndexSet.contains(index)) {
                continue;
            }
            planThumbnailForIndex(
                index, index >= _visibleStart && index <= _visibleEnd,
                requests);
            // qDebug() << "decode2 " << requests.last().info.path << requests.last().targetSize;
        }
        dynamic_cast<ThumbnailsRequestInterface *>(_model)->decodeImages(requests);
    }
}

void MasonryLayout::onThumbnailReadFinished(bool animate) {
    if (_bricks.count() && _currentLoadingRow.size()) {
        pushToCurrentRow(_bricks.count(), animate);
    }
    _skipThumbnailBackfillUntilFlush = false;
}

MasonryLayout::MasonryBrick MasonryLayout::brickForImage(
    ImageFile *imageFile) const {
    QSize imageSize = imageFile ? imageFile->fullSize() : QSize();
    bool lineBreakAfter = false;
    if (imageFile && imageSize.isEmpty()) {
        if (imageFile->isFolder() && _listView) {
            lineBreakAfter = true;
            imageSize = QSize(0, imageFile->folderView() ? 0
                                                        : listRowHeight());
        }
        else {
            imageSize = GridView_Folder.toSize();
        }
    }
    return MasonryBrick{
        .originalSize = imageSize,
        .lineBreakAfter = lineBreakAfter,
        .image = imageFile,
    };
}

void MasonryLayout::prepareForIncrementalModelChange() {
    if (_incrementalModelChangeDepth++ > 0) {
        return;
    }
    preserveCurrentItemPositionForNextModelReset();

    // Older loading-row entries did not retain their identity explicitly.
    // Fill it while their old indexes are still valid so the queue can be
    // remapped rather than canceled after an insert/remove/move.
    for (MasonryBrick &pendingBrick : _currentLoadingRow) {
        if (!pendingBrick.image && pendingBrick.globalIndex >= 0 &&
            pendingBrick.globalIndex < _bricks.size()) {
            pendingBrick.image = _bricks[pendingBrick.globalIndex].image;
        }
    }
}

void MasonryLayout::applyIncrementalModelChange() {
    if (_incrementalModelChangeDepth <= 0) {
        return;
    }
    if (--_incrementalModelChangeDepth > 0) {
        return;
    }
    _scheduledThumbnailIndexes.clear();
    _scheduledThumbnailRequestKeys.clear();
    _lastThumbnailViewportIndexes.clear();
    _catalogMetadataRequested = false;
    const int previousCount = _bricks.size();
    const int previousCurrentIndex = _currentIndex;
    const QString previousCurrentPath =
        _preserveCurrentItemPositionOnNextModelReset
            ? _preservedCurrentItemFullPath
            : QString();

    QList<MasonryBrick> oldBricks = std::move(_bricks);
    QHash<ImageFile *, int> oldIndexes;
    oldIndexes.reserve(oldBricks.size());
    for (int i = 0; i < oldBricks.size(); ++i) {
        if (oldBricks[i].image) {
            oldIndexes.insert(oldBricks[i].image, i);
        }
    }

    QList<bool> retained(oldBricks.size(), false);
    _bricks.clear();
    if (_model) {
        _bricks.reserve(_model->rowCount());
        for (int row = 0; row < _model->rowCount(); ++row) {
            ImageFile *imageFile =
                imageFileFromModelIndex(_model->index(row, 0));
            const auto oldIt = oldIndexes.constFind(imageFile);
            if (imageFile && oldIt != oldIndexes.constEnd()) {
                retained[*oldIt] = true;
                _bricks.append(std::move(oldBricks[*oldIt]));
            }
            else if (imageFile) {
                _bricks.append(brickForImage(imageFile));
            }
        }
    }

    // Recycle delegates only for identities that truly disappeared. Retained
    // bricks carry their existing BrickItem into the new row position.
    for (int i = 0; i < oldBricks.size(); ++i) {
        if (retained[i] || !oldBricks[i].item) {
            continue;
        }
        BrickItem *item = oldBricks[i].item;
        pushBrickItem(item);
        item->setVisible(false);
        item->setProperty("model", QVariant());
        item->setProperty("viewIndex", -1);
        item->setProperty("sourceIndex", -1);
    }

    QHash<ImageFile *, int> newIndexes;
    newIndexes.reserve(_bricks.size());
    for (int i = 0; i < _bricks.size(); ++i) {
        if (_bricks[i].image) {
            newIndexes.insert(_bricks[i].image, i);
        }
    }
    _activeBrickIndexes.clear();
    for (int i = 0; i < _bricks.size(); ++i) {
        if (_bricks[i].item) {
            _activeBrickIndexes.insert(i);
        }
    }
    for (int i = _currentLoadingRow.size() - 1; i >= 0; --i) {
        const auto newIt = newIndexes.constFind(_currentLoadingRow[i].image);
        if (!_currentLoadingRow[i].image || newIt == newIndexes.constEnd()) {
            _currentLoadingRow.removeAt(i);
        }
        else {
            _currentLoadingRow[i].globalIndex = *newIt;
        }
    }
    std::sort(_currentLoadingRow.begin(), _currentLoadingRow.end(),
              [](const MasonryBrick &left, const MasonryBrick &right) {
                  return left.globalIndex < right.globalIndex;
              });

    restorePreservedCurrentItemPosition();
    if (_bricks.isEmpty()) {
        _currentIndex = -1;
        _topItem = 0;
    }
    else if (_currentIndex < 0 || _currentIndex >= _bricks.size()) {
        _currentIndex = qBound(0, _currentIndex, _bricks.size() - 1);
    }

    // The previous numeric range is stale, but updateProperties() now tracks
    // delegates by brick identity and will retain every still-visible item.
    _visibleStart = -1;
    _visibleEnd = -1;
    rewrap(false);
    positionViewport();

    const int previousImageCount = _imageCount;
    _imageCount = 0;
    for (const MasonryBrick &brick : std::as_const(_bricks)) {
        if (brick.image && brick.image->isImage()) {
            ++_imageCount;
        }
    }
    if (_imageCount != previousImageCount) {
        emit imageCountChanged();
    }
    updateCurrentImageIndex();
    if (previousCount != _bricks.size()) {
        emit countChanged();
    }
    if (!_quickSearch->mask().isEmpty()) {
        _quickSearch->updateItemsText();
    }

    const bool currentIdentityPreserved =
        !previousCurrentPath.isEmpty() && _currentIndex >= 0 &&
        _currentIndex < _bricks.size() && _bricks[_currentIndex].image &&
        _bricks[_currentIndex].image->fullPath() == previousCurrentPath;
    if (!currentIdentityPreserved || previousCurrentIndex != _currentIndex) {
        emit currentIndexChanged();
    }
}

void MasonryLayout::onModelAboutToBeReset() {
    _incrementalModelChangeDepth = 0;
    _currentLoadingRow.clear();
    _visibleStart = -1;
    _visibleEnd = -1;
    _topItem = 0;
    _bricks.clear();
    _layoutBands.clear();
    _activeBrickIndexes.clear();
    _scheduledThumbnailIndexes.clear();
    _scheduledThumbnailRequestKeys.clear();
    _lastThumbnailViewportIndexes.clear();
    _catalogMetadataRequested = false;
    const bool hadVisibleIndexes = !_visibleIndexSet.isEmpty();
    const bool hadOverscanIndexes = !_overscanIndexSet.isEmpty();
    _visibleIndexSet.clear();
    _overscanIndexSet.clear();
    if (!_preserveCurrentItemPositionOnNextModelReset) {
        _currentIndex = 0;
    }
    for (auto it = _usedBrickItems.begin(); it != _usedBrickItems.end(); ++it) {
        (*it)->setVisible(false);
        (*it)->setProperty("model", QVariant());
        (*it)->setProperty("viewIndex", -1);
        (*it)->setProperty("sourceIndex", -1);
    }
    _freeBrickItems.unite(_usedBrickItems);
    _usedBrickItems.clear();
    if (hadVisibleIndexes) {
        emit visibleIndexesChanged();
    }
    if (hadOverscanIndexes) {
        emit overscanIndexesChanged();
    }
}

void MasonryLayout::onModelReset() {
    const int previousCurrentIndex = _currentIndex;
    const QString preservedCurrentPath =
        _preserveCurrentItemPositionOnNextModelReset
            ? _preservedCurrentItemFullPath
            : QString();
    if (_model) {
        for (int i = 0; i < _model->rowCount(); i++) {
            ImageFile *imageFile = imageFileFromModelIndex(_model->index(i, 0));
            if (!imageFile) {
                continue;
            }
            QSize imgSize = imageFile->fullSize();
            bool lineBreakAfter = false;
            if (imgSize.isEmpty()) {
                if (imageFile->isFolder() && _listView) {
                    lineBreakAfter = true;
                    imgSize = QSize(0, imageFile->folderView() ? 0 : listRowHeight());
                }
                else {
                    imgSize = GridView_Folder.toSize();
                }
            }
            _bricks.append(MasonryBrick {
                .originalSize = imgSize,
                .lineBreakAfter = lineBreakAfter,
                .image = imageFile,
            });
        }
    }
    restorePreservedCurrentItemPosition();
    if (_bricks.isEmpty()) {
        _currentIndex = -1;
    }
    else if (_currentIndex < 0 || _currentIndex >= _bricks.size()) {
        _currentIndex = qBound(0, _currentIndex, _bricks.size() - 1);
    }

    emit modelChanged();
    rewrap();
    restorePendingThumbnailRequestsAfterModelReset();
    if (_preserveDecodeQueueForCurrentRebuild) {
        _skipThumbnailBackfillUntilFlush = true;
    }
    _preserveDecodeQueueForCurrentRebuild = false;
    positionViewport();

    _imageCount = 0;
    for (int i = 0; i < _bricks.size(); i++) {
        if (_bricks[i].image->isImage()) {
            _imageCount++;
        }
    }
    emit imageCountChanged();

    updateCurrentImageIndex();
    emit countChanged();

    if (!_quickSearch->mask().isEmpty()) {
        _quickSearch->updateItemsText();
    }
    const bool currentIdentityPreserved =
        !preservedCurrentPath.isEmpty() && _currentIndex >= 0 &&
        _currentIndex < _bricks.size() && _bricks[_currentIndex].image &&
        _bricks[_currentIndex].image->fullPath() == preservedCurrentPath;
    if (!currentIdentityPreserved ||
        previousCurrentIndex != _currentIndex) {
        emit currentIndexChanged();
    }
}

void MasonryLayout::restorePreservedCurrentItemPosition() {
    if (!_preserveCurrentItemPositionOnNextModelReset) {
        return;
    }

    _preserveCurrentItemPositionOnNextModelReset = false;
    bool restored = false;
    if (!_preservedCurrentItemFullPath.isEmpty()) {
        for (int i = 0; i < _bricks.size(); i++) {
            if (_bricks[i].image &&
                _bricks[i].image->fullPath() ==
                    _preservedCurrentItemFullPath) {
                _currentIndex = i;
                restored = true;
                break;
            }
        }
    }
    if (!restored && !_bricks.isEmpty()) {
        _currentIndex = qBound(0, _preservedCurrentFallbackIndex,
                               _bricks.size() - 1);
    }

    bool restoredViewportAnchor = false;
    if (!_preservedViewportAnchorFullPath.isEmpty()) {
        for (int i = 0; i < _bricks.size(); ++i) {
            if (_bricks[i].image &&
                _bricks[i].image->fullPath() ==
                    _preservedViewportAnchorFullPath) {
                _topItem = i;
                _topItemOffset = _preservedViewportAnchorOffset;
                restoredViewportAnchor = true;
                break;
            }
        }
    }
    if (!restoredViewportAnchor && !_bricks.isEmpty() &&
        _preservedViewportAnchorFallbackIndex >= 0) {
        _topItem = qBound(0, _preservedViewportAnchorFallbackIndex,
                          _bricks.size() - 1);
        _topItemOffset = _preservedViewportAnchorOffset;
    }

    _preservedCurrentItemFullPath.clear();
    _preservedCurrentFallbackIndex = -1;
    _preservedViewportAnchorFullPath.clear();
    _preservedViewportAnchorFallbackIndex = -1;
    _preservedViewportAnchorOffset = 0;
}

void MasonryLayout::zoom(bool in) {
    if (_presentationMode != Masonry) {
        const qreal step = (_presentationMode == Columns ||
                            _presentationMode == Details)
            ? 2.0 : 8.0;
        const qreal previousDensity = _density;
        setDensity(_density + (in ? step : -step));
        if (!qFuzzyCompare(previousDensity, _density)) {
            reReadAndDecodeThumbnails();
        }
        return;
    }
    const int smallestHeight = 30;
    const int largestHeight = 500;

    QList<MasonryBrick> bricks;
    QSize minSize = QSize(_targetHeight * GridView_Folder.width() / GridView_Folder.height(), _targetHeight);
    for (int i = 0; i <= ((width() - _paddingLeft - _paddingRight) / minSize.width()) * 2; i++) {
        bricks.append(MasonryBrick {
            .originalSize = GridView_Folder.toSize(),
        });
    }
    int columns = -1;
    int newTargetHeight = -1;
    int increment = in ? 1 : -1;

    int targetHeightRangeStart = -1;
    int targetHeightRangeEnd = -1;

    for (int targetHeight = _targetHeight - _paddingBottom; targetHeight >= smallestHeight && targetHeight <= largestHeight; targetHeight += increment) {
        calcLayout(bricks, width() - _paddingLeft - _paddingRight, targetHeight, _spacing, !_listView, _paddingTop, layoutMode());
        for (int i = 0; i < bricks.size(); i++) {
            if (bricks[i].row && i) {
                if (columns == -1) {
                    columns = bricks[i - 1].column;
                }
                else if (bricks[i - 1].column != columns) {
                    columns = bricks[i - 1].column;
                    if (targetHeightRangeStart == -1) {
                        targetHeightRangeStart = targetHeight;
                    }
                    else {
                        targetHeightRangeEnd = targetHeight;
                        newTargetHeight = (targetHeightRangeStart + targetHeightRangeEnd) / 2;
                    }
                }
                break;
            }
        }
        if (newTargetHeight != -1) {
            break;
        }
    }
    if (newTargetHeight != -1 || (_targetHeight != largestHeight && in) || (_targetHeight != smallestHeight && !in)) {
        setTargetHeight((newTargetHeight != -1 ? newTargetHeight : (in ? largestHeight : smallestHeight)) + _paddingBottom);
        reReadAndDecodeThumbnails();
    }
}

void MasonryLayout::updateNeedScroll() {
    if (!_bricks.size()) {
        if (_needScroll) {
            _needScroll = false;
            emit needScrollChanged();
        }
        return;
    }
    if (_presentationMode != Masonry) {
        const bool newNeedScroll = _contentHeight > viewportExtent();
        if (newNeedScroll != _needScroll && height() > 0) {
            _needScroll = newNeedScroll;
            emit needScrollChanged();
        }
        return;
    }
    // Legacy nested folder-preview Masonry layouts never own a scroll surface.
    // Fixed presentation strategies, however, are also used by the reusable
    // external panel and must expose their real viewport overflow.
    if (isEmbedded()) {
        return;
    }

    bool newNeedScroll = _contentHeight > height();
    if (newNeedScroll != _needScroll && height() > 0) {
        QList<MasonryBrick> bricks = _bricks;
        // TODO: Scrollbar height is hardcoded here
        calcLayout(bricks, width() - _paddingLeft - _paddingRight + (newNeedScroll ? 0 : 16), _targetHeight, _spacing,
                   !_listView, _paddingTop, layoutMode());

        const int newContentHeight = static_cast<int>(
            bricks.last().y + bricks.last().normalizedSize.height()
            + _paddingBottom);
        newNeedScroll = newContentHeight > height();
        if (newNeedScroll != _needScroll && newContentHeight > 0) {
            _needScroll = newNeedScroll;
            emit needScrollChanged();
        }
    }
}

void MasonryLayout::pushBrickItem(BrickItem *item) {
    item->stopGeometryAnimation();
    _usedBrickItems.remove(item);
    _freeBrickItems.insert(item);
}

BrickItem *MasonryLayout::popBrickItem() {
    BrickItem *item = nullptr;
    if (_freeBrickItems.size() > 0) {
        item = *_freeBrickItems.begin();
        _freeBrickItems.remove(item);
        item->stopGeometryAnimation();
        // if (isEmbedded()) {
            // qDebug() << "take from stash";
        // }
    }
    else {
        item = createComponent();
        // if (isEmbedded()) {
            // qDebug() << "create component";
        // }
    }
    _usedBrickItems.insert(item);
    return item;
}

QSize MasonryLayout::dp(QSizeF value) {
    return QSize(dp(value.width()), dp(value.height()));
}

qreal MasonryLayout::dp(qreal value) {
    return qRound(value * dpValue());
}

qreal MasonryLayout::dpValue() {
    if (_devicePixelRatioOverride > 0) {
        _dp = _devicePixelRatioOverride;
    }
    else if (QWindow *window_ = window()) {
        _dp = window_->devicePixelRatio();
    }
    return _dp;
}

qreal MasonryLayout::devicePixelRatio() const {
    return _devicePixelRatioOverride > 0
        ? _devicePixelRatioOverride : _dp;
}

void MasonryLayout::setDevicePixelRatio(qreal value) {
    const qreal normalized = value > 0 ? value : 0;
    if (qFuzzyCompare(_devicePixelRatioOverride, normalized)) {
        return;
    }
    _devicePixelRatioOverride = normalized;
    emit devicePixelRatioChanged();
    reReadAndDecodeThumbnails();
}

QFont MasonryLayout::iconLabelFont() const {
    return _iconLabelFont;
}

void MasonryLayout::setIconLabelFont(const QFont &font) {
    if (_iconLabelFont == font) {
        return;
    }
    _iconLabelFont = font;
    emit iconLabelFontChanged();
    if (_presentationMode == Icons) {
        rewrap(false);
    }
}

MasonryLayout::PresentationMode MasonryLayout::presentationMode() const {
    return _presentationMode;
}

void MasonryLayout::setPresentationMode(PresentationMode mode) {
    const int normalizedValue = qBound(
        static_cast<int>(Masonry), static_cast<int>(mode),
        static_cast<int>(Icons));
    mode = static_cast<PresentationMode>(normalizedValue);
    if (_presentationMode == mode) {
        return;
    }
    qreal previousCurrentViewportY = 0;
    bool previousCurrentWasVisible = false;
    if (_currentIndex >= 0 && _currentIndex < _bricks.size()) {
        const QRectF previousGeometry = indexGeometry(_currentIndex);
        previousCurrentWasVisible = previousGeometry.isValid() &&
            !previousGeometry.isEmpty() &&
            previousGeometry.top() < _contentY + height() &&
            previousGeometry.bottom() > _contentY;
        if (previousCurrentWasVisible) {
            previousCurrentViewportY =
                previousGeometry.top() - _contentY;
        }
    }
    _presentationMode = mode;
    _density = _modeDensities[normalizedValue];
    _targetHeight = qRound(_density);
    _currentLoadingRow.clear();
    _scheduledThumbnailIndexes.clear();
    _scheduledThumbnailRequestKeys.clear();
    _lastThumbnailViewportIndexes.clear();
    _windowTopIndex = qBound(
        0, _topItem, maximumWindowTopIndex());
    if (_presentationMode == Columns && _currentIndex >= 0) {
        _windowTopIndex = windowTopIndexForIndex(_currentIndex);
    }
    if (_persistSettings && !isEmbedded()) {
        QSettings settings;
        settings.setValue(QStringLiteral("layout/presentationMode"),
                          normalizedValue);
    }
    emit presentationModeChanged();
    emit densityChanged();
    emit targetExtentChanged();
    emit targetHeightChanged();
    if (_model && (_presentationMode == Details
                   || (_presentationMode != Masonry && !isEmbedded()))) {
        dynamic_cast<ThumbnailsRequestInterface *>(_model)
            ->cancelAllDecodeRunners();
    }
    rewrap(false);

    // A raw pixel contentY has no semantic meaning across presentation modes:
    // Icons and Grid, for example, use different row pitches and column
    // counts. Preserve the current item's viewport-space Y when possible,
    // then clamp only as much as needed to keep the whole new cell visible.
    // This happens synchronously so the first rendered frame is valid.
    if (_currentIndex >= 0 && _currentIndex < _bricks.size()) {
        const QRectF currentGeometry = indexGeometry(_currentIndex);
        if (currentGeometry.isValid() && !currentGeometry.isEmpty()) {
            qreal targetY = _contentY;
            if (_presentationMode != Columns &&
                previousCurrentWasVisible) {
                const qreal desired = currentGeometry.top() -
                    previousCurrentViewportY;
                const qreal minimumForVisibility =
                    currentGeometry.bottom() - height();
                targetY = qBound(
                    qMin(currentGeometry.top(), minimumForVisibility),
                    desired,
                    qMax(currentGeometry.top(), minimumForVisibility));
            }
            else if (_presentationMode != Columns) {
                if (currentGeometry.top() < targetY) {
                    targetY = currentGeometry.top();
                }
                else if (currentGeometry.bottom() > targetY + height()) {
                    targetY = currentGeometry.bottom() - height();
                }
            }
            targetY = qBound<qreal>(
                0, targetY, maximumContentOffset());
            if (!qFuzzyCompare(targetY + 1, _contentY + 1)) {
                setContentY(targetY);
            }
        }
    }
    if (_presentationMode == Masonry) {
        reReadAndDecodeThumbnails();
    }
    else {
        emit layoutReset();
        planViewportThumbnails(_overscanIndexSet, true);
    }
}

int MasonryLayout::columnCount() const {
    return _columnCount;
}

void MasonryLayout::setColumnCount(int columnCount) {
    columnCount = qBound(2, columnCount, 3);
    if (_columnCount == columnCount) {
        return;
    }
    _columnCount = columnCount;
    if (_persistSettings && !isEmbedded()) {
        QSettings settings;
        settings.setValue(QStringLiteral("layout/columnCount"),
                          _columnCount);
    }
    emit columnCountChanged();
    if (_presentationMode == Columns) {
        _windowTopIndex = qBound(
            0, _windowTopIndex, maximumWindowTopIndex());
        rewrap(false);
    }
}

qreal MasonryLayout::density() const {
    return _density;
}

void MasonryLayout::setDensity(qreal density) {
    density = normalizedDensity(_presentationMode, density);
    if (qFuzzyCompare(_density, density)) {
        return;
    }
    _density = density;
    _modeDensities[static_cast<int>(_presentationMode)] = density;
    if (_presentationMode != Masonry) {
        // Density is a user-facing zoom operation. Preserve the old layout's
        // semantic viewport anchor for exactly the rewrap below; unrelated
        // initialization, presentation and model rewraps must not resurrect
        // a stale viewport position.
        _preserveViewportAnchorForNextRewrap = true;
    }
    const int previousTargetHeight = _targetHeight;
    _targetHeight = qRound(density);
    if (_persistSettings && !isEmbedded()) {
        QSettings settings;
        const QString name = presentationModeSettingsName(_presentationMode);
        settings.setValue(QStringLiteral("layout/%1/density").arg(name),
                          _density);
        settings.setValue(
            QStringLiteral("layout/%1/targetExtent").arg(name),
            _targetHeight);
        if (_presentationMode == Masonry) {
            settings.setValue(QStringLiteral("targetHeight"),
                              _targetHeight);
        }
    }
    rewrap();
    emit densityChanged();
    emit targetExtentChanged();
    if (previousTargetHeight != _targetHeight) {
        emit targetHeightChanged();
    }
}

int MasonryLayout::targetExtent() const {
    return qRound(_density);
}

void MasonryLayout::setTargetExtent(int targetExtent) {
    setDensity(targetExtent);
}

int MasonryLayout::targetHeight() const {
    return _targetHeight;
}

void MasonryLayout::setTargetHeight(int newTargetHeight) {
    setDensity(newTargetHeight);
}

int MasonryLayout::windowTopIndex() const {
    return _windowTopIndex;
}

void MasonryLayout::setWindowTopIndex(int index) {
    index = qBound(0, index, maximumWindowTopIndex());
    if (_windowTopIndex == index) {
        return;
    }
    const qreal oldContentY = _contentY;
    _windowTopIndex = index;
    _contentY = contentYForWindowTopIndex(index);
    // An explicit window jump is authoritative.  Keep the semantic anchor in
    // sync before rewrap(), otherwise a stale anchor from the previous
    // horizontal viewport can immediately restore the old column.
    _topItem = _windowTopIndex;
    _topItemOffset = _topItem >= 0 && _topItem < _bricks.size()
        ? _bricks.at(_topItem).x - _contentY : 0;
    emit windowTopIndexChanged();
    rewrap(false);
    if (!qFuzzyCompare(oldContentY, _contentY)) {
        emit contentYChanged();
    }
}

QVariantList MasonryLayout::visibleIndexes() const {
    QList<int> indexes = _visibleIndexSet.values();
    std::sort(indexes.begin(), indexes.end());
    QVariantList result;
    result.reserve(indexes.size());
    for (const int index : std::as_const(indexes)) {
        result.append(index);
    }
    return result;
}

QVariantList MasonryLayout::overscanIndexes() const {
    QList<int> indexes = _overscanIndexSet.values();
    std::sort(indexes.begin(), indexes.end());
    QVariantList result;
    result.reserve(indexes.size());
    for (const int index : std::as_const(indexes)) {
        result.append(index);
    }
    return result;
}

QVariantList MasonryLayout::layoutBands() const {
    QVariantList result;
    result.reserve(_layoutBands.size());
    for (int bandIndex = 0; bandIndex < _layoutBands.size(); ++bandIndex) {
        const LayoutBand &band = _layoutBands.at(bandIndex);
        QVariantList indexes;
        indexes.reserve(band.indexes.size());
        for (const int index : band.indexes) {
            indexes.append(index);
        }
        result.append(QVariantMap{
            {QStringLiteral("bandIndex"), bandIndex},
            {QStringLiteral("row"), band.row},
            {QStringLiteral("top"), band.top},
            {QStringLiteral("bottom"), band.bottom},
            {QStringLiteral("indexes"), indexes},
        });
    }
    return result;
}

quint64 MasonryLayout::layoutRevision() const {
    return _layoutRevision;
}

qreal MasonryLayout::contentY() const {
    return _contentY;
}

void MasonryLayout::setContentY(qreal newContentY) {
    newContentY = qBound<qreal>(0, newContentY, maximumContentOffset());
    if (_presentationMode == Columns) {
        const int nextTop = windowTopIndexForContentY(newContentY);
        if (nextTop != _windowTopIndex) {
            _windowTopIndex = nextTop;
            emit windowTopIndexChanged();
        }
        setContentYInternal(newContentY);
        _topItem = _windowTopIndex;
        _topItemOffset = _topItem >= 0 && _topItem < _bricks.size()
            ? _bricks.at(_topItem).x - _contentY : 0;
        return;
    }
    setContentYInternal(newContentY);
    if (_visibleStart != -1) {
        for (_topItem = _visibleStart; _topItem < _visibleEnd && _topItem < _bricks.size(); _topItem++) {
            if (_bricks[_topItem].y >= _contentY) {
                break;
            }
        }
    }
    if (_topItem != -1 && _topItem < _bricks.count()) {
        _topItemOffset = _bricks[_topItem].y - _contentY;
//        qDebug() << "set contentY" << newContentY << "top item" << _topItem << "offset" << _topItemOffset;
    }
//    _topItem = _visibleStart;
//    setCurrentIndex(_topItem);
}

void MasonryLayout::setContentYInternal(qreal newContentY) {
    static int depth = 0;
    depth++;
    if (qFuzzyCompare(_contentY, newContentY) || depth > 1) {
//        qDebug() << "skip";
        depth--;
        return;
    }
//    qDebug() << "contentY" << _contentY << "->" << newContentY;
    _contentY = newContentY;
    positionViewport();

    updateProperties();
    emit contentYChanged();
    depth--;
}

qreal MasonryLayout::contentHeight() const {
    return _contentHeight;
}

BrickItem::BrickItem(QQuickItem *parent)
    : QQuickItem(parent) {
    _isChangingGeometry = false;
    _row = -1;
    _column = -1;

    int animationDuration = 500;
    _geometryAnimationGroup = new QParallelAnimationGroup(this);
    _xAnimation = new QPropertyAnimation(this, "x");
    _xAnimation->setDuration(animationDuration);
    _xAnimation->setEasingCurve(QEasingCurve::InOutQuad);
    _geometryAnimationGroup->addAnimation(_xAnimation);

    _yAnimation = new QPropertyAnimation(this, "y");
    _yAnimation->setDuration(animationDuration);
    _yAnimation->setEasingCurve(QEasingCurve::InOutQuad);
    _geometryAnimationGroup->addAnimation(_yAnimation);

    _widthAnimation = new QPropertyAnimation(this, "width");
    _widthAnimation->setDuration(animationDuration);
    _widthAnimation->setEasingCurve(QEasingCurve::InOutQuad);
    _geometryAnimationGroup->addAnimation(_widthAnimation);

    _heightAnimation = new QPropertyAnimation(this, "height");
    _heightAnimation->setDuration(animationDuration);
    _heightAnimation->setEasingCurve(QEasingCurve::InOutQuad);
    _geometryAnimationGroup->addAnimation(_heightAnimation);
}

void BrickItem::setRowColumn(int row, int column) {
    if (_row == row && _column == column) {
        return;
    }
    _row = row;
    _column = column;
    emit rowColumnChanged();
}

void BrickItem::setPreviewRect(const QRectF &previewRect) {
    if (_previewRect == previewRect) {
        return;
    }
    _previewRect = previewRect;
    emit previewRectChanged();
}

void BrickItem::setIconLabelText(const QString &text) {
    if (_iconLabelText == text) {
        return;
    }
    _iconLabelText = text;
    emit iconLabelTextChanged();
}

QString BrickItem::iconLabelText() const {
    return _iconLabelText;
}

void BrickItem::animateToRect(const QRectF &rect) {
    // Animate the x property if it has changed
    // if (x() != rect.x()) {
        _xAnimation->setEndValue(rect.x());
    // }

    // Animate the y property if it has changed
    // if (y() != rect.y()) {
        _yAnimation->setEndValue(rect.y());
    // }

    // Animate the width property if it has changed
    // if (width() != rect.width()) {
        _widthAnimation->setEndValue(rect.width());
    // }

    // Animate the height property if it has changed
    // if (height() != rect.height()) {
        _heightAnimation->setEndValue(rect.height());
    // }

    // Start the animation group and set it to delete itself when done
    _geometryAnimationGroup->start();
}

void BrickItem::setGeometry(QRectF rect, bool animate,
                            bool snapToLogicalPixels) {
    if (snapToLogicalPixels) {
        rect = roundRect(rect);
    }
    if (animate) {
        animateToRect(rect);
        return;
    }

    stopGeometryAnimation();

    QRectF oldGeometry(x(), y(), width(), height());
    _isChangingGeometry = true;

    if (x() != rect.x()) {
        setX(rect.x());
    }
    if (y() != rect.y()) {
        setY(rect.y());
    }
    if (width() != rect.width()) {
        setWidth(rect.width());
    }
    if (height() != rect.height()) {
        setHeight(rect.height());
    }

    _isChangingGeometry = false;

    if (oldGeometry != rect) {
#if QT_VERSION < QT_VERSION_CHECK(6, 3, 0)
        geometryChanged(oldGeometry, rect);
#else
        geometryChange(oldGeometry, rect);
#endif
    }
}

QRectF BrickItem::geometry() const {
    return QRectF(x(), y(), width(), height());
}

QRectF BrickItem::previewRect() const {
    return _previewRect;
}

void BrickItem::stopGeometryAnimation() {
    _geometryAnimationGroup->stop();
/*    if (_geometryAnimationGroup->state() == QAbstractAnimation::Running) {
        QRectF rect(_xAnimation->endValue().toReal(),
                    _yAnimation->endValue().toReal(),
                    _widthAnimation->endValue().toReal(),
                    _heightAnimation->endValue().toReal());
        setGeometry(rect, false);
    }*/
}

int BrickItem::row() const {
    return _row;
}

int BrickItem::column() const {
    return _column;
}

#if QT_VERSION < QT_VERSION_CHECK(6, 3, 0)
void BrickItem::geometryChanged(const QRectF &newGeometry, const QRectF &oldGeometry) {
    if (!_isChangingGeometry) {
        QQuickItem::geometryChanged(newGeometry, oldGeometry);
    }
}
#else
void BrickItem::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) {
    if (!_isChangingGeometry) {
        QQuickItem::geometryChange(newGeometry, oldGeometry);
    }
}
#endif

QRectF MasonryLayout::MasonryBrick::geometry() const {
    return QRectF(QPointF(x, y), normalizedSize);
}

QSize MasonryLayout::MasonryBrick::thumbnailSize(int spacing) const {
    if (previewGeometry.isValid() && !previewGeometry.isEmpty()) {
        return roundRect(previewGeometry).toRect().size();
    }
    return roundRect(geometry()).toRect().size() - QSize(spacing, spacing);
}

QAbstractItemModel *MasonryLayout::model() const {
    return _model;
}

void MasonryLayout::setModel(QAbstractItemModel *newModel) {
    if (_model) {
        disconnect(_model, nullptr, this, nullptr);
    }
    _model = newModel;
    if (_model) {
        connect(_model, &QAbstractItemModel::dataChanged,
                this, &MasonryLayout::onDataChanged);
        connect(_model, &QAbstractItemModel::modelAboutToBeReset,
                this, [this]() {
            const bool preserveReset =
                modelRequestsViewStatePreservation(_model);
            _preserveDecodeQueueForCurrentRebuild = preserveReset;
            if (preserveReset) {
                preserveCurrentItemPositionForNextModelReset();
                preservePendingThumbnailRequestsForModelReset();
            }
            else {
                _preservedPendingThumbnailInfo.clear();
                _skipThumbnailBackfillUntilFlush = false;
            }
            onModelAboutToBeReset();
        });
        connect(_model, &QAbstractItemModel::modelReset,
                this, &MasonryLayout::onModelReset);
        connect(_model, &QAbstractItemModel::layoutAboutToBeChanged,
                this, [this]() {
            prepareForIncrementalModelChange();
        });
        connect(_model, &QAbstractItemModel::layoutChanged,
                this, [this]() { applyIncrementalModelChange(); });
        connect(_model, &QAbstractItemModel::rowsAboutToBeInserted,
                this, [this](const QModelIndex &parent) {
            if (parent.isValid()) {
                return;
            }
            if (modelRequestsViewStatePreservation(_model)) {
                // The metadata for a watcher-added row arrives after this
                // signal. Do not rebuild the old loading row from index zero;
                // request that new row directly when its dimensions arrive.
                _skipThumbnailBackfillUntilFlush = true;
            }
            prepareForIncrementalModelChange();
        });
        connect(_model, &QAbstractItemModel::rowsInserted,
                this, [this](const QModelIndex &parent) {
            if (!parent.isValid()) {
                applyIncrementalModelChange();
            }
        });
        connect(_model, &QAbstractItemModel::rowsAboutToBeRemoved,
                this, [this](const QModelIndex &parent) {
            if (parent.isValid()) {
                return;
            }
            prepareForIncrementalModelChange();
        });
        connect(_model, &QAbstractItemModel::rowsRemoved,
                this, [this](const QModelIndex &parent) {
            if (!parent.isValid()) {
                applyIncrementalModelChange();
            }
        });
        connect(_model, &QAbstractItemModel::rowsAboutToBeMoved,
                this, [this](const QModelIndex &sourceParent, int, int,
                             const QModelIndex &destinationParent, int) {
            if (!sourceParent.isValid() && !destinationParent.isValid()) {
                prepareForIncrementalModelChange();
            }
        });
        connect(_model, &QAbstractItemModel::rowsMoved,
                this, [this](const QModelIndex &sourceParent, int, int,
                             const QModelIndex &destinationParent, int) {
            if (!sourceParent.isValid() && !destinationParent.isValid()) {
                applyIncrementalModelChange();
            }
        });
    }

    onModelAboutToBeReset();
    onModelReset();
}

int MasonryLayout::currentIndex() const {
    return _currentIndex;
}

void MasonryLayout::updateCurrentImageIndex() {
    _currentImageIndex = 0;
    for (int i = 0; i < _currentIndex && i < _bricks.size(); i++) {
        if (_bricks[i].image && _bricks[i].image->isImage()) {
            _currentImageIndex++;
        }
    }
    emit currentImageIndexChanged();
}

void MasonryLayout::setCurrentIndex(int newCurrentIndex) {
    const int rowCount = _model ? _model->rowCount() : 0;
    newCurrentIndex = rowCount > 0
        ? qMin(qMax(0, newCurrentIndex), rowCount - 1)
        : -1;
    if (_currentIndex == newCurrentIndex) {
        return;
    }
    _currentIndex = newCurrentIndex;
    emit currentIndexChanged();

    updateCurrentImageIndex();

    if (_quickSearch && !_quickSearch->mask().isEmpty()) {
        _quickSearch->updateItemsText();
    }
}

int MasonryLayout::spacing() const {
    return _spacing;
}

void MasonryLayout::setSpacing(int newSpacing) {
    if (_spacing == newSpacing)
        return;
    _spacing = newSpacing;
    emit spacingChanged();
    rewrap(false);
}

QQuickItem *MasonryLayout::currentItem() const {
    if (_currentIndex >= 0 && _currentIndex < _bricks.size()) {
        return _bricks[_currentIndex].item;
    }
    return nullptr;
}

int MasonryLayout::count() const {
    if (_model) {
        return _model->rowCount();
    }
    return 0;
}

MasonryLayoutQuickSearch *MasonryLayout::quickSearch() const {
    return _quickSearch;
}

bool MasonryLayout::needScroll() const {
    return _needScroll;
}

bool MasonryLayout::listView() const {
    return _listView;
}

void MasonryLayout::setListView(bool isListView) {
    if (_listView == isListView) {
        return;
    }
    _listView = isListView;
    if (_persistSettings) {
        QSettings set;
        set.setValue("listView", isListView);
    }

    for (int i = 0; i < _bricks.size(); i++) {
        if (_bricks[i].image->isFolder()) {
            if (!_listView) {
                _bricks[i].originalSize = GridView_Folder;
                _bricks[i].lineBreakAfter = false;
            }
            else {
                _bricks[i].originalSize = QSize(0, _bricks[i].image->folderView() ? 0 : listRowHeight());
                _bricks[i].lineBreakAfter = true;
            }
        }
    }
    rewrap();

    emit listViewChanged();
}

int MasonryLayout::imageCount() const {
    return _imageCount;
}

int MasonryLayout::currentImageIndex() const {
    return _currentImageIndex;
}

void MasonryLayout::setCurrentImageIndex(int newCurrentImageIndex) {
    if (_currentImageIndex == newCurrentImageIndex) {
        return;
    }
    for (int i = 0, imageIndex = 0; i < _bricks.size(); i++) {
        if (_bricks[i].image->isImage()) {
            if (imageIndex == newCurrentImageIndex) {
                setCurrentIndex(i);
                break;
            }
            imageIndex++;
        }
    }
}

bool MasonryLayout::showTransparentGrid() const {
    return _showTransparentGrid;
}

void MasonryLayout::setShowTransparentGrid(bool newShowTransparentGrid) {
    if (_showTransparentGrid == newShowTransparentGrid) {
        return;
    }

    _showTransparentGrid = newShowTransparentGrid;

    if (_persistSettings) {
        QSettings set;
        set.setValue("showTransparentGrid", _showTransparentGrid);
    }

    emit showTransparentGridChanged();
}

bool MasonryLayout::animateResizing() const {
    return _animateResizing;
}

void MasonryLayout::setAnimateResizing(bool newAnimateResizing) {
    if (_animateResizing == newAnimateResizing) {
        return;
    }

    _animateResizing = newAnimateResizing;
    
    if (_persistSettings) {
        QSettings set;
        set.setValue("animateResizing", _animateResizing);
    }
    
    emit animateResizingChanged();
}

qreal MasonryLayout::paddingLeft() const {
    return _paddingLeft;
}

void MasonryLayout::setPaddingLeft(qreal newPaddingLeft) {
    if (qFuzzyCompare(_paddingLeft, newPaddingLeft))
        return;
    _paddingLeft = newPaddingLeft;
    positionViewport();
    rewrap();
    emit paddingLeftChanged();
}

qreal MasonryLayout::paddingRight() const {
    return _paddingRight;
}

void MasonryLayout::setPaddingRight(qreal newPaddingRight) {
    if (qFuzzyCompare(_paddingRight, newPaddingRight))
        return;
    _paddingRight = newPaddingRight;
    rewrap();
    emit paddingRightChanged();
}

qreal MasonryLayout::paddingTop() const {
    return _paddingTop;
}

void MasonryLayout::setPaddingTop(qreal newPaddingTop) {
    if (qFuzzyCompare(_paddingTop, newPaddingTop))
        return;

    _paddingTop = newPaddingTop;
    _topItemOffset = _paddingTop;
    rewrap();
    emit paddingTopChanged();
}

qreal MasonryLayout::paddingBottom() const {
    return _paddingBottom;
}

void MasonryLayout::setPaddingBottom(qreal newPaddingBottom) {
    if (qFuzzyCompare(_paddingBottom, newPaddingBottom))
        return;
    _paddingBottom = newPaddingBottom;
    rewrap();
    emit paddingBottomChanged();
}

qreal MasonryLayout::width() const {
    return qMax(0.0, qIsInf(QQuickItem::width()) ? 0 : QQuickItem::width());
}

int MasonryLayout::listRowHeight() const {
    return _listRowHeight;
}

QVariantList MasonryLayout::currentImageExif() const {
    if (_currentIndex >= 0 && _currentIndex < _bricks.size()) {
        return _bricks[_currentIndex].image->exifList();
    }
    return QVariantList();
}

QQuickItem *MasonryLayout::viewport() const {
    return _viewport;
}
