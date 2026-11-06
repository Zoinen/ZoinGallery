#include "MasonryLayout.h"

#include "GalleryPixelGrid.h"
#include "GalleryIconTextMeasurer.h"
#include "GalleryViewportMotion.h"

#include <ZoinGallery/MediaTimingTrace.h>

#include <QElapsedTimer>
#include <QTimer>
#include <QScopeGuard>

#include <utility>
#include <algorithm>

namespace
{
constexpr char DelegateCatalogGenerationProperty[] =
    "_zoinGalleryCatalogGeneration";
constexpr char DelegateVisualRevisionProperty[] =
    "_zoinGalleryVisualRevision";
constexpr int RetainedVectorSceneLimit = 16;
}

struct MasonryLayout::DelegateCommitContext
{
    bool trace = false;
    QElapsedTimer timer;
    QSet<BrickItem *> itemsToHide;
    QSet<int> retainedVectorIndexes;
    qint64 viewportSetsCompletedNs = 0;
    qint64 indexesCompletedNs = 0;
    qint64 retiredCompletedNs = 0;
    qint64 delegatesCompletedNs = 0;
    qint64 delegateAcquireNs = 0;
    qint64 visualSnapshotApplyNs = 0;
    qint64 visualSnapshotFetchNs = 0;
    qint64 visualRowNotifyNs = 0;
    qint64 delegateBindingNs = 0;
    qint64 delegateAssignmentNotifyNs = 0;
    qint64 delegateLayoutMetaNs = 0;
    qint64 delegateGeometryNs = 0;
    qint64 delegateVisibilityNs = 0;
    int reboundCount = 0;
    int createdOrPoppedCount = 0;
    int retainedSlotCount = 0;
    int retainedSpareCount = 0;
    int oldActiveCount = 0;
    int overlappingActiveCount = 0;
    int retainedActiveCount = 0;
    int delegateComponentMismatchCount = 0;
    int snapshotCount = 0;
    int identityNotificationRows = 0;
    int mediaNotificationRows = 0;
    int stateNotificationRows = 0;
    int styleNotificationRows = 0;
};

struct MasonryLayout::DelegateRowCommit
{
    int index = -1;
    MasonryBrick *brick = nullptr;
    BrickItem *item = nullptr;
    ImageFile *image = nullptr;
    QVariantMap replacementVisualSnapshot;
    qint64 phaseStartedNs = 0;
    bool itemPopped = false;
    bool deferMissingFacade = false;
    bool snapshotAlreadyCurrent = false;
    bool retainedSnapshotIdentity = false;
    bool replacementVisualSnapshotFetched = false;
    bool assignmentChanged = false;
};

QList<int> MasonryLayout::delegateIndexesForCurrentViewport() const
{
    const ZoinGallery::GalleryViewportWindow materialization =
        viewportMaterializationPlan();
    if (_presentationMode != Columns) {
        return indexesForVerticalRange(materialization.delegateStart,
                                       materialization.delegateEnd);
    }
    return indexesForHorizontalRange(materialization.delegateStart,
                                     materialization.delegateEnd);
}

QList<int> MasonryLayout::motionLeadIndexes(const ZoinGallery::GalleryViewportWindow &plan) const
{
    const qreal delta = _viewportMotionState->targetY.load(std::memory_order_acquire)
        - _contentY;
    const qreal lead = qMin(height(), qAbs(delta) + effectiveTargetExtent());
    if (delta >= 0)
        return indexesForVerticalRange(plan.visibleEnd, plan.visibleEnd + lead);

    auto indexes = indexesForVerticalRange(plan.visibleStart - lead, plan.visibleStart);
    std::reverse(indexes.begin(), indexes.end());
    return indexes;
}

