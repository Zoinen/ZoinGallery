#include "MasonryLayout.h"
#include "FileListModel.h"
#include "MasonryLayoutQuickSearch.h"
#include "SvgCursor.h"

#include <QAbstractAnimation>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>
#include <QPropertyAnimation>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlProperty>
#include <QQuickWindow>
#include <QDir>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QSettings>
#include <QMap>
#include <QGuiApplication>
#include <QFontMetricsF>
#include <QTimer>

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

bool isScalableVectorImage(const ImageInfo &info) {
    const QString suffix = QFileInfo(info.path).suffix();
    return suffix.compare(QStringLiteral("svg"), Qt::CaseInsensitive) == 0
        || suffix.compare(QStringLiteral("svgz"), Qt::CaseInsensitive) == 0;
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
        ImageFile *image = const_cast<MasonryLayout *>(this)
                               ->materializeImageForIndex(index);
        return image ? image->imageIdUrl() : QString();
    }
    return QString();
}

QString MasonryLayout::indexText(int index) const {
    if (index >= 0 && index < _bricks.size()) {
        if (!_bricks[index].modelText.isEmpty()) {
            return _bricks[index].modelText;
        }
        ImageFile *image = const_cast<MasonryLayout *>(this)
                               ->materializeImageForIndex(index);
        return image ? image->fileName() : QString();
    }
    return QString();
}

QString MasonryLayout::indexFullPath(int index) const {
    if (index >= 0 && index < _bricks.size()) {
        return QDir::toNativeSeparators(brickPath(index));
    }
    return QString();
}

QSize MasonryLayout::indexOriginalSize(int index) const {
    if (index >= 0 && index < _bricks.size()) {
        if (_bricks[index].modelKnownSize.isValid()) {
            return _bricks[index].modelKnownSize;
        }
        return _bricks[index].originalSize.toSize();
    }
    return QSize();
}

QVariantMap MasonryLayout::indexExif(int index) const {
    if (index >= 0 && index < _bricks.size()) {
        ImageFile *image = const_cast<MasonryLayout *>(this)
                               ->materializeImageForIndex(index);
        return image ? image->info().exif : QVariantMap();
    }
    return QVariantMap();
}

