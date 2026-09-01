#include "MasonryLayout.h"

#include "GalleryPixelGrid.h"

#include <ZoinGallery/MediaTimingTrace.h>

#include <QElapsedTimer>

#include <utility>

namespace
{
constexpr char DelegateCatalogGenerationProperty[] =
    "_zoinGalleryCatalogGeneration";
constexpr char DelegateVisualRevisionProperty[] =
    "_zoinGalleryVisualRevision";
}

struct MasonryLayout::DelegateCommitContext
{
    bool trace = false;
    QElapsedTimer timer;
    QSet<BrickItem *> itemsToHide;
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

void MasonryLayout::retireInactiveDelegates(
    const QSet<int> &retainedIndexes, DelegateCommitContext *context)
{
    const QSet<int> oldActiveIndexes = _activeBrickIndexes;
    context->oldActiveCount = oldActiveIndexes.size();
    const quintptr targetDelegate = reinterpret_cast<quintptr>(
        delegateComponent(_presentationMode));
    for (const int index : oldActiveIndexes) {
        const MasonryBrick *activeBrick = brickAt(index);
        const bool componentMatches = activeBrick && activeBrick->item
            && activeBrick->item->property(
                   "_zoinGalleryDelegateComponent").toULongLong()
                == targetDelegate;
        if (retainedIndexes.contains(index)) {
            ++context->overlappingActiveCount;
            if (componentMatches) {
                ++context->retainedActiveCount;
            } else {
                ++context->delegateComponentMismatchCount;
            }
        }
        if ((retainedIndexes.contains(index) && componentMatches)
            || index < 0 || index >= logicalBrickCount()) {
            continue;
        }
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
        ? brick.image : materializeImageForIndex(index);

    if (!brick.item) {
        const int initialSourceIndex = row->image
            ? row->image->index() : brick.modelSourceIndex;
        QVariantMap initialProperties{
            {QStringLiteral("viewIndex"), index},
            {QStringLiteral("sourceIndex"), initialSourceIndex},
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
            initialProperties.insert(
                QStringLiteral("model"),
                QVariant::fromValue(row->image));
        }
        if (_visualSnapshotRole >= 0) {
            row->replacementVisualSnapshot = visualSnapshotForIndex(index);
            row->replacementVisualSnapshotFetched = true;
            initialProperties.insert(
                QStringLiteral("visualRow"),
                row->replacementVisualSnapshot);
        }
        BrickItem *retained = _resetSlotItems.take(index);
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
            brick.item = popBrickItem(index, initialProperties);
        }
    }
    row->item = brick.item;
    if (!row->item) {
        return false;
    }
    const bool initialStateInstalled = row->itemPopped
        && row->item->viewIndex() == index
        && (_visualSnapshotRole < 0
            || row->item->visualRow()
                == row->replacementVisualSnapshot);
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
    row->item->setIconLabelText(row->brick->iconLabelText);
    row->item->setPreviewRect(row->brick->previewGeometry.translated(
        -row->brick->x, -row->brick->y));
    if (context->trace) {
        const qint64 now = context->timer.nsecsElapsed();
        context->delegateLayoutMetaNs += now - row->phaseStartedNs;
        row->phaseStartedNs = now;
    }

    const bool preserveFractional = _presentationMode == Details
        || _presentationMode == Columns;
    const bool geometryDiffers = preserveFractional
        ? row->item->geometry() != row->brick->geometry()
        : ZoinGallery::PixelGrid::snapLogicalRect(row->item->geometry())
            != ZoinGallery::PixelGrid::snapLogicalRect(
                row->brick->geometry());
    if (!_animateResizing && geometryDiffers) {
        row->item->setGeometry(row->brick->geometry(), false,
                               !preserveFractional);
    } else if (_animateResizing && (row->itemPopped || geometryDiffers)) {
        const bool animateGeometry = row->item->isVisible() && animate
            && row->item->geometry().isValid();
        row->item->setGeometry(row->brick->geometry(), animateGeometry,
                               !preserveFractional);
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
}

void MasonryLayout::finalizeDelegateRow(
    DelegateRowCommit *row, DelegateCommitContext *context)
{
    _activeBrickIndexes.insert(row->index);
    if (row->deferMissingFacade) {
        enqueueDeferredDelegateMaterialization(row->index);
    }
    if (context->itemsToHide.contains(row->item)) {
        context->itemsToHide.remove(row->item);
    } else if (!row->item->isVisible()) {
        row->item->setVisible(true);
    }
    if (context->trace) {
        context->delegateVisibilityNs += context->timer.nsecsElapsed()
            - row->phaseStartedNs;
    }
}

void MasonryLayout::releaseUnclaimedDelegates(
    DelegateCommitContext *context)
{
    releaseResetSlotItems();
    for (BrickItem *item : std::as_const(context->itemsToHide)) {
        item->setVisible(false);
        if (!_presentationModeCommitInProgress) {
            item->setVisualRow({});
            item->setVisualFacadeReady(false);
            item->setProperty("model", QVariant());
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
    const QSet<int> delegateIndexSet(delegateIndexes.cbegin(),
                                     delegateIndexes.cend());
    context.indexesCompletedNs = context.trace
        ? context.timer.nsecsElapsed() : 0;
    if (_delegateRefreshPending) {
        traceDeferredDelegateCommit(delegateIndexes, context);
        return;
    }

    retireInactiveDelegates(delegateIndexSet, &context);
    for (const int index : delegateIndexes) {
        DelegateRowCommit row;
        if (!prepareDelegateRow(index, &row, &context)) {
            continue;
        }
        bindDelegateRowIdentity(&row, &context);
        bindDelegateRowVisual(&row, &context);
        applyDelegateRowLayout(&row, animate, &context);
        finalizeDelegateRow(&row, &context);
    }
    context.delegatesCompletedNs = context.trace
        ? context.timer.nsecsElapsed() : 0;
    releaseUnclaimedDelegates(&context);
    traceDelegateCommit(delegateIndexes, context);
}