void MasonryLayout::retireInactiveDelegates(
    const QSet<int> &retainedIndexes, DelegateCommitContext *context)
{
    const QSet<int> oldActiveIndexes = _activeBrickIndexes;
    context->retainedVectorIndexes = retainedVectorSceneIndexes();
    const QSet<int> &retainedVectors = context->retainedVectorIndexes;
    context->oldActiveCount = oldActiveIndexes.size();
    const quintptr targetDelegate = reinterpret_cast<quintptr>(
        delegateComponent(_presentationMode));
    int obsoleteCount = 0;
    for (const int index : oldActiveIndexes)
        obsoleteCount += !retainedIndexes.contains(index) && !retainedVectors.contains(index);
    int retirementBudget = keepsNearbyDelegates() && !_presentationModeCommitInProgress
        ? qMax(obsoleteCount > 64 ? 2 : 0, obsoleteCount - 128)
        : oldActiveIndexes.size();
    if (preparingViewportMotion() && _viewportMotionState->page)
        retirementBudget = qMax(qBound(0, obsoleteCount - 8, 8), obsoleteCount - 128);
    for (const int index : oldActiveIndexes) {
        const MasonryBrick *activeBrick = brickAt(index);
        const bool componentMatches = activeBrick && activeBrick->item
            && _delegatePool.delegateKey(activeBrick->item) == targetDelegate;
        if (retainedIndexes.contains(index)) {
            ++context->overlappingActiveCount;
            if (componentMatches) {
                ++context->retainedActiveCount;
            } else {
                ++context->delegateComponentMismatchCount;
            }
        }
        if (((retainedIndexes.contains(index) || retainedVectors.contains(index)) && componentMatches)
            || index < 0 || index >= logicalBrickCount()) {
            continue;
        }
        // Keep a bounded recent-row working set for reversing scroll direction.
        // Beyond 64 clipped rows, clear bindings/resources incrementally. The
        // hard cap also bounds temporary retirement debt after a distant jump.
        if (componentMatches && retirementBudget-- <= 0) continue;
        MasonryBrick *brick = brickAt(index);
        if (brick && brick->item) {
            pushBrickItem(brick->item);
            context->itemsToHide.insert(brick->item);
            brick->item = nullptr;
        }
        _activeBrickIndexes.remove(index);
        discardSparseBrick(index);
    }
    context->retiredCompletedNs = context->trace
        ? context->timer.nsecsElapsed() : 0;
}

bool MasonryLayout::prepareDelegateRow(
    int index, DelegateRowCommit *row, DelegateCommitContext *context)
{
    if (!row || index < 0 || index >= logicalBrickCount()) {
        return false;
    }
    MasonryBrick &brick = ensureBrickAt(index);
    if (!brick.normalizedSize.isValid() || brick.normalizedSize.isEmpty()) {
        return false;
    }
    row->index = index;
    row->brick = &brick;
    row->phaseStartedNs = context->trace
        ? context->timer.nsecsElapsed() : 0;
    row->deferMissingFacade = _visualSnapshotRole >= 0
        && _delegateMaterializationPending && !brick.image;
    row->image = (_visualSnapshotRefresh || row->deferMissingFacade)
        ? brick.image.data() : materializeImageForIndex(index);

    if (!brick.item) {
        const quintptr delegateKey = reinterpret_cast<quintptr>(
            delegateComponent(_presentationMode));
        BrickItem *retained = _resetSlotItems.take(index);
        if (!retained && _resetSlotReusePending && keepsMasonryResetSpares()) {
            retained = _delegatePool.findFree(delegateKey, brick.modelIdentity);
            context->retainedSpareCount += retained != nullptr;
        }
        if (!retained && !_resetSlotItems.isEmpty()) {
            auto slot = _resetSlotItems.begin();
            const int previousRow = slot.key();
            retained = slot.value();
            _resetSlotItems.erase(slot);
            _resetSlotModels.remove(previousRow);
        }
        if (retained) {
            brick.item = retained;
            _delegatePool.adopt(retained);
            ++context->retainedSlotCount;
        } else {
            row->itemPopped = true;
            ++context->createdOrPoppedCount;
            brick.item = _delegatePool.acquire(delegateKey, [&] {
                const int sourceIndex = row->image
                    ? row->image->index() : brick.modelSourceIndex;
                QVariantMap initialProperties{
                    {QStringLiteral("viewIndex"), index},
                    {QStringLiteral("sourceIndex"), sourceIndex},
                    {QStringLiteral("row"), brick.row},
                    {QStringLiteral("column"), brick.column},
                    {QStringLiteral("previewRect"),
                     brick.previewGeometry.translated(-brick.x, -brick.y)},
                    {QStringLiteral("iconLabelText"), brick.iconLabelText},
                    {QStringLiteral("x"), brick.geometry().x()},
                    {QStringLiteral("y"), brick.geometry().y()},
                    {QStringLiteral("width"), brick.geometry().width()},
                    {QStringLiteral("height"), brick.geometry().height()},
                    {QStringLiteral("visible"), false},
                    {QStringLiteral("visualFacadeReady"),
                     !_visualSnapshotRefresh && row->image != nullptr},
                };
                if (!_visualSnapshotRefresh && row->image) {
                    initialProperties.insert(QStringLiteral("model"),
                                             QVariant::fromValue(row->image));
                }
                if (_visualSnapshotRole >= 0) {
                    row->replacementVisualSnapshot = visualSnapshotForIndex(index);
                    row->replacementVisualSnapshotFetched = true;
                    initialProperties.insert(QStringLiteral("visualRow"),
                                             row->replacementVisualSnapshot);
                }
                return createComponent(_presentationMode, initialProperties);
            });
        }
    }
    row->item = brick.item;
    if (!row->item) {
        return false;
    }
    // Close before rebinding a recycled slot; publish only after its geometry.
    if (!brick.masonryGeometryReady)
        row->item->setProperty("masonryGeometryReady", false);
    const bool initialStateInstalled = row->itemPopped
        && row->item->viewIndex() == index
        && (_visualSnapshotRole < 0
            || (row->replacementVisualSnapshotFetched
                && row->item->visualRow() == row->replacementVisualSnapshot));
    if (initialStateInstalled) {
        row->item->setProperty(DelegateCatalogGenerationProperty,
                               QVariant::fromValue(
                                   _delegateCatalogGeneration));
        row->item->setProperty(DelegateVisualRevisionProperty,
                               QVariant::fromValue(
                                   brick.modelVisualRevision));
    }
    row->snapshotAlreadyCurrent = !_visualSnapshotRefresh
        && row->item->property(DelegateCatalogGenerationProperty)
               .toULongLong() == _delegateCatalogGeneration
        && row->item->viewIndex() == index
        && row->item->property(DelegateVisualRevisionProperty)
               .toULongLong() == brick.modelVisualRevision
        && row->item->visualRow().value(QStringLiteral("entryId"))
               .toString() == brick.modelIdentity;
    row->retainedSnapshotIdentity = _visualSnapshotRefresh
        && _visualSnapshotRole >= 0 && row->item->viewIndex() == index
        && row->item->visualRow().value(QStringLiteral("entryId"))
               .toString() == brick.modelIdentity;
    if (context->trace) {
        const qint64 now = context->timer.nsecsElapsed();
        context->delegateAcquireNs += now - row->phaseStartedNs;
        row->phaseStartedNs = now;
    }
    return true;
}