int MasonryLayout::nextImageIndex(bool forward, bool moveToEnd) {
    int nextIndex = _currentIndex;
    for (int i = _currentIndex + (forward ? 1 : -1); i >= 0 && i < _bricks.size(); i += (forward ? 1 : -1)) {
        if (brickIsImage(i)) {
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
    if (_lightweightRewrapPending) {
        // Metadata relayout is frame-gated during background completion, but
        // keyboard navigation must never target stale row geometry. Treat an
        // interactive navigation query as a synchronous consistency barrier.
        const_cast<MasonryLayout *>(this)->flushLightweightRewrap();
    }
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

    if (_currentIndex >= 0 && _currentIndex < _bricks.size()) {
        _preservedCurrentItemFullPath = brickPath(_currentIndex);
        _preservedCurrentFallbackIndex = _currentIndex;
    }

    const int anchorIndex = _topItem;
    if (anchorIndex >= 0 && anchorIndex < _bricks.size()) {
        const MasonryBrick &anchorBrick = _bricks[anchorIndex];
        _preservedViewportAnchorFullPath = brickPath(anchorIndex);
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

qreal MasonryLayout::columnStride() const {
    const int columns = effectiveColumnCount();
    const qreal canvasWidth = qMax<qreal>(
        0, width() - _paddingLeft - _paddingRight);
    if (columns <= 0 || canvasWidth <= 0) {
        return 0;
    }

    // Equal mathematical thirds do not generally land on device pixels.
    // Rounding each QRectF independently then produces alternating widths
    // and one-pixel phase changes while the viewport moves. Choose one
    // integral physical-pixel pitch for every column instead. Any remainder
    // (at most columnCount-1 pixels) stays at the far right of the canvas, so
    // all cells and all horizontal scroll steps remain strictly identical.
    const qreal dpr = qMax<qreal>(0.01, devicePixelRatio());
    const qreal physicalCanvas = std::floor(canvasWidth * dpr + 0.000001);
    const qreal physicalStride = std::floor(physicalCanvas / columns);
    if (physicalStride < 1) {
        return canvasWidth / columns;
    }
    return physicalStride / dpr;
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
        const qreal cellWidth = columnStride();
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
                ? brick.image->text() : brick.modelText;
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
    if (_presentationMode == Details) {
        // Details has exactly one brick per monotonically increasing row.
        // Avoid building a QMap with hundreds of one-element lists and then
        // sorting values that are already in vertical order.
        _layoutBands.reserve(_bricks.size());
        for (int index = 0; index < _bricks.size(); ++index) {
            const MasonryBrick &brick = _bricks.at(index);
            const QRectF geometry = brick.geometry();
            if (!geometry.isValid() || geometry.isEmpty()) {
                continue;
            }
            LayoutBand band;
            band.row = brick.row;
            band.top = geometry.top();
            band.bottom = geometry.bottom();
            band.indexes.append(index);
            _layoutBands.append(std::move(band));
        }
        ++_layoutRevision;
        emit layoutRevisionChanged();
        emit layoutBandsChanged();
        return;
    }
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
    const bool traceReset = qEnvironmentVariableIsSet(
        "F4_NAV_BENCHMARK_TRACE");
    QElapsedTimer rewrapTimer;
    if (traceReset) {
        rewrapTimer.start();
    }
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
            const qreal stride = columnStride();
            const qreal canvasWidth = qMax<qreal>(
                0, width() - _paddingLeft - _paddingRight);
            const qreal trailingCanvasRemainder = qMax<qreal>(
                0, canvasWidth - columns * stride);
            // The integral physical-pixel stride can leave one or two pixels
            // after the visible columns. Keep that remainder at the trailing
            // edge of the horizontal content too. Otherwise
            // contentHeight-width shortens the terminal offset by precisely
            // the remainder and shifts the whole last screen to the right.
            setContentHeight(_paddingLeft + totalColumns * stride
                             + trailingCanvasRemainder + _paddingRight);
        }
        else {
            virtualRows = columns > 0
                ? (_bricks.size() + columns - 1) / columns : 0;
        }
        if (_presentationMode == Details) {
            const qreal usableViewportHeight = qMax<qreal>(
                0, height() - _paddingTop - _paddingBottom);
            const int completeVisibleRows = int(std::floor(
                usableViewportHeight / extent + 0.000000001));
            const qreal trailingViewportRemainder = completeVisibleRows > 0
                ? qMax<qreal>(
                    0, usableViewportHeight - completeVisibleRows * extent)
                : 0;

            // Keep the terminal scroll position on the same row lattice as
            // every keyboard reveal. Without the viewport's fractional row
            // remainder at the trailing edge, contentHeight-height clamps
            // the final screen between two rows and shifts its top row down.
            setContentHeight(_paddingTop + virtualRows * extent
                             + trailingViewportRemainder + _paddingBottom);
        }
        else if (_presentationMode != Icons
                 && _presentationMode != Columns) {
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
        const qint64 extentCompletedNs = traceReset
            ? rewrapTimer.nsecsElapsed() : 0;
        calcFixedLayout();
        const qint64 layoutCompletedNs = traceReset
            ? rewrapTimer.nsecsElapsed() : 0;
        if (_presentationMode == Icons) {
            const qreal iconContentHeight = _bricks.isEmpty()
                ? 0
                : _bricks.constLast().geometry().bottom() + _paddingBottom;
            setContentHeight(iconContentHeight);
            _contentY = qBound<qreal>(
                0, _contentY, qMax<qreal>(0, _contentHeight - height()));
        }
        rebuildLayoutBands();
        const qint64 bandsCompletedNs = traceReset
            ? rewrapTimer.nsecsElapsed() : 0;
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
        const qint64 viewportCompletedNs = traceReset
            ? rewrapTimer.nsecsElapsed() : 0;
        updateProperties(animate);
        const qint64 propertiesCompletedNs = traceReset
            ? rewrapTimer.nsecsElapsed() : 0;
        // A strategy switch can preserve the same numeric content height
        // while changing from an embedded Masonry surface (which owns no
        // scrollbar) to a fixed viewport. Re-evaluate overflow even when
        // setContentHeight() therefore emitted no change.
        updateNeedScroll();
        if (!qFuzzyCompare(oldContentY, _contentY)) {
            emit contentYChanged();
        }
        if (traceReset) {
            const qint64 completedNs = rewrapTimer.nsecsElapsed();
            qInfo().nospace()
                << "F4_NAV_BENCHMARK_TRACE masonry.rewrap rows="
                << _bricks.size() << " mode="
                << static_cast<int>(_presentationMode)
                << " extentNs=" << extentCompletedNs
                << " layoutNs="
                << (layoutCompletedNs - extentCompletedNs)
                << " bandsNs="
                << (bandsCompletedNs - layoutCompletedNs)
                << " viewportNs="
                << (viewportCompletedNs - bandsCompletedNs)
                << " propertiesNs="
                << (propertiesCompletedNs - viewportCompletedNs)
                << " tailNs=" << (completedNs - propertiesCompletedNs)
                << " totalNs=" << completedNs;
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
    const bool traceReset = qEnvironmentVariableIsSet(
        "F4_NAV_BENCHMARK_TRACE");
    QElapsedTimer propertiesTimer;
    if (traceReset) {
        propertiesTimer.start();
    }
    updateViewportIndexSets();
    const qint64 viewportSetsCompletedNs = traceReset
        ? propertiesTimer.nsecsElapsed() : 0;

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
    const qint64 indexesCompletedNs = traceReset
        ? propertiesTimer.nsecsElapsed() : 0;
    if (_delegateRefreshPending) {
        // A host-controlled path replacement keeps this item fully transparent
        // until catalog and cursor placement have both completed. Preserve the
        // old visual slots for this event-stack, then bind the final current
        // model/window in updatePolish(), immediately before scene-graph sync.
        // This stores no prior catalog: only the already-live BrickItems are
        // retained across the one synchronous reset transaction.
        if (traceReset) {
            const qint64 completedNs = propertiesTimer.nsecsElapsed();
            qInfo().nospace()
                << "F4_NAV_BENCHMARK_TRACE masonry.properties rows="
                << _bricks.size() << " delegates=" << delegateIndexes.size()
                << " deferred=1 viewportSetsNs="
                << viewportSetsCompletedNs << " indexesNs="
                << (indexesCompletedNs - viewportSetsCompletedNs)
                << " totalNs=" << completedNs;
        }
        return;
    }
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
    const qint64 retiredCompletedNs = traceReset
        ? propertiesTimer.nsecsElapsed() : 0;

    int reboundCount = 0;
    int createdOrPoppedCount = 0;
    int retainedSlotCount = 0;
    int snapshotCount = 0;
    qint64 delegateAcquireNs = 0;
    qint64 visualSnapshotApplyNs = 0;
    qint64 delegateBindingNs = 0;
    qint64 delegateLayoutMetaNs = 0;
    qint64 delegateGeometryNs = 0;
    qint64 delegateVisibilityNs = 0;
    for (const int i : delegateIndexes) {
        if (i >= 0 && i < _bricks.size() &&
            _bricks[i].normalizedSize.isValid() &&
            !_bricks[i].normalizedSize.isEmpty()) {
            qint64 phaseStartedNs = traceReset
                ? propertiesTimer.nsecsElapsed() : 0;
            // ImageFile is the QML delegate/viewer façade. Keep construction
            // bounded to active visual rows; fixed Details geometry above was
            // computed entirely from lightweight catalog roles.
            // Snapshot-backed rows that become active during the post-swap
            // queue (metadata rewrap, scroll, resize) must join that same
            // bounded queue. Eager materialization here would turn one such
            // updateProperties() pass back into 49 synchronous QObject/QML
            // rebinds.
            const bool deferMissingFacade = _visualSnapshotRole >= 0
                && _delegateMaterializationPending
                && !_bricks[i].image;
            ImageFile *const image = (_visualSnapshotRefresh
                                      || deferMissingFacade)
                ? _bricks[i].image : materializeImageForIndex(i);
            bool itemPopped = false;
            if (!_bricks[i].item) {
                BrickItem *const retained = _resetSlotItems.take(i);
                if (retained) {
                    _bricks[i].item = retained;
                    _freeBrickItems.remove(retained);
                    _usedBrickItems.insert(retained);
                    ++retainedSlotCount;
                }
                else {
                    itemPopped = true;
                    ++createdOrPoppedCount;
                    if (isEmbedded()) {
                        // qDebug() << "POP ITEM" << i << dynamic_cast<ThumbnailsRequestInterface *>(_model)->rootItem();
                    }
                    _bricks[i].item = popBrickItem();
                }
            }
            BrickItem *const item = _bricks[i].item;
            if (traceReset) {
                const qint64 now = propertiesTimer.nsecsElapsed();
                delegateAcquireNs += now - phaseStartedNs;
                phaseStartedNs = now;
            }
            if (_visualSnapshotRole >= 0) {
                item->setVisualRow(visualSnapshotForIndex(i));
                ++snapshotCount;
            }
            if (traceReset) {
                const qint64 now = propertiesTimer.nsecsElapsed();
                visualSnapshotApplyNs += now - phaseStartedNs;
                phaseStartedNs = now;
            }
            if (_visualSnapshotRole >= 0) {
                if (_visualSnapshotRefresh) {
                    item->setVisualFacadeReady(false);
                    // A retained slot can still carry the QObject from the
                    // previous directory. Static bindings are snapshot-owned
                    // now, so clearing this stale dynamic facade invalidates
                    // only name-search/thumbnail bindings and guarantees that
                    // first-frame actions can never observe the old row.
                    if (item->property("model").value<ImageFile *>()) {
                        item->setProperty("model", QVariant());
                    }
                }
            }
            if (!_visualSnapshotRefresh
                && item->property("model").value<ImageFile *>() != image) {
                item->setProperty(
                    "model", QVariant::fromValue(image));
                ++reboundCount;
            }
            if (!_visualSnapshotRefresh) {
                item->setVisualFacadeReady(image != nullptr);
            }
            if (item->property("viewIndex").toInt() != i) {
                item->setProperty("viewIndex", i);
            }
            const int sourceIndex = image
                ? image->index() : _bricks[i].modelSourceIndex;
            if (item->property("sourceIndex").toInt() != sourceIndex) {
                item->setProperty("sourceIndex", sourceIndex);
            }
            if (traceReset) {
                const qint64 now = propertiesTimer.nsecsElapsed();
                delegateBindingNs += now - phaseStartedNs;
                phaseStartedNs = now;
            }
            _bricks[i].item->setRowColumn(_bricks[i].row, _bricks[i].column);
            _bricks[i].item->setIconLabelText(
                _bricks[i].iconLabelText);
            _bricks[i].item->setPreviewRect(
                _bricks[i].previewGeometry.translated(
                    -_bricks[i].x, -_bricks[i].y));
            if (traceReset) {
                const qint64 now = propertiesTimer.nsecsElapsed();
                delegateLayoutMetaNs += now - phaseStartedNs;
                phaseStartedNs = now;
            }

            // Details derives a fractional row extent from host font metrics.
            // Columns derives one integral *physical*-pixel stride, which can
            // likewise be fractional in logical coordinates at a non-integer
            // DPR. Snapping either QRectF independently would reintroduce the
            // cumulative phase drift these layouts are designed to avoid.
            const bool preserveFractionalGeometry =
                _presentationMode == Details || _presentationMode == Columns;
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
                                _bricks[i].image->info().sourceIdentity();
                        }
                    }
                    _bricks[i].item->setGeometry(
                        _bricks[i].geometry(), animateGeometry,
                        !preserveFractionalGeometry);
                }
            }
            if (traceReset) {
                const qint64 now = propertiesTimer.nsecsElapsed();
                delegateGeometryNs += now - phaseStartedNs;
                phaseStartedNs = now;
            }

            _activeBrickIndexes.insert(i);
            if (deferMissingFacade) {
                enqueueDeferredDelegateMaterialization(i);
            }

            if (itemsToHide.contains(_bricks[i].item)) {
                itemsToHide.remove(_bricks[i].item);
            }
            else {
                if (!_bricks[i].item->isVisible()) {
                    _bricks[i].item->setVisible(true);
                }
            }
            if (traceReset) {
                delegateVisibilityNs += propertiesTimer.nsecsElapsed()
                    - phaseStartedNs;
            }
        }
    }
    const qint64 delegatesCompletedNs = traceReset
        ? propertiesTimer.nsecsElapsed() : 0;

    // Slots outside the new visible window are no longer part of the active
    // catalog. Release them only after every reusable row has been claimed.
    // This is still inside the synchronous reset; no previous catalog state
    // survives the call.
    releaseResetSlotItems();

    for (BrickItem *item : itemsToHide) {
        item->setVisible(false);
        item->setVisualRow({});
        item->setVisualFacadeReady(false);
        item->setProperty("model", QVariant());
        item->setProperty("viewIndex", -1);
        item->setProperty("sourceIndex", -1);
    }
    if (traceReset) {
        const qint64 completedNs = propertiesTimer.nsecsElapsed();
        qInfo().nospace()
            << "F4_NAV_BENCHMARK_TRACE masonry.properties rows="
            << _bricks.size() << " delegates=" << delegateIndexes.size()
            << " popped=" << createdOrPoppedCount
            << " retainedSlots=" << retainedSlotCount
            << " rebound=" << reboundCount
            << " snapshots=" << snapshotCount
            << " snapshotOnly=" << (_visualSnapshotRefresh ? 1 : 0)
            << " viewportSetsNs=" << viewportSetsCompletedNs
            << " indexesNs="
            << (indexesCompletedNs - viewportSetsCompletedNs)
            << " retiredNs="
            << (retiredCompletedNs - indexesCompletedNs)
            << " delegatesNs="
            << (delegatesCompletedNs - retiredCompletedNs)
            << " acquireNs=" << delegateAcquireNs
            << " visualNs=" << visualSnapshotApplyNs
            << " bindingNs=" << delegateBindingNs
            << " layoutMetaNs=" << delegateLayoutMetaNs
            << " geometryNs=" << delegateGeometryNs
            << " visibilityNs=" << delegateVisibilityNs
            << " leftoversNs=" << (completedNs - delegatesCompletedNs)
            << " totalNs=" << completedNs;
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
    if (!image && canUseLightweightRows()) {
        // The layout intentionally keeps non-delegate rows as POD-only
        // bricks. Once metadata makes an overscan image decodable, create its
        // ImageFile facade here rather than requiring a viewport-set change
        // (or materializing the whole catalog during model reset).
        if (_delegateRefreshPending || _visualSnapshotRefresh
            || _delegateMaterializationPending
            || !brick.modelIsImage || !brick.modelKnownSize.isValid()
            || brick.modelKnownSize.isEmpty()) {
            return;
        }
        image = materializeImageForIndex(index);
    }
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
        brick.lastPlannedSourcePath == info.sourceIdentity() &&
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
        brick.lastPlannedSourcePath = info.sourceIdentity();
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
    // Raster thumbnails never need synthesized pixels: if the native image
    // is smaller than the preview, publish native dimensions and let the
    // scene graph perform the unavoidable display upscale. SVG metadata is
    // different: its width/height is only the authoring viewport (commonly
    // 16 or 24 px), not a native pixel limit. Clamping SVG here used to
    // publish a tiny raster which was then enlarged by QML in every panel
    // mode. Keep the requested physical preview size for scalable vectors.
    if (!isScalableVectorImage(brick.image->info())) {
        scale = qMin<qreal>(scale, 1.0);
    }
    scale = qMax<qreal>(0, scale);
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
            keys.insert(request.info.sourceIdentity() + QChar(0x1f) +
                        request.info.sourceVersionToken +
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
        const bool semanticRolesChanged = roles.isEmpty()
            || roles.contains(FileListModel::IsImageRole)
            || roles.contains(FileListModel::FolderRole);
        const bool pathRoleChanged = _localPathRole >= 0
            && (roles.isEmpty() || roles.contains(_localPathRole));
        const bool textRoleChanged = _entryNameRole >= 0
            && (roles.isEmpty() || roles.contains(_entryNameRole));
        const bool sizeRoleChanged = roles.isEmpty()
            || roles.contains(FileListModel::ImageFullSizeRole)
            || (_knownImageSizeRole >= 0
                && roles.contains(_knownImageSizeRole));
        if (semanticRolesChanged || pathRoleChanged || textRoleChanged
            || sizeRoleChanged) {
            for (int row = index; row <= indexTo; ++row) {
                const QModelIndex modelIndex = _model->index(row, 0);
                if (semanticRolesChanged) {
                    _bricks[row].modelIsImage = modelIndex.data(
                        FileListModel::IsImageRole).toBool();
                    _bricks[row].modelIsFolder = modelIndex.data(
                        FileListModel::FolderRole).toBool();
                }
                if (pathRoleChanged) {
                    _bricks[row].modelPath = modelIndex.data(
                        _localPathRole).toString();
                }
                if (textRoleChanged) {
                    _bricks[row].modelText = modelIndex.data(
                        _entryNameRole).toString();
                }
                if (sizeRoleChanged) {
                    _bricks[row].modelKnownSize = modelIndex.data(
                        FileListModel::ImageFullSizeRole).toSize();
                }
                if (canUseLightweightRows()
                    && (semanticRolesChanged || sizeRoleChanged)) {
                    QSize layoutSize = _bricks[row].modelKnownSize;
                    bool lineBreakAfter = false;
                    if (layoutSize.isEmpty()) {
                        if (_bricks[row].modelIsFolder && _listView) {
                            lineBreakAfter = true;
                            layoutSize = QSize(0, listRowHeight());
                        }
                        else {
                            layoutSize = GridView_Folder.toSize();
                        }
                    }
                    _bricks[row].originalSize = layoutSize;
                    _bricks[row].lineBreakAfter = lineBreakAfter;
                }
            }
            if (semanticRolesChanged) {
                const int previousImageCount = _imageCount;
                _imageCount = 0;
                for (int row = 0; row < _bricks.size(); ++row) {
                    if (brickIsImage(row)) {
                        ++_imageCount;
                    }
                }
                if (_imageCount != previousImageCount) {
                    emit imageCountChanged();
                }
                updateCurrentImageIndex();
            }
        }
        const bool imageUrlOnly = roles.size() == 1
            && roles.contains(FileListModel::ImageIdUrlRole);
        if (_visualSnapshotRole >= 0) {
            for (int row = index; row <= indexTo; ++row) {
                if (_activeBrickIndexes.contains(row)) {
                    BrickItem *item = _bricks[row].item;
                    // A current, generation-checked facade exposes its
                    // notifying thumbnail URL directly to QML. Avoid making
                    // a URL-only cache event invalidate every static field in
                    // this otherwise immutable first-frame snapshot.
                    if (!imageUrlOnly || !item
                        || !item->visualFacadeReady()) {
                        updateVisualSnapshotForIndex(row);
                    }
                }
            }
        }
        if (_presentationMode == Masonry && canUseLightweightRows()) {
            if (semanticRolesChanged || sizeRoleChanged) {
                // Header results can arrive for offscreen rows without an
                // ImageFile facade. Fold a synchronous decoder batch into one
                // full-catalog rewrap; otherwise per-image header completions
                // produce O(image-count * catalog-size) GUI-thread work.
                if (roles.isEmpty() || roles.contains(
                        FileListModel::CachedMetadataBatchRole)) {
                    flushLightweightRewrap();
                }
                else {
                    scheduleLightweightRewrap();
                }
            }
            if (roles.isEmpty()
                || roles.contains(FileListModel::ImageFullSizeRole)
                || roles.contains(FileListModel::ImageIdUrlRole)) {
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
            const bool cachedMetadataBatch =
                roles.contains(FileListModel::CachedMetadataBatchRole);
            bool animateLayoutChange = !cachedMetadataBatch;
            if (animateLayoutChange) {
                animateLayoutChange = false;
                for (int i = index; i <= indexTo; i++) {
                    if (_bricks[i].image &&
                        _bricks[i].image->fullSize().isValid() &&
                        !_bricks[i].image->info().isCached) {
                        animateLayoutChange = true;
                        break;
                    }
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
                rewrap(!_bricks[index].image ||
                       !_bricks[index].image->info().isCached);
            }
        }
        if (roles.contains(FileListModel::TimeToFlushRole)) {
            const bool animateLayoutChange =
                !roles.contains(FileListModel::CachedMetadataBatchRole) &&
                (!_bricks[index].image ||
                 !_bricks[index].image->info().isCached);
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

void MasonryLayout::updateModelRoleCache() {
    _entryIdRole = -1;
    _sourceIndexRole = -1;
    _localPathRole = -1;
    _entryNameRole = -1;
    _knownImageSizeRole = -1;
    _visualSnapshotRole = -1;
    if (!_model) {
        return;
    }
    const QHash<int, QByteArray> roles = _model->roleNames();
    for (auto role = roles.cbegin(); role != roles.cend(); ++role) {
        if (role.value() == QByteArrayLiteral("entryId")) {
            _entryIdRole = role.key();
        }
        else if (role.value() == QByteArrayLiteral("sourceIndex")) {
            _sourceIndexRole = role.key();
        }
        else if (role.value() == QByteArrayLiteral("localPath")) {
            _localPathRole = role.key();
        }
        else if (role.value() == QByteArrayLiteral("entryName")) {
            _entryNameRole = role.key();
        }
        else if (role.value() == QByteArrayLiteral("knownImageSize")) {
            _knownImageSizeRole = role.key();
        }
        else if (role.value() == QByteArrayLiteral("visualSnapshot")) {
            _visualSnapshotRole = role.key();
        }
    }
}

QVariantMap MasonryLayout::visualSnapshotForIndex(int index) const {
    if (!_model || _visualSnapshotRole < 0 || index < 0
        || index >= _model->rowCount()) {
        return {};
    }
    return _model->index(index, 0).data(_visualSnapshotRole).toMap();
}

void MasonryLayout::updateVisualSnapshotForIndex(int index) {
    if (_visualSnapshotRole < 0 || index < 0 || index >= _bricks.size()) {
        return;
    }
    BrickItem *item = _bricks[index].item;
    if (!item) {
        return;
    }
    item->setVisualRow(visualSnapshotForIndex(index));
}

bool MasonryLayout::canUseLightweightRows() const {
    // External catalogs carry enough POD role data to build every strategy's
    // first geometry. Unknown image dimensions deliberately use the same 1:1
    // placeholder as a freshly-created ImageFile; metadata later updates the
    // cheap size role and rewraps the affected rows.
    return _model
        && _entryIdRole >= 0 && _sourceIndexRole >= 0
        && _localPathRole >= 0 && _entryNameRole >= 0
        && _knownImageSizeRole >= 0;
}

MasonryLayout::MasonryBrick MasonryLayout::lightweightBrickForModelRow(
    int row) const {
    MasonryBrick brick;
    populateBrickModelState(brick, row);
    QSize imageSize = brick.modelKnownSize;
    if (imageSize.isEmpty()) {
        if (brick.modelIsFolder && _listView) {
            brick.lineBreakAfter = true;
            imageSize = QSize(0, listRowHeight());
        }
        else {
            imageSize = GridView_Folder.toSize();
        }
    }
    brick.originalSize = imageSize;
    return brick;
}

void MasonryLayout::populateBrickModelState(
    MasonryBrick &brick, int row) const {
    if (!_model || row < 0 || row >= _model->rowCount()) {
        return;
    }
    const QModelIndex modelIndex = _model->index(row, 0);
    brick.modelIsImage = modelIndex.data(
        FileListModel::IsImageRole).toBool();
    brick.modelIsFolder = modelIndex.data(
        FileListModel::FolderRole).toBool();
    if (_entryIdRole >= 0) {
        brick.modelIdentity = modelIndex.data(_entryIdRole).toString();
    }
    if (_sourceIndexRole >= 0) {
        brick.modelSourceIndex = modelIndex.data(_sourceIndexRole).toInt();
    }
    if (_localPathRole >= 0) {
        brick.modelPath = modelIndex.data(_localPathRole).toString();
    }
    if (_entryNameRole >= 0) {
        brick.modelText = modelIndex.data(_entryNameRole).toString();
    }
    if (_knownImageSizeRole >= 0) {
        brick.modelKnownSize = modelIndex.data(
            _knownImageSizeRole).toSize();
    }
    if (brick.image) {
        brick.modelIsImage = brick.image->isImage();
        brick.modelIsFolder = brick.image->isFolder();
        brick.modelSourceIndex = brick.image->index();
        if (brick.modelPath.isEmpty()) {
            brick.modelPath = brick.image->fullPath();
        }
        if (brick.modelIdentity.isEmpty()) {
            brick.modelIdentity = brick.modelPath;
        }
        if (brick.modelText.isEmpty()) {
            brick.modelText = brick.image->text();
        }
        if (brick.image->fullSize().isValid()) {
            brick.modelKnownSize = brick.image->fullSize();
        }
    }
}

ImageFile *MasonryLayout::materializeImageForIndex(int index) {
    if (!_model || index < 0 || index >= _bricks.size()
        || index >= _model->rowCount()) {
        return nullptr;
    }
    MasonryBrick &brick = _bricks[index];
    if (!brick.image) {
        brick.image = imageFileFromModelIndex(_model->index(index, 0));
        if (brick.image) {
            brick.modelIsImage = brick.image->isImage();
            brick.modelIsFolder = brick.image->isFolder();
            brick.modelSourceIndex = brick.image->index();
            if (brick.modelPath.isEmpty()) {
                brick.modelPath = brick.image->fullPath();
            }
            if (brick.modelIdentity.isEmpty()) {
                brick.modelIdentity = brick.modelPath;
            }
            if (brick.modelText.isEmpty()) {
                brick.modelText = brick.image->text();
            }
            if (brick.image->fullSize().isValid()) {
                brick.modelKnownSize = brick.image->fullSize();
            }
            if (_quickSearch) {
                _quickSearch->updateItemText(index);
            }
        }
    }
    return brick.image;
}

bool MasonryLayout::brickIsImage(int index) const {
    return index >= 0 && index < _bricks.size()
        && (_bricks[index].image
                ? _bricks[index].image->isImage()
                : _bricks[index].modelIsImage);
}

bool MasonryLayout::brickIsFolder(int index) const {
    return index >= 0 && index < _bricks.size()
        && (_bricks[index].image
                ? _bricks[index].image->isFolder()
                : _bricks[index].modelIsFolder);
}

QString MasonryLayout::brickPath(int index) const {
    if (index < 0 || index >= _bricks.size()) {
        return {};
    }
    return _bricks[index].image
        ? _bricks[index].image->fullPath()
        : _bricks[index].modelPath;
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
    QHash<QString, int> oldIdentityIndexes;
    oldIndexes.reserve(oldBricks.size());
    oldIdentityIndexes.reserve(oldBricks.size());
    for (int i = 0; i < oldBricks.size(); ++i) {
        if (oldBricks[i].image) {
            oldIndexes.insert(oldBricks[i].image, i);
        }
        if (!oldBricks[i].modelIdentity.isEmpty()) {
            oldIdentityIndexes.insert(oldBricks[i].modelIdentity, i);
        }
    }

    QList<bool> retained(oldBricks.size(), false);
    _bricks.clear();
    if (_model) {
        _bricks.reserve(_model->rowCount());
        const bool lightweightRows = canUseLightweightRows();
        for (int row = 0; row < _model->rowCount(); ++row) {
            if (lightweightRows) {
                const QString identity = _model->index(row, 0)
                    .data(_entryIdRole).toString();
                const auto oldIt = oldIdentityIndexes.constFind(identity);
                MasonryBrick brick;
                if (!identity.isEmpty()
                    && oldIt != oldIdentityIndexes.constEnd()) {
                    retained[*oldIt] = true;
                    brick = std::move(oldBricks[*oldIt]);
                    const MasonryBrick modelState =
                        lightweightBrickForModelRow(row);
                    brick.originalSize = modelState.originalSize;
                    brick.lineBreakAfter = modelState.lineBreakAfter;
                    brick.modelIdentity = modelState.modelIdentity;
                    brick.modelPath = modelState.modelPath;
                    brick.modelText = modelState.modelText;
                    brick.modelKnownSize = modelState.modelKnownSize;
                    brick.modelSourceIndex = modelState.modelSourceIndex;
                    brick.modelIsImage = modelState.modelIsImage;
                    brick.modelIsFolder = modelState.modelIsFolder;
                }
                else {
                    brick = lightweightBrickForModelRow(row);
                }
                _bricks.append(std::move(brick));
            }
            else {
                ImageFile *imageFile =
                    imageFileFromModelIndex(_model->index(row, 0));
                const auto oldIt = oldIndexes.constFind(imageFile);
                if (imageFile && oldIt != oldIndexes.constEnd()) {
                    retained[*oldIt] = true;
                    MasonryBrick brick = std::move(oldBricks[*oldIt]);
                    populateBrickModelState(brick, row);
                    _bricks.append(std::move(brick));
                }
                else if (imageFile) {
                    MasonryBrick brick = brickForImage(imageFile);
                    populateBrickModelState(brick, row);
                    _bricks.append(std::move(brick));
                }
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
        item->setVisualRow({});
        item->setVisualFacadeReady(false);
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
    for (int index = 0; index < _bricks.size(); ++index) {
        if (brickIsImage(index)) {
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
        _currentIndex < _bricks.size() &&
        brickPath(_currentIndex) == previousCurrentPath;
    if (!currentIdentityPreserved || previousCurrentIndex != _currentIndex) {
        emit currentIndexChanged();
    }
}

void MasonryLayout::onModelAboutToBeReset() {
    // `beginResetModel()` and `endResetModel()` run in one GUI-thread stack.
    // Retain each painted delegate in its current visual slot across that
    // interval. Clearing `model`, `viewIndex`, and `sourceIndex` here used to
    // invalidate every QML binding, only to assign all three again a few
    // milliseconds later. Under repeated directory navigation that doubled
    // native-icon cancellation and binding work for every visible row.
    cancelDeferredDelegateMaterialization();
    releaseResetSlotItems();
    _lightweightRewrapPending = false;
    ++_lightweightRewrapGeneration;
    _resetSlotPresentationMode = _presentationMode;
    _resetSlotWidth = width();
    _resetSlotHeight = height();
    _resetSlotDensity = _density;
    _resetSlotDelegate = _delegate;
    _resetSlotViewport = _viewport;
    _resetSlotReusePending = _delegate && _viewport;
    QSet<BrickItem *> retainedItems;
    if (_resetSlotReusePending) {
        _resetSlotItems.reserve(_activeBrickIndexes.size());
        retainedItems.reserve(_activeBrickIndexes.size());
        for (const int index : std::as_const(_activeBrickIndexes)) {
            if (index < 0 || index >= _bricks.size()
                || !_bricks.at(index).item) {
                continue;
            }
            BrickItem *item = _bricks.at(index).item;
            _resetSlotItems.insert(index, item);
            _resetSlotModels.insert(
                index, item->property("model").value<ImageFile *>());
            if (_deferDelegateRefreshOnReset) {
                // Until polish installs the current row, an old slot must be
                // neither paintable nor hit-testable. The scene graph cannot
                // observe this intermediate hidden state: the matching polish
                // pass runs before the next synchronization.
                item->setVisible(false);
                item->setVisualFacadeReady(false);
            }
            retainedItems.insert(item);
        }
    }

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
    for (BrickItem *item : std::as_const(_usedBrickItems)) {
        if (retainedItems.contains(item)) {
            continue;
        }
        item->setVisible(false);
        item->setVisualRow({});
        item->setVisualFacadeReady(false);
        item->setProperty("model", QVariant());
        item->setProperty("viewIndex", -1);
        item->setProperty("sourceIndex", -1);
        _freeBrickItems.insert(item);
    }
    _usedBrickItems.clear();
    if (hadVisibleIndexes) {
        emit visibleIndexesChanged();
    }
    if (hadOverscanIndexes) {
        emit overscanIndexesChanged();
    }
}

bool MasonryLayout::resetSlotLayoutMatches() const {
    if (!(_resetSlotReusePending
        && _resetSlotDelegate == _delegate
        && _resetSlotViewport == _viewport
        && _resetSlotPresentationMode == _presentationMode
        && qFuzzyCompare(_resetSlotWidth + 1, width() + 1)
        && qFuzzyCompare(_resetSlotHeight + 1, height() + 1)
        && qFuzzyCompare(_resetSlotDensity + 1, _density + 1))) {
        return false;
    }
    for (auto slot = _resetSlotItems.cbegin();
         slot != _resetSlotItems.cend(); ++slot) {
        BrickItem *item = slot.value();
        const QPointer<ImageFile> expected = _resetSlotModels.value(
            slot.key());
        const bool validSnapshotSlot = _visualSnapshotRole >= 0
            && item && item->visualRow().value(
                QStringLiteral("valid")).toBool()
            && item->property("viewIndex").toInt() == slot.key();
        if (!item || item->parentItem() != _viewport
            || (_visualSnapshotRole >= 0 && !validSnapshotSlot)
            || (_visualSnapshotRole < 0
                && (expected.isNull()
                    || item->property("model").value<ImageFile *>()
                        != expected.data()))) {
            return false;
        }
    }
    return true;
}

void MasonryLayout::releaseResetSlotItems(bool clearBindings) {
    for (BrickItem *item : std::as_const(_resetSlotItems)) {
        if (!item) {
            continue;
        }
        item->stopGeometryAnimation();
        item->setVisible(false);
        if (clearBindings) {
            item->setVisualRow({});
            item->setVisualFacadeReady(false);
            item->setProperty("model", QVariant());
            item->setProperty("viewIndex", -1);
            item->setProperty("sourceIndex", -1);
        }
        _usedBrickItems.remove(item);
        _freeBrickItems.insert(item);
    }
    _resetSlotItems.clear();
    _resetSlotModels.clear();
    _resetSlotReusePending = false;
    _resetSlotDelegate = nullptr;
    _resetSlotViewport = nullptr;
}

void MasonryLayout::scheduleDeferredDelegateRefresh() {
    cancelDeferredDelegateMaterialization();
    _delegateRefreshPending = true;
    const quint64 generation = ++_delegateRefreshGeneration;
    polish();
    update();
    // Polish is tied to a renderable window. Keep a zero-delay fallback for
    // offscreen/windowless embedders and tests; whichever path runs first
    // consumes the same generation and the other becomes a no-op.
    QTimer::singleShot(0, this, [this, generation]() {
        if (_delegateRefreshPending
            && generation == _delegateRefreshGeneration) {
            flushDeferredDelegateRefresh();
        }
    });
}

void MasonryLayout::flushDeferredDelegateRefresh() {
    if (!_delegateRefreshPending) {
        return;
    }
    _delegateRefreshPending = false;
    ++_delegateRefreshGeneration;
    const bool snapshotOnly = _visualSnapshotRole >= 0;
    _visualSnapshotRefresh = snapshotOnly;
    updateProperties(false);
    _visualSnapshotRefresh = false;
    if (snapshotOnly) {
        scheduleDeferredDelegateMaterialization();
    }
}

void MasonryLayout::cancelDeferredDelegateMaterialization() {
    _delegateMaterializationPending = false;
    _delegateMaterializationRows.clear();
    _delegateMaterializationEntries.clear();
    _delegateMaterializationCursor = 0;
    _delegateSynchronizedGeneration.store(0, std::memory_order_release);
    if (_delegateFrameSwappedConnection) {
        disconnect(_delegateFrameSwappedConnection);
        _delegateFrameSwappedConnection = {};
    }
    if (_delegateAfterSynchronizingConnection) {
        disconnect(_delegateAfterSynchronizingConnection);
        _delegateAfterSynchronizingConnection = {};
    }
}

void MasonryLayout::scheduleDeferredDelegateMaterialization() {
    cancelDeferredDelegateMaterialization();
    _delegateMaterializationPending = true;
    const quint64 generation = ++_delegateRefreshGeneration;

    QList<int> orderedRows;
    orderedRows.reserve(_activeBrickIndexes.size());
    if (_activeBrickIndexes.contains(_currentIndex)) {
        orderedRows.append(_currentIndex);
    }
    for (const int row : std::as_const(_activeBrickIndexes)) {
        if (row != _currentIndex && brickIsImage(row)) {
            orderedRows.append(row);
        }
    }
    for (const int row : std::as_const(_activeBrickIndexes)) {
        if (row != _currentIndex && !brickIsImage(row)) {
            orderedRows.append(row);
        }
    }
    _delegateMaterializationRows.reserve(orderedRows.size());
    _delegateMaterializationEntries.reserve(orderedRows.size());
    for (const int row : std::as_const(orderedRows)) {
        enqueueDeferredDelegateMaterialization(row);
    }

    if (QQuickWindow *quickWindow = window()) {
        // A frameSwapped from an already-in-flight old scene is not proof
        // that the new snapshot was painted. Arm the swap only after the
        // render thread has synchronized at least once since this generation
        // was installed.
        _delegateAfterSynchronizingConnection = connect(
            quickWindow, &QQuickWindow::afterSynchronizing, this,
            [this, generation]() {
                _delegateSynchronizedGeneration.store(
                    generation, std::memory_order_release);
            }, Qt::DirectConnection);
        _delegateFrameSwappedConnection = connect(
            quickWindow, &QQuickWindow::frameSwapped, this,
            [this, generation]() {
                if (_delegateSynchronizedGeneration.load(
                        std::memory_order_acquire) == generation) {
                    beginDeferredDelegateMaterialization(generation);
                }
            }, Qt::QueuedConnection);
        // A hidden/offscreen surface may never swap. This fallback affects no
        // visible first frame and keeps tests/lifecycle users from retaining
        // old QObject facades indefinitely.
        QPointer<QQuickWindow> guardedWindow(quickWindow);
        QTimer::singleShot(100, this, [this, generation, guardedWindow]() {
            if (!guardedWindow || !guardedWindow->isVisible()
                || !guardedWindow->isExposed()) {
                beginDeferredDelegateMaterialization(generation);
            }
        });
    }
    else {
        QTimer::singleShot(0, this, [this, generation]() {
            beginDeferredDelegateMaterialization(generation);
        });
    }
}

void MasonryLayout::enqueueDeferredDelegateMaterialization(int row) {
    if (!_delegateMaterializationPending || row < 0
        || row >= _bricks.size()) {
        return;
    }
    const QString entryId = _bricks.at(row).modelIdentity;
    if (_delegateMaterializationEntries.value(row) == entryId
        && _delegateMaterializationEntries.contains(row)) {
        return;
    }
    _delegateMaterializationEntries.insert(row, entryId);
    _delegateMaterializationRows.append(qMakePair(row, entryId));
}

void MasonryLayout::beginDeferredDelegateMaterialization(
    quint64 generation) {
    if (!_delegateMaterializationPending
        || generation != _delegateRefreshGeneration) {
        return;
    }
    if (_delegateFrameSwappedConnection) {
        disconnect(_delegateFrameSwappedConnection);
        _delegateFrameSwappedConnection = {};
    }
    if (_delegateAfterSynchronizingConnection) {
        disconnect(_delegateAfterSynchronizingConnection);
        _delegateAfterSynchronizingConnection = {};
    }
    materializeDeferredDelegateBatch(generation);
}

void MasonryLayout::materializeDeferredDelegateBatch(
    quint64 generation) {
    if (!_delegateMaterializationPending
        || generation != _delegateRefreshGeneration) {
        return;
    }
    QElapsedTimer budget;
    budget.start();
    int completed = 0;
    QSet<int> thumbnailRows;
    while (_delegateMaterializationCursor
               < _delegateMaterializationRows.size()
           && completed < 4 && budget.nsecsElapsed() < 1'000'000) {
        const auto [row, expectedEntryId] =
            _delegateMaterializationRows.at(
                _delegateMaterializationCursor++);
        const auto queuedEntry = _delegateMaterializationEntries.constFind(
            row);
        if (queuedEntry != _delegateMaterializationEntries.cend()
            && *queuedEntry == expectedEntryId) {
            _delegateMaterializationEntries.remove(row);
        }
        ++completed;
        if (row < 0 || row >= _bricks.size()
            || !_activeBrickIndexes.contains(row)
            || _bricks.at(row).modelIdentity != expectedEntryId) {
            continue;
        }
        BrickItem *item = _bricks[row].item;
        if (!item || item->property("viewIndex").toInt() != row
            || item->visualRow().value(
                QStringLiteral("entryId")).toString() != expectedEntryId) {
            continue;
        }
        ImageFile *image = materializeImageForIndex(row);
        if (image && item->property("model").value<ImageFile *>() != image) {
            item->setProperty("model", QVariant::fromValue(image));
        }
        item->setVisualFacadeReady(image != nullptr);
        if (image && _overscanIndexSet.contains(row)) {
            thumbnailRows.insert(row);
        }
    }
    if (!thumbnailRows.isEmpty()) {
        // Thumbnail planning was deliberately suppressed before the first
        // swap. Resume it only for facades admitted by this bounded batch;
        // already-materialized rows bypass the pending-queue gate above.
        planViewportThumbnails(thumbnailRows);
    }
    if (_delegateMaterializationCursor
        >= _delegateMaterializationRows.size()) {
        _delegateMaterializationPending = false;
        _delegateMaterializationRows.clear();
        _delegateMaterializationEntries.clear();
        _delegateMaterializationCursor = 0;
        return;
    }
    // Yield between small batches. A repeated navigation reset increments the
    // generation and cancels this current-model-only queue before it can bind
    // a facade to the replacement catalog.
    QTimer::singleShot(8, this, [this, generation]() {
        materializeDeferredDelegateBatch(generation);
    });
}

void MasonryLayout::scheduleLightweightRewrap() {
    if (_lightweightRewrapPending) {
        return;
    }
    _lightweightRewrapPending = true;
    const quint64 generation = ++_lightweightRewrapGeneration;
    // Header readers finish independently and their historical isLast marker
    // follows submission order, not completion order. Gate relayout to at
    // most one pass per frame instead of treating that marker as a barrier.
    QTimer::singleShot(16, this, [this, generation]() {
        if (generation != _lightweightRewrapGeneration) {
            return;
        }
        _lightweightRewrapPending = false;
        if (_presentationMode != Masonry || !canUseLightweightRows()) {
            return;
        }
        rewrap(false);
        // Rewrap can change the exact target tier without changing the set of
        // overscan indexes, so explicitly re-plan that bounded window.
        planViewportThumbnails(_overscanIndexSet);
    });
}

void MasonryLayout::flushLightweightRewrap() {
    if (_lightweightRewrapPending) {
        _lightweightRewrapPending = false;
        ++_lightweightRewrapGeneration;
    }
    rewrap(false);
    planViewportThumbnails(_overscanIndexSet);
}

void MasonryLayout::onModelReset() {
    const bool traceReset = qEnvironmentVariableIsSet(
        "F4_NAV_BENCHMARK_TRACE");
    QElapsedTimer resetTimer;
    if (traceReset) {
        resetTimer.start();
    }
    const int previousCurrentIndex = _currentIndex;
    const QString preservedCurrentPath =
        _preserveCurrentItemPositionOnNextModelReset
            ? _preservedCurrentItemFullPath
            : QString();
    if (_model) {
        _bricks.reserve(_model->rowCount());
        const bool lightweightRows = canUseLightweightRows();
        for (int i = 0; i < _model->rowCount(); i++) {
            MasonryBrick brick;
            if (lightweightRows) {
                brick = lightweightBrickForModelRow(i);
            }
            else {
                ImageFile *imageFile = imageFileFromModelIndex(
                    _model->index(i, 0));
                if (!imageFile) {
                    continue;
                }
                brick = brickForImage(imageFile);
            }
            if (!lightweightRows) {
                populateBrickModelState(brick, i);
            }
            _bricks.append(std::move(brick));
        }
    }
    const qint64 rowsBuiltNs = traceReset
        ? resetTimer.nsecsElapsed() : 0;
    restorePreservedCurrentItemPosition();
    if (_bricks.isEmpty()) {
        _currentIndex = -1;
    }
    else if (_currentIndex < 0 || _currentIndex >= _bricks.size()) {
        _currentIndex = qBound(0, _currentIndex, _bricks.size() - 1);
    }

    // QML is allowed to react to modelAboutToBeReset. If that changed the
    // renderer or its geometry, fall back to the ordinary free-pool path;
    // visual-row retention is valid only for an unchanged layout contract.
    if (!resetSlotLayoutMatches()) {
        releaseResetSlotItems();
    }

    const qint64 modelSignalStartedNs = traceReset
        ? resetTimer.nsecsElapsed() : 0;
    emit modelChanged();
    const qint64 modelSignalCompletedNs = traceReset
        ? resetTimer.nsecsElapsed() : 0;
    // Model identity and hit-test geometry change atomically at reset. Move
    // retained delegates to their target rect in the same frame rather than
    // animating them from stale catalog positions for 500 ms.
    const bool synchronousSnapshotRefresh =
        _deferDelegateRefreshOnReset && _visualSnapshotRole >= 0;
    if (synchronousSnapshotRefresh) {
        // A polish requested from modelReset can miss the threaded render
        // loop's imminent sync cutoff and add a whole vsync even though the
        // snapshot work itself is only a few milliseconds. Install the POD
        // facade as part of this reset transaction; QObject/thumbnail work
        // remains gated behind frameSwapped below.
        _delegateRefreshPending = false;
        ++_delegateRefreshGeneration;
        _visualSnapshotRefresh = true;
    }
    else if (_deferDelegateRefreshOnReset) {
        scheduleDeferredDelegateRefresh();
    }
    rewrap(false);
    if (synchronousSnapshotRefresh) {
        _visualSnapshotRefresh = false;
        scheduleDeferredDelegateMaterialization();
        // Property/geometry changes already dirty the scene graph. Explicitly
        // request a frame as well so a quiet threaded window can consume the
        // synchronous snapshot at the nearest render-loop opportunity.
        update();
    }
    const qint64 rewrapCompletedNs = traceReset
        ? resetTimer.nsecsElapsed() : 0;
    restorePendingThumbnailRequestsAfterModelReset();
    if (_preserveDecodeQueueForCurrentRebuild) {
        _skipThumbnailBackfillUntilFlush = true;
    }
    _preserveDecodeQueueForCurrentRebuild = false;
    positionViewport();
    const qint64 viewportCompletedNs = traceReset
        ? resetTimer.nsecsElapsed() : 0;

    _imageCount = 0;
    for (int i = 0; i < _bricks.size(); i++) {
        if (brickIsImage(i)) {
            _imageCount++;
        }
    }
    emit imageCountChanged();

    updateCurrentImageIndex();
    emit countChanged();
    const qint64 countCompletedNs = traceReset
        ? resetTimer.nsecsElapsed() : 0;

    if (!_quickSearch->mask().isEmpty()) {
        _quickSearch->updateItemsText();
    }
    const bool currentIdentityPreserved =
        !preservedCurrentPath.isEmpty() && _currentIndex >= 0 &&
        _currentIndex < _bricks.size() &&
        brickPath(_currentIndex) == preservedCurrentPath;
    if (!currentIdentityPreserved ||
        previousCurrentIndex != _currentIndex) {
        emit currentIndexChanged();
    }
    if (traceReset) {
        const qint64 completedNs = resetTimer.nsecsElapsed();
        qInfo().nospace()
            << "F4_NAV_BENCHMARK_TRACE masonry.reset rowsNs="
            << rowsBuiltNs << " preSignalNs="
            << (modelSignalStartedNs - rowsBuiltNs)
            << " modelSignalNs="
            << (modelSignalCompletedNs - modelSignalStartedNs)
            << " rewrapNs="
            << (rewrapCompletedNs - modelSignalCompletedNs)
            << " viewportNs="
            << (viewportCompletedNs - rewrapCompletedNs)
            << " countNs=" << (countCompletedNs - viewportCompletedNs)
            << " tailNs=" << (completedNs - countCompletedNs)
            << " totalNs=" << completedNs;
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
            if (brickPath(i) == _preservedCurrentItemFullPath) {
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
            if (brickPath(i) == _preservedViewportAnchorFullPath) {
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
    if (_presentationMode == Columns) {
        // Columns geometry is expressed on the device-pixel lattice. Moving
        // the window between screens therefore changes its logical stride.
        rewrap(false);
    }
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

bool MasonryLayout::deferDelegateRefreshOnReset() const {
    return _deferDelegateRefreshOnReset;
}

void MasonryLayout::setDeferDelegateRefreshOnReset(bool defer) {
    if (_deferDelegateRefreshOnReset == defer) {
        return;
    }
    _deferDelegateRefreshOnReset = defer;
    emit deferDelegateRefreshOnResetChanged();
}

MasonryLayout::PresentationMode MasonryLayout::presentationMode() const {
    return _presentationMode;
}

void MasonryLayout::requestRewrap(bool animate) {
    if (_layoutUpdateDepth > 0) {
        _layoutUpdateNeedsRewrap = true;
        // One non-animated participant makes the whole atomic commit
        // non-animated. Presentation switches always take this path.
        _layoutUpdateAnimate = _layoutUpdateAnimate && animate;
        return;
    }
    rewrap(animate);
}

void MasonryLayout::capturePresentationViewportAnchor(
    qreal *viewportY, bool *wasVisible) const {
    if (!viewportY || !wasVisible) {
        return;
    }
    *viewportY = 0;
    *wasVisible = false;
    if (_currentIndex < 0 || _currentIndex >= _bricks.size()) {
        return;
    }
    const QRectF geometry = indexGeometry(_currentIndex);
    *wasVisible = geometry.isValid() && !geometry.isEmpty()
        && geometry.top() < _contentY + height()
        && geometry.bottom() > _contentY;
    if (*wasVisible) {
        *viewportY = geometry.top() - _contentY;
    }
}

void MasonryLayout::beginLayoutUpdate() {
    if (_layoutUpdateDepth++ > 0) {
        return;
    }
    _layoutUpdateNeedsRewrap = false;
    _layoutUpdateAnimate = true;
    _layoutUpdateNeedsPositionViewport = false;
    _layoutUpdateNeedsScrollRefresh = false;
    _layoutUpdatePresentationModeChanged = false;
    capturePresentationViewportAnchor(
        &_layoutUpdateCurrentViewportY,
        &_layoutUpdateCurrentWasVisible);
}

void MasonryLayout::endLayoutUpdate() {
    if (_layoutUpdateDepth <= 0) {
        return;
    }
    if (--_layoutUpdateDepth > 0) {
        return;
    }

    const bool needsRewrap = _layoutUpdateNeedsRewrap;
    const bool animate = _layoutUpdateAnimate;
    const bool needsPositionViewport =
        _layoutUpdateNeedsPositionViewport;
    const bool needsScrollRefresh = _layoutUpdateNeedsScrollRefresh;
    const bool presentationModeChanged =
        _layoutUpdatePresentationModeChanged;
    const qreal previousCurrentViewportY =
        _layoutUpdateCurrentViewportY;
    const bool previousCurrentWasVisible =
        _layoutUpdateCurrentWasVisible;

    _layoutUpdateNeedsRewrap = false;
    _layoutUpdateAnimate = true;
    _layoutUpdateNeedsPositionViewport = false;
    _layoutUpdateNeedsScrollRefresh = false;
    _layoutUpdatePresentationModeChanged = false;

    if (needsPositionViewport) {
        positionViewport();
    }
    if (presentationModeChanged) {
        // setDensity() may have requested a same-mode viewport anchor after
        // setPresentationMode(). A cross-mode switch owns the stronger
        // current-item viewport contract below.
        _preserveViewportAnchorForNextRewrap = false;
        completePresentationModeChange(previousCurrentViewportY,
                                       previousCurrentWasVisible);
    }
    else if (needsRewrap) {
        rewrap(animate);
    }
    if (needsScrollRefresh) {
        updateNeedScroll();
    }
}

void MasonryLayout::completePresentationModeChange(
    qreal previousCurrentViewportY, bool previousCurrentWasVisible) {
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
            if (_presentationMode != Columns
                && previousCurrentWasVisible) {
                const qreal desired = currentGeometry.top()
                    - previousCurrentViewportY;
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

void MasonryLayout::setPresentationMode(PresentationMode mode) {
    const int normalizedValue = qBound(
        static_cast<int>(Masonry), static_cast<int>(mode),
        static_cast<int>(Icons));
    mode = static_cast<PresentationMode>(normalizedValue);
    if (_presentationMode == mode) {
        return;
    }
    qreal previousCurrentViewportY = _layoutUpdateCurrentViewportY;
    bool previousCurrentWasVisible = _layoutUpdateCurrentWasVisible;
    if (_layoutUpdateDepth == 0) {
        capturePresentationViewportAnchor(
            &previousCurrentViewportY, &previousCurrentWasVisible);
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
    if (_layoutUpdateDepth > 0) {
        _layoutUpdatePresentationModeChanged = true;
        requestRewrap(false);
        return;
    }
    completePresentationModeChange(previousCurrentViewportY,
                                   previousCurrentWasVisible);
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
        requestRewrap(false);
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
    requestRewrap();
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

BrickVisualRow::BrickVisualRow(QObject *parent)
    : QObject(parent) {}

void BrickVisualRow::applySnapshot(const QVariantMap &snapshot) {
    const bool nextValid = snapshot.value(QStringLiteral("valid")).toBool();
    const QString nextEntryId = snapshot.value(
        QStringLiteral("entryId")).toString();
    const int nextSourceIndex = snapshot.value(
        QStringLiteral("sourceIndex"), -1).toInt();
    const QString nextLocalPath = snapshot.value(
        QStringLiteral("localPath")).toString();
    const QString nextText = snapshot.value(QStringLiteral("text")).toString();
    const bool nextIsFolder = snapshot.value(
        QStringLiteral("isFolder")).toBool();
    const bool nextIsImage = snapshot.value(
        QStringLiteral("isImage")).toBool();
    const bool nextIsSelected = snapshot.value(
        QStringLiteral("isSelected")).toBool();
    const QString nextIconPath = snapshot.value(
        QStringLiteral("iconPath")).toString();
    const QVariantMap nextHighlightStyle = snapshot.value(
        QStringLiteral("highlightStyle")).toMap();
    const QVariantMap nextDisplayFields = snapshot.value(
        QStringLiteral("displayFields")).toMap();
    const QString nextImageIdUrl = snapshot.value(
        QStringLiteral("imageIdUrl")).toString();
    const QString nextDisplayBaseName = nextDisplayFields.value(
        QStringLiteral("displayBaseName")).toString();
    const QString nextDisplayExtension = nextDisplayFields.value(
        QStringLiteral("displayExtension")).toString();
    const QString nextSizeText = nextDisplayFields.value(
        QStringLiteral("sizeText")).toString();
    const bool nextIsHidden = nextDisplayFields.value(
        QStringLiteral("isHidden")).toBool();
    const QString nextHighlightMarker = nextHighlightStyle.value(
        QStringLiteral("marker")).toString();
    const auto highlightPatch = [&nextHighlightStyle](const char *name) {
        return nextHighlightStyle.value(QString::fromLatin1(name)).toMap();
    };
    const QVariantMap normalPatch = highlightPatch("normal");
    const QVariantMap selectedPatch = highlightPatch("selected");
    const QVariantMap cursorPatch = highlightPatch("cursor");
    const QVariantMap selectedCursorPatch = highlightPatch("selectedCursor");
    const QString nextNormalForeground = normalPatch.value(
        QStringLiteral("foreground")).toString();
    const QString nextNormalBackground = normalPatch.value(
        QStringLiteral("background")).toString();
    const QString nextSelectedForeground = selectedPatch.value(
        QStringLiteral("foreground")).toString();
    const QString nextSelectedBackground = selectedPatch.value(
        QStringLiteral("background")).toString();
    const QString nextCursorForeground = cursorPatch.value(
        QStringLiteral("foreground")).toString();
    const QString nextCursorBackground = cursorPatch.value(
        QStringLiteral("background")).toString();
    const QString nextSelectedCursorForeground = selectedCursorPatch.value(
        QStringLiteral("foreground")).toString();
    const QString nextSelectedCursorBackground = selectedCursorPatch.value(
        QStringLiteral("background")).toString();

    const auto assign = [](auto &target, const auto &next) {
        if (target == next) {
            return false;
        }
        target = next;
        return true;
    };
    // Commit every backing field before notifying QML. A reset therefore
    // never evaluates a binding against a half-old/half-new row, and each
    // targeted dependency runs at most once for this snapshot.
    const bool validChangedFlag = assign(_valid, nextValid);
    const bool entryIdChangedFlag = assign(_entryId, nextEntryId);
    const bool sourceIndexChangedFlag = assign(_sourceIndex, nextSourceIndex);
    const bool localPathChangedFlag = assign(_localPath, nextLocalPath);
    const bool textChangedFlag = assign(_text, nextText);
    const bool isFolderChangedFlag = assign(_isFolder, nextIsFolder);
    const bool isImageChangedFlag = assign(_isImage, nextIsImage);
    const bool isSelectedChangedFlag = assign(_isSelected, nextIsSelected);
    const bool iconPathChangedFlag = assign(_iconPath, nextIconPath);
    const bool highlightStyleChangedFlag = assign(
        _highlightStyle, nextHighlightStyle);
    const bool displayFieldsChangedFlag = assign(
        _displayFields, nextDisplayFields);
    const bool imageIdUrlChangedFlag = assign(_imageIdUrl, nextImageIdUrl);
    const bool displayBaseNameChangedFlag = assign(
        _displayBaseName, nextDisplayBaseName);
    const bool displayExtensionChangedFlag = assign(
        _displayExtension, nextDisplayExtension);
    const bool sizeTextChangedFlag = assign(_sizeText, nextSizeText);
    const bool isHiddenChangedFlag = assign(_isHidden, nextIsHidden);
    const bool highlightMarkerChangedFlag = assign(
        _highlightMarker, nextHighlightMarker);
    const bool normalForegroundChangedFlag = assign(
        _normalForeground, nextNormalForeground);
    const bool normalBackgroundChangedFlag = assign(
        _normalBackground, nextNormalBackground);
    const bool selectedForegroundChangedFlag = assign(
        _selectedForeground, nextSelectedForeground);
    const bool selectedBackgroundChangedFlag = assign(
        _selectedBackground, nextSelectedBackground);
    const bool cursorForegroundChangedFlag = assign(
        _cursorForeground, nextCursorForeground);
    const bool cursorBackgroundChangedFlag = assign(
        _cursorBackground, nextCursorBackground);
    const bool selectedCursorForegroundChangedFlag = assign(
        _selectedCursorForeground, nextSelectedCursorForeground);
    const bool selectedCursorBackgroundChangedFlag = assign(
        _selectedCursorBackground, nextSelectedCursorBackground);

    if (validChangedFlag) emit validChanged();
    if (entryIdChangedFlag) emit entryIdChanged();
    if (sourceIndexChangedFlag) emit sourceIndexChanged();
    if (localPathChangedFlag) emit localPathChanged();
    if (textChangedFlag) emit textChanged();
    if (isFolderChangedFlag) emit isFolderChanged();
    if (isImageChangedFlag) emit isImageChanged();
    if (isSelectedChangedFlag) emit isSelectedChanged();
    if (iconPathChangedFlag) emit iconPathChanged();
    if (highlightStyleChangedFlag) emit highlightStyleChanged();
    if (displayFieldsChangedFlag) emit displayFieldsChanged();
    if (imageIdUrlChangedFlag) emit imageIdUrlChanged();
    if (displayBaseNameChangedFlag) emit displayBaseNameChanged();
    if (displayExtensionChangedFlag) emit displayExtensionChanged();
    if (sizeTextChangedFlag) emit sizeTextChanged();
    if (isHiddenChangedFlag) emit isHiddenChanged();
    if (highlightMarkerChangedFlag) emit highlightMarkerChanged();
    if (normalForegroundChangedFlag) emit normalForegroundChanged();
    if (normalBackgroundChangedFlag) emit normalBackgroundChanged();
    if (selectedForegroundChangedFlag) emit selectedForegroundChanged();
    if (selectedBackgroundChangedFlag) emit selectedBackgroundChanged();
    if (cursorForegroundChangedFlag) emit cursorForegroundChanged();
    if (cursorBackgroundChangedFlag) emit cursorBackgroundChanged();
    if (selectedCursorForegroundChangedFlag) {
        emit selectedCursorForegroundChanged();
    }
    if (selectedCursorBackgroundChangedFlag) {
        emit selectedCursorBackgroundChanged();
    }
}

bool BrickVisualRow::valid() const { return _valid; }
QString BrickVisualRow::entryId() const { return _entryId; }
int BrickVisualRow::sourceIndex() const { return _sourceIndex; }
QString BrickVisualRow::localPath() const { return _localPath; }
QString BrickVisualRow::text() const { return _text; }
bool BrickVisualRow::isFolder() const { return _isFolder; }
bool BrickVisualRow::isImage() const { return _isImage; }
bool BrickVisualRow::isSelected() const { return _isSelected; }
QString BrickVisualRow::iconPath() const { return _iconPath; }
QVariantMap BrickVisualRow::highlightStyle() const {
    return _highlightStyle;
}
QVariantMap BrickVisualRow::displayFields() const {
    return _displayFields;
}
QString BrickVisualRow::displayBaseName() const { return _displayBaseName; }
QString BrickVisualRow::displayExtension() const { return _displayExtension; }
QString BrickVisualRow::sizeText() const { return _sizeText; }
bool BrickVisualRow::isHidden() const { return _isHidden; }
QString BrickVisualRow::highlightMarker() const { return _highlightMarker; }
QString BrickVisualRow::normalForeground() const {
    return _normalForeground;
}
QString BrickVisualRow::normalBackground() const {
    return _normalBackground;
}
QString BrickVisualRow::selectedForeground() const {
    return _selectedForeground;
}
QString BrickVisualRow::selectedBackground() const {
    return _selectedBackground;
}
QString BrickVisualRow::cursorForeground() const {
    return _cursorForeground;
}
QString BrickVisualRow::cursorBackground() const {
    return _cursorBackground;
}
QString BrickVisualRow::selectedCursorForeground() const {
    return _selectedCursorForeground;
}
QString BrickVisualRow::selectedCursorBackground() const {
    return _selectedCursorBackground;
}
QString BrickVisualRow::imageIdUrl() const { return _imageIdUrl; }

BrickItem::BrickItem(QQuickItem *parent)
    : QQuickItem(parent) {
    _visualModel = new BrickVisualRow(this);
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

    connect(_geometryAnimationGroup, &QAbstractAnimation::stateChanged,
            this, [this](QAbstractAnimation::State current,
                         QAbstractAnimation::State previous) {
        const bool isRunning = current == QAbstractAnimation::Running;
        const bool wasRunning = previous == QAbstractAnimation::Running;
        if (isRunning != wasRunning) {
            emit geometryAnimationRunningChanged();
        }
    });
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

void BrickItem::setVisualRow(const QVariantMap &visualRow) {
    if (_visualRow == visualRow) {
        return;
    }
    _visualRow = visualRow;
    _visualModel->applySnapshot(visualRow);
    emit visualRowChanged();
}

void BrickItem::setVisualFacadeReady(bool ready) {
    if (_visualFacadeReady == ready) {
        return;
    }
    _visualFacadeReady = ready;
    emit visualFacadeReadyChanged();
}

QString BrickItem::iconLabelText() const {
    return _iconLabelText;
}

QVariantMap BrickItem::visualRow() const {
    return _visualRow;
}

bool BrickItem::visualFacadeReady() const {
    return _visualFacadeReady;
}

bool BrickItem::geometryAnimationRunning() const {
    return _geometryAnimationGroup
        && _geometryAnimationGroup->state() == QAbstractAnimation::Running;
}

QObject *BrickItem::visualModel() const {
    return _visualModel;
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
    updateModelRoleCache();
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
        if (brickIsImage(i)) {
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
    requestRewrap(false);
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
        if (brickIsFolder(i)) {
            if (!_listView) {
                _bricks[i].originalSize = GridView_Folder;
                _bricks[i].lineBreakAfter = false;
            }
            else {
                const bool folderView = _bricks[i].image
                    && _bricks[i].image->folderView();
                _bricks[i].originalSize = QSize(
                    0, folderView ? 0 : listRowHeight());
                _bricks[i].lineBreakAfter = true;
            }
        }
    }
    requestRewrap();

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
        if (brickIsImage(i)) {
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
    if (_layoutUpdateDepth > 0) {
        _layoutUpdateNeedsPositionViewport = true;
    }
    else {
        positionViewport();
    }
    requestRewrap();
    emit paddingLeftChanged();
}

qreal MasonryLayout::paddingRight() const {
    return _paddingRight;
}

void MasonryLayout::setPaddingRight(qreal newPaddingRight) {
    if (qFuzzyCompare(_paddingRight, newPaddingRight))
        return;
    _paddingRight = newPaddingRight;
    requestRewrap();
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
    requestRewrap();
    emit paddingTopChanged();
}

qreal MasonryLayout::paddingBottom() const {
    return _paddingBottom;
}

void MasonryLayout::setPaddingBottom(qreal newPaddingBottom) {
    if (qFuzzyCompare(_paddingBottom, newPaddingBottom))
        return;
    _paddingBottom = newPaddingBottom;
    requestRewrap();
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
        ImageFile *image = const_cast<MasonryLayout *>(this)
                               ->materializeImageForIndex(_currentIndex);
        return image ? image->exifList() : QVariantList();
    }
    return QVariantList();
}

QQuickItem *MasonryLayout::viewport() const {
    return _viewport;
}