void MasonryLayout::bindDelegateRowIdentity(
    DelegateRowCommit *row, DelegateCommitContext *context)
{
    if (!row->snapshotAlreadyCurrent && row->retainedSnapshotIdentity) {
        row->replacementVisualSnapshot = visualSnapshotForIndex(row->index);
        row->replacementVisualSnapshotFetched = true;
        row->snapshotAlreadyCurrent =
            row->replacementVisualSnapshot == row->item->visualRow();
        if (context->trace) {
            const qint64 now = context->timer.nsecsElapsed();
            context->visualSnapshotFetchNs += now - row->phaseStartedNs;
            row->phaseStartedNs = now;
        }
    }
    if (_visualSnapshotRole >= 0 && _visualSnapshotRefresh) {
        row->item->setVisualFacadeReady(false);
        if (row->item->property("model").value<ImageFile *>()) {
            row->item->setProperty("model", QVariant());
        }
    }
    if (!_visualSnapshotRefresh
        && row->item->property("model").value<ImageFile *>() != row->image) {
        row->item->setProperty("model", QVariant::fromValue(row->image));
        ++context->reboundCount;
    }
    if (!_visualSnapshotRefresh) {
        row->item->setVisualFacadeReady(row->image != nullptr);
    }
    const int sourceIndex = row->image ? row->image->index()
                                       : row->brick->modelSourceIndex;
    row->assignmentChanged = row->item->prepareViewSourceIndexes(
        row->index, sourceIndex);
    if (row->item->property(DelegateCatalogGenerationProperty)
            .toULongLong() != _delegateCatalogGeneration) {
        row->item->setProperty(DelegateCatalogGenerationProperty,
                               QVariant::fromValue(
                                   _delegateCatalogGeneration));
    }
    if (context->trace) {
        const qint64 now = context->timer.nsecsElapsed();
        context->delegateBindingNs += now - row->phaseStartedNs;
        row->phaseStartedNs = now;
    }
}

void MasonryLayout::bindDelegateRowVisual(
    DelegateRowCommit *row, DelegateCommitContext *context)
{
    if (_visualSnapshotRole >= 0 && !row->snapshotAlreadyCurrent) {
        if (!row->replacementVisualSnapshotFetched) {
            row->replacementVisualSnapshot = visualSnapshotForIndex(
                row->index);
        }
        if (context->trace && !row->replacementVisualSnapshotFetched) {
            const qint64 now = context->timer.nsecsElapsed();
            context->visualSnapshotFetchNs += now - row->phaseStartedNs;
            row->phaseStartedNs = now;
        }
        const quint8 changes = row->item->setVisualRow(
            row->replacementVisualSnapshot);
        context->identityNotificationRows += bool(
            changes & BrickVisualRow::IdentityChange);
        context->mediaNotificationRows += bool(
            changes & BrickVisualRow::MediaChange);
        context->stateNotificationRows += bool(
            changes & BrickVisualRow::StateChange);
        context->styleNotificationRows += bool(
            changes & BrickVisualRow::StyleChange);
        if (context->trace) {
            const qint64 now = context->timer.nsecsElapsed();
            context->visualRowNotifyNs += now - row->phaseStartedNs;
            row->phaseStartedNs = now;
        }
        row->item->setProperty(DelegateVisualRevisionProperty,
                               QVariant::fromValue(
                                   row->brick->modelVisualRevision));
        ++context->snapshotCount;
    } else if (_visualSnapshotRole >= 0
               && row->item->property(DelegateVisualRevisionProperty)
                      .toULongLong() != row->brick->modelVisualRevision) {
        row->item->setProperty(DelegateVisualRevisionProperty,
                               QVariant::fromValue(
                                   row->brick->modelVisualRevision));
    }
    if (row->assignmentChanged) {
        row->item->notifyViewSourceIndexesChanged();
    }
    if (context->trace) {
        const qint64 now = context->timer.nsecsElapsed();
        context->delegateAssignmentNotifyNs += now - row->phaseStartedNs;
        context->visualSnapshotApplyNs += now - row->phaseStartedNs;
        row->phaseStartedNs = now;
    }
}

void MasonryLayout::applyDelegateRowLayout(
    DelegateRowCommit *row, bool animate, DelegateCommitContext *context)
{
    row->item->setRowColumn(row->brick->row, row->brick->column);
    if (_presentationMode == Grid) {
        auto &brick = *row->brick;
        const QString text = row->item->property("effectiveDisplayName").toString();
        const size_t nameHash = qHash(text);
        const qreal width = cellCaptionWidth(brick.geometry());
        if (brick.iconLabelPresentationMode != Grid
            || brick.iconLabelStyleRevision != _iconCaptionStyleRevision
            || brick.iconLabelNameHash != nameHash || brick.iconLabelWidth != width) {
            ZoinGallery::GalleryIconTextMeasurer measurer(_iconLabelFont, true);
            brick.iconLabelText = measurer.layout(text, width, 2).text;
            brick.iconLabelPresentationMode = Grid;
            brick.iconLabelStyleRevision = _iconCaptionStyleRevision;
            brick.iconLabelNameHash = nameHash;
            brick.iconLabelWidth = width;
            ++_iconCaptionMeasurementCount;
        }
        row->item->setIconLabelText(brick.iconLabelText);
    } else {
        row->item->setIconLabelText(row->brick->iconLabelText);
    }
    const QRectF geometry = ZoinGallery::PixelGrid::snapDeviceRect(
        row->brick->geometry(), devicePixelRatio());
    row->item->setPreviewRect(ZoinGallery::PixelGrid::snapDeviceRect(
        row->brick->previewGeometry, devicePixelRatio()).translated(
            -geometry.topLeft()));
    if (context->trace) {
        const qint64 now = context->timer.nsecsElapsed();
        context->delegateLayoutMetaNs += now - row->phaseStartedNs;
        row->phaseStartedNs = now;
    }

    const bool geometryDiffers = row->item->geometry() != geometry;
    if (!_animateResizing && geometryDiffers) {
        row->item->setGeometry(geometry, false, false);
    } else if (_animateResizing && (row->itemPopped || geometryDiffers)) {
        const bool animateGeometry = row->item->isVisible() && row->item->sceneVisible() && animate
            && row->item->geometry().isValid();
        row->item->setGeometry(geometry, animateGeometry, false);
    }
    if (context->trace) {
        const qint64 now = context->timer.nsecsElapsed();
        context->delegateGeometryNs += now - row->phaseStartedNs;
        row->phaseStartedNs = now;
    }

    // A reused viewport slot can retain a visual from the outgoing
    // presentation. Install the target row metadata and geometry before
    // changing presentationMode: the synchronous QML Loader then constructs
    // the new mode visual against its final dimensions and preview rectangle.
    // Switching mode first made every retained Grid/Icons delegate evaluate
    // once with stale Details geometry and immediately evaluate all of those
    // bindings again after setGeometry()/setPreviewRect().
    row->item->setPresentationMode(static_cast<int>(_presentationMode));
    row->item->setProperty("masonryGeometryReady", row->brick->masonryGeometryReady);
}

void MasonryLayout::finalizeDelegateRow(
    DelegateRowCommit *row, DelegateCommitContext *context)
{
    _activeBrickIndexes.insert(row->index);
    if (row->deferMissingFacade) {
        enqueueDeferredDelegateMaterialization(row->index);
    }
    context->itemsToHide.remove(row->item);
    setDelegateVisible(row->item, !keepsNearbyDelegates()
                         || _visibleIndexSet.contains(row->index));
    updateSceneBranchParent(row->item);
    if (context->trace) {
        context->delegateVisibilityNs += context->timer.nsecsElapsed()
            - row->phaseStartedNs;
    }
}

void MasonryLayout::setDelegateVisible(BrickItem *item, bool visible)
{
    const bool nativeCulling = _presentationMode == Masonry && !_containedPreview
        && item->nativeCullingAvailable();
    if (visible || !nativeCulling) item->setRetainSceneWhenHidden(false);
    if (visible && nativeCulling) {
        const auto *visual = qobject_cast<GalleryEntryVisual *>(item->visualModel());
        if (visual && visual->isVector() && !visual->vectorSource().isEmpty())
            item->setVectorSceneUseSerial(++_vectorSceneUseSerial);
    }
    item->setSceneVisible(!nativeCulling || visible);
    item->setVisible(nativeCulling || visible);
}

QSet<int> MasonryLayout::retainedVectorSceneIndexes() const
{
    QSet<int> retained;
    if (!_vectorSceneUseSerial || _presentationMode != Masonry
        || _containedPreview || !clip()) return retained;
    QList<QPair<quint64, int>> candidates;
    for (const int index : _activeBrickIndexes) {
        if (_visibleIndexSet.contains(index)) continue;
        const auto *brick = brickAt(index);
        auto *item = brick ? brick->item : nullptr;
        if (!item || item->viewIndex() < 0 || !item->nativeCullingAvailable()) continue;
        const auto *visual = qobject_cast<GalleryEntryVisual *>(item->visualModel());
        if (!visual || !visual->isVector() || visual->vectorSource().isEmpty()) continue;
        // Retention is safe only when the viewport's existing clip completely
        // hides the tile. Keep logical visibility and input disabled separately.
        if (boundingRect().intersects(item->mapRectToItem(this, item->boundingRect()))) continue;
        candidates.append({item->vectorSceneUseSerial(), index});
    }
    std::sort(candidates.begin(), candidates.end(), [](const auto &left, const auto &right) {
        return left.first > right.first;
    });
    for (qsizetype i = 0; i < qMin<qsizetype>(RetainedVectorSceneLimit, candidates.size()); ++i)
        retained.insert(candidates[i].second);
    return retained;
}

void MasonryLayout::updateRetainedVectorScenes()
{
    if (!_vectorSceneUseSerial) return;
    const QSet<int> retained = retainedVectorSceneIndexes();
    for (const int index : _activeBrickIndexes) {
        const auto *brick = brickAt(index);
        if (brick && brick->item)
            brick->item->setRetainSceneWhenHidden(retained.contains(index));
    }
}

void MasonryLayout::releaseUnclaimedDelegates(
    DelegateCommitContext *context)
{
    releaseResetSlotItems(keepsMasonryResetSpares());
    for (BrickItem *item : std::as_const(context->itemsToHide)) {
        item->setRetainSceneWhenHidden(false);
        if (_presentationMode == Masonry && !_containedPreview
            && item->nativeCullingAvailable())
            item->setSceneVisible(false);
        else
            item->setVisible(false);
        if (!_presentationModeCommitInProgress) {
            // Hidden Masonry spares keep coherent metadata until reassignment.
            // Publishing an empty row first needlessly rebuilds fallback
            // geometry, tint and source bindings on the scrolling frame.
            if (_presentationMode != Masonry || _containedPreview) {
                item->setVisualRow({});
                item->setVisualFacadeReady(false);
                item->setProperty("model", QVariant());
            }
            item->setViewSourceIndexes(-1, -1);
        }
    }
    trimFreeDelegatePool();
}

void MasonryLayout::traceDeferredDelegateCommit(
    const QList<int> &delegateIndexes,
    const DelegateCommitContext &context) const
{
    if (!context.trace) {
        return;
    }
    const qint64 completedNs = context.timer.nsecsElapsed();
    qInfo().nospace()
        << "F4_NAV_BENCHMARK_TRACE masonry.properties rows="
        << logicalBrickCount() << " delegates=" << delegateIndexes.size()
        << " deferred=1 viewportSetsNs=" << context.viewportSetsCompletedNs
        << " indexesNs="
        << (context.indexesCompletedNs - context.viewportSetsCompletedNs)
        << " totalNs=" << completedNs;
}

void MasonryLayout::traceDelegateCommit(
    const QList<int> &delegateIndexes,
    const DelegateCommitContext &context) const
{
    if (!context.trace) {
        return;
    }
    const qint64 completedNs = context.timer.nsecsElapsed();
    qInfo().nospace()
        << "F4_NAV_BENCHMARK_TRACE masonry.properties rows="
        << logicalBrickCount() << " delegates=" << delegateIndexes.size()
        << " popped=" << context.createdOrPoppedCount
        << " retainedSlots=" << context.retainedSlotCount
        << " retainedSpares=" << context.retainedSpareCount
        << " oldActive=" << context.oldActiveCount
        << " overlappingActive=" << context.overlappingActiveCount
        << " retainedActive=" << context.retainedActiveCount
        << " componentMismatches="
        << context.delegateComponentMismatchCount
        << " rebound=" << context.reboundCount
        << " snapshots=" << context.snapshotCount
        << " snapshotOnly=" << (_visualSnapshotRefresh ? 1 : 0)
        << " viewportSetsNs=" << context.viewportSetsCompletedNs
        << " indexesNs="
        << (context.indexesCompletedNs - context.viewportSetsCompletedNs)
        << " retiredNs="
        << (context.retiredCompletedNs - context.indexesCompletedNs)
        << " delegatesNs="
        << (context.delegatesCompletedNs - context.retiredCompletedNs)
        << " totalNs=" << completedNs;
    ZoinGallery::MediaTimingTrace::event(
        QStringLiteral("qt.gallery.masonry.properties"), {
            {QStringLiteral("rows"), logicalBrickCount()},
            {QStringLiteral("delegates"), delegateIndexes.size()},
            {QStringLiteral("popped"), context.createdOrPoppedCount},
            {QStringLiteral("retainedSlots"), context.retainedSlotCount},
            {QStringLiteral("retainedSpares"), context.retainedSpareCount},
            {QStringLiteral("oldActive"), context.oldActiveCount},
            {QStringLiteral("overlappingActive"),
             context.overlappingActiveCount},
            {QStringLiteral("retainedActive"),
             context.retainedActiveCount},
            {QStringLiteral("componentMismatches"),
             context.delegateComponentMismatchCount},
            {QStringLiteral("snapshots"), context.snapshotCount},
            {QStringLiteral("viewportSetsNs"),
             context.viewportSetsCompletedNs},
            {QStringLiteral("indexesNs"),
             context.indexesCompletedNs - context.viewportSetsCompletedNs},
            {QStringLiteral("retiredNs"),
             context.retiredCompletedNs - context.indexesCompletedNs},
            {QStringLiteral("delegateAcquireNs"),
             context.delegateAcquireNs},
            {QStringLiteral("visualSnapshotNs"),
             context.visualSnapshotApplyNs},
            {QStringLiteral("visualSnapshotFetchNs"),
             context.visualSnapshotFetchNs},
            {QStringLiteral("visualRowNotifyNs"), context.visualRowNotifyNs},
            {QStringLiteral("identityNotificationRows"),
             context.identityNotificationRows},
            {QStringLiteral("mediaNotificationRows"),
             context.mediaNotificationRows},
            {QStringLiteral("stateNotificationRows"),
             context.stateNotificationRows},
            {QStringLiteral("styleNotificationRows"),
             context.styleNotificationRows},
            {QStringLiteral("delegateBindingNs"), context.delegateBindingNs},
            {QStringLiteral("delegateAssignmentNotifyNs"),
             context.delegateAssignmentNotifyNs},
            {QStringLiteral("delegateLayoutMetaNs"),
             context.delegateLayoutMetaNs},
            {QStringLiteral("delegateGeometryNs"),
             context.delegateGeometryNs},
            {QStringLiteral("delegateVisibilityNs"),
             context.delegateVisibilityNs},
            {QStringLiteral("durationNs"), completedNs},
        });
}

void MasonryLayout::updateProperties(bool animate)
{
    ++_delegateCommitRevision;
    DelegateCommitContext context;
    context.trace = qEnvironmentVariableIsSet("F4_NAV_BENCHMARK_TRACE");
    if (context.trace) {
        context.timer.start();
    }
    updateViewportIndexSets();
    context.viewportSetsCompletedNs = context.trace
        ? context.timer.nsecsElapsed() : 0;
    const QList<int> delegateIndexes = delegateIndexesForCurrentViewport();
    QSet<int> delegateIndexSet(delegateIndexes.cbegin(),
                                     delegateIndexes.cend());
    if (keepsNearbyDelegates()) {
        // Retain only already-prepared rows inside the existing metadata
        // look-ahead. Visible rows still commit synchronously below.
        const auto plan = viewportMaterializationPlan();
        const auto nearby = _presentationMode == Columns
            ? indexesForHorizontalRange(plan.metadataStart, plan.metadataEnd)
            : indexesForVerticalRange(plan.metadataStart, plan.metadataEnd);
        for (const int index : nearby) {
            if (_activeBrickIndexes.contains(index)) delegateIndexSet.insert(index);
        }
        for (const int index : edgeThumbnailIndexes()) {
            if (_activeBrickIndexes.contains(index)) delegateIndexSet.insert(index);
        }
        if (preparingViewportMotion() && _viewportMotionState->page) {
            for (const int index : motionLeadIndexes(plan))
                if (_activeBrickIndexes.contains(index)) delegateIndexSet.insert(index);
        }
    }
    context.indexesCompletedNs = context.trace
        ? context.timer.nsecsElapsed() : 0;
    if (_delegateRefreshPending) {
        traceDeferredDelegateCommit(delegateIndexes, context);
        return;
    }

    const quint64 layoutRevision = _layoutRevision;
    const bool reusePrepared = preparingViewportMotion()
        && !_visualSnapshotRefresh && !_presentationModeCommitInProgress
        && _delegateLayoutRevision == layoutRevision;
    retireInactiveDelegates(delegateIndexSet, &context);
    for (const int index : delegateIndexes) {
        if (reusePrepared && isMotionDelegatePrepared(index)) {
            auto *item = brickAt(index)->item;
            if (item->sceneVisible() != _visibleIndexSet.contains(index))
                setDelegateVisible(item, _visibleIndexSet.contains(index));
            continue;
        }
        DelegateRowCommit row;
        if (!prepareDelegateRow(index, &row, &context)) {
            continue;
        }
        row.item->beginVisualUpdate();
        const auto finishVisualUpdate = qScopeGuard([item = row.item] {
            item->endVisualUpdate();
        });
        // Hide the outgoing photo before activating a cached folder frame.
        // Both visual identity and square geometry are committed in this call;
        // QML must not lay out either presentation against the other's size.
        const bool folderRebind = _visualSnapshotRole >= 0
            && row.brick->modelIsFolder && !row.item->isFolder();
        if (folderRebind) row.item->setProperty("visualGeometryReady", false);
        bindDelegateRowIdentity(&row, &context);
        bindDelegateRowVisual(&row, &context);
        applyDelegateRowLayout(&row, animate, &context);
        if (folderRebind) row.item->setProperty("visualGeometryReady", true);
        finalizeDelegateRow(&row, &context);
    }
    context.delegatesCompletedNs = context.trace
        ? context.timer.nsecsElapsed() : 0;
    if (keepsNearbyDelegates()) {
        for (const int index : std::as_const(_activeBrickIndexes)) {
            if (_visibleIndexSet.contains(index)) continue;
            if (auto *brick = brickAt(index); brick && brick->item)
                setDelegateVisible(brick->item, false);
        }
    }
    releaseUnclaimedDelegates(&context);
    updateRetainedVectorScenes();
    _delegateLayoutRevision = layoutRevision;
    traceDelegateCommit(delegateIndexes, context);
    scheduleNearbyPreparation();
}

bool MasonryLayout::keepsNearbyDelegates() const
{
    return _presentationMode == Columns
        || (_presentationMode == Masonry && !_containedPreview);
}

bool MasonryLayout::isMotionDelegatePrepared(int index) const
{
    const auto *brick = brickAt(index);
    const auto *item = brick ? brick->item : nullptr;
    if (!item || item->viewIndex() != index || !item->visualFacadeReady()) return false;
    if (item->property(DelegateCatalogGenerationProperty).toULongLong()
        != _delegateCatalogGeneration) return false;
    if (_visualSnapshotRole >= 0 && item->entryId() != brick->modelIdentity)
        return false;
    // Thumbnail publication may advance the visual revision while the
    // delegate still has the correct identity and geometry. The facade
    // updates its preview independently of camera coverage.
    const qreal dpr = devicePixelRatio();
    const QRectF geometry = ZoinGallery::PixelGrid::snapDeviceRect(brick->geometry(), dpr);
    return item->geometry() == geometry
        && item->previewRect() == ZoinGallery::PixelGrid::snapDeviceRect(
               brick->previewGeometry, dpr).translated(-geometry.topLeft());
}

bool MasonryLayout::commitPreparedMotionViewport(const QSet<int> &previousVisible)
{
    const auto entering = _visibleIndexSet - previousVisible;
    for (const int index : entering)
        if (!isMotionDelegatePrepared(index)) return false;
    for (const int index : previousVisible - _visibleIndexSet) {
        if (const auto *brick = brickAt(index); brick && brick->item)
            setDelegateVisible(brick->item, false);
    }
    for (const int index : entering)
        setDelegateVisible(brickAt(index)->item, true);
    if (previousVisible != _visibleIndexSet)
        updateRetainedVectorScenes();
    scheduleNearbyPreparation();
    return true;
}

void MasonryLayout::scheduleNearbyPreparation()
{
    if (_nearbyPreparationScheduled || !keepsNearbyDelegates()
        || !isVisible() || !_model) return;
    _nearbyPreparationScheduled = true;
    QTimer::singleShot(preparingViewportMotion() && _viewportMotionState->page ? 0 : 8, this, [this] {
        _nearbyPreparationScheduled = false;
        prepareNearbyDelegates();
    });
}

void MasonryLayout::prepareNearbyDelegates()
{
    if (!keepsNearbyDelegates() || !isVisible() || !_model
        || _layoutUpdateDepth || _delegateRefreshPending) return;
    const auto plan = viewportMaterializationPlan();
    const auto nearby = _presentationMode == Columns
        ? indexesForHorizontalRange(plan.metadataStart, plan.metadataEnd)
        : indexesForVerticalRange(plan.metadataStart, plan.metadataEnd);
    const auto edge = edgeThumbnailIndexes();
    const bool motion = preparingViewportMotion() && _viewportMotionState->page;
    const auto lead = motion ? motionLeadIndexes(plan) : QList<int>{};
    const auto &prepare = motion ? lead : edge.isEmpty() ? nearby : edge;
    DelegateCommitContext context;
    QSet<int> retained(nearby.cbegin(), nearby.cend());
    for (int index : edge) retained.insert(index);
    if (motion) {
        for (int index : lead) retained.insert(index);
    }
    retireInactiveDelegates(retained, &context);
    releaseUnclaimedDelegates(&context);
    QElapsedTimer budget;
    budget.start();
    const qint64 budgetNs = motion ? 3000000 : 2000000;
    int prepared = 0;
    bool pending = false;
    int obsolete = 0;
    for (const int index : std::as_const(_activeBrickIndexes))
        obsolete += !retained.contains(index)
            && !context.retainedVectorIndexes.contains(index);
    pending = obsolete > 64;
    for (const int index : prepare) {
        if (_activeBrickIndexes.contains(index)) {
            const auto *brick = brickAt(index);
            if (!motion || isMotionDelegatePrepared(index)
                || (_delegateMaterializationPending && brick && !brick->image))
                continue;
        }
        if (prepared >= (motion ? 12 : 2) || budget.nsecsElapsed() >= budgetNs) {
            pending = true;
            break;
        }
        DelegateRowCommit row;
        if (!prepareDelegateRow(index, &row, &context)) continue;
        row.item->beginVisualUpdate();
        const auto finishVisualUpdate = qScopeGuard([item = row.item] {
            item->endVisualUpdate();
        });
        bindDelegateRowIdentity(&row, &context);
        bindDelegateRowVisual(&row, &context);
        applyDelegateRowLayout(&row, false, &context);
        finalizeDelegateRow(&row, &context);
        if (!motion) setDelegateVisible(row.item, false);
        ++prepared;
    }
    updateRetainedVectorScenes();
    if (pending) scheduleNearbyPreparation();
}
