#include "GalleryGroupIndex.h"

#include <QHash>
#include <QMetaType>

#include <algorithm>

namespace ZoinGallery {

void GalleryGroupIndex::PrefixTree::reset(int count)
{
    _tree.fill(0, qMax(0, count) + 1);
}

void GalleryGroupIndex::PrefixTree::add(int index, int delta)
{
    if (index < 0 || index + 1 >= _tree.size()) {
        return;
    }
    for (int cursor = index + 1; cursor < _tree.size();
         cursor += cursor & -cursor) {
        _tree[cursor] += delta;
    }
}

int GalleryGroupIndex::PrefixTree::sumBefore(int index) const
{
    int sum = 0;
    for (int cursor = qBound(0, index, _tree.size() - 1);
         cursor > 0; cursor -= cursor & -cursor) {
        sum += _tree.at(cursor);
    }
    return sum;
}

int GalleryGroupIndex::PrefixTree::total() const
{
    return sumBefore(_tree.size() - 1);
}

int GalleryGroupIndex::PrefixTree::lowerBound(int target) const
{
    const int count = _tree.size() - 1;
    if (count <= 0 || target <= 0 || target > total()) {
        return -1;
    }
    int cursor = 0;
    int step = 1;
    while ((step << 1) <= count) {
        step <<= 1;
    }
    for (; step > 0; step >>= 1) {
        const int next = cursor + step;
        if (next <= count && _tree.at(next) < target) {
            cursor = next;
            target -= _tree.at(next);
        }
    }
    return qMin(cursor, count - 1);
}

bool GalleryGroupIndex::setDescriptors(const QVariantList &groups,
                                       int catalogCount)
{
    QVector<GalleryGroupDescriptor> descriptors;
    descriptors.reserve(groups.size());
    for (const QVariant &value : groups) {
        if (value.metaType().id() != QMetaType::QVariantMap) {
            clear();
            return false;
        }
        const QVariantMap map = value.toMap();
        GalleryGroupDescriptor descriptor;
        descriptor.key = map.value(QStringLiteral("key")).toString();
        descriptor.title = map.value(QStringLiteral("title")).toString();
        bool startOK = false;
        bool countOK = false;
        descriptor.startIndex = map.value(QStringLiteral("startIndex"))
                                    .toInt(&startOK);
        descriptor.count = map.value(QStringLiteral("count"))
                               .toInt(&countOK);
        if (!startOK || !countOK) {
            clear();
            return false;
        }
        descriptors.append(std::move(descriptor));
    }
    return setDescriptors(descriptors, catalogCount);
}

bool GalleryGroupIndex::setDescriptors(
    const QVector<GalleryGroupDescriptor> &groups, int catalogCount)
{
    int inferredCount = catalogCount;
    if (inferredCount < 0) {
        inferredCount = 0;
        for (const GalleryGroupDescriptor &group : groups) {
            inferredCount = qMax(inferredCount, group.startIndex + group.count);
        }
    }
    if (inferredCount < 0) {
        clear();
        return false;
    }

    QSet<QString> keys;
    int previousEnd = 0;
    for (const GalleryGroupDescriptor &group : groups) {
        const bool validKey = !group.key.isEmpty() && !keys.contains(group.key);
        const bool validRange = group.startIndex >= previousEnd
            && group.count > 0
            && group.startIndex <= inferredCount
            && group.count <= inferredCount - group.startIndex;
        if (!validKey || !validRange) {
            clear();
            return false;
        }
        keys.insert(group.key);
        previousEnd = group.startIndex + group.count;
    }
    if (!groups.isEmpty()) {
        // The only permitted uncovered row is the leading parent entry. A
        // partial response is not a catalog: rejecting it keeps a stale or
        // truncated page from silently hiding files in the renderer.
        if (groups.first().startIndex > 1 || previousEnd != inferredCount) {
            clear();
            return false;
        }
        for (int i = 1; i < groups.size(); ++i) {
            if (groups.at(i - 1).startIndex + groups.at(i - 1).count
                != groups.at(i).startIndex) {
                clear();
                return false;
            }
        }
    }

    _groups = groups;
    _catalogCount = inferredCount;
    _starts.clear();
    _groupIndexByKey.clear();
    _starts.reserve(_groups.size());
    _groupIndexByKey.reserve(_groups.size());
    for (int index = 0; index < _groups.size(); ++index) {
        const GalleryGroupDescriptor &group = _groups.at(index);
        _starts.append(group.startIndex);
        _groupIndexByKey.insert(group.key, index);
    }
    _collapsedKeys.intersect(keys);
    rebuildPrefixes();
    if (_sectionOffsets.size() != _groups.size()
        || _sectionExtents.size() != _groups.size()) {
        _sectionOffsets.clear();
        _sectionExtents.clear();
    }
    return true;
}

void GalleryGroupIndex::clear()
{
    _groups.clear();
    _starts.clear();
    _visibleCounts.reset(0);
    _hiddenCounts.reset(0);
    _catalogCount = 0;
    _visibleTotal = 0;
    _leadingCount = 0;
    _trailingCount = 0;
    _groupIndexByKey.clear();
    _collapsedKeys.clear();
    _sectionOffsets.clear();
    _sectionExtents.clear();
    _headerExtent = 0;
}

bool GalleryGroupIndex::active() const
{
    return !_groups.isEmpty();
}

int GalleryGroupIndex::groupCount() const
{
    return _groups.size();
}

int GalleryGroupIndex::catalogCount() const
{
    return _catalogCount;
}

const GalleryGroupDescriptor *GalleryGroupIndex::descriptor(int group) const
{
    return group >= 0 && group < _groups.size() ? &_groups.at(group) : nullptr;
}

int GalleryGroupIndex::groupForSourceIndex(int sourceIndex) const
{
    if (sourceIndex < 0 || sourceIndex >= _catalogCount || _starts.isEmpty()) {
        return -1;
    }
    const auto it = std::upper_bound(_starts.cbegin(), _starts.cend(),
                                     sourceIndex);
    const int group = int(it - _starts.cbegin()) - 1;
    if (group < 0) {
        return -1;
    }
    const GalleryGroupDescriptor &candidate = _groups.at(group);
    return sourceIndex < candidate.startIndex + candidate.count ? group : -1;
}

int GalleryGroupIndex::localIndexForSourceIndex(int sourceIndex) const
{
    const int group = groupForSourceIndex(sourceIndex);
    if (group < 0) {
        return -1;
    }
    return sourceIndex - _groups.at(group).startIndex;
}

int GalleryGroupIndex::visibleOrdinalForSourceIndex(int sourceIndex) const
{
    if (sourceIndex < 0 || sourceIndex >= _catalogCount) {
        return -1;
    }
    const int group = groupForSourceIndex(sourceIndex);
    if (group >= 0) {
        if (isCollapsed(group)) {
            return -1;
        }
        return sourceIndex - _hiddenCounts.sumBefore(group);
    }
    const int before = int(std::upper_bound(
        _starts.cbegin(), _starts.cend(), sourceIndex) - _starts.cbegin());
    return sourceIndex - _hiddenCounts.sumBefore(before);
}

int GalleryGroupIndex::sourceIndexForVisibleOrdinal(int visibleOrdinal) const
{
    if (visibleOrdinal < 0 || visibleOrdinal >= visibleFileCount()) {
        return -1;
    }
    if (_groups.isEmpty()) {
        return visibleOrdinal < _catalogCount ? visibleOrdinal : -1;
    }

    if (visibleOrdinal < _leadingCount) {
        return visibleOrdinal;
    }
    const int withinGroups = visibleOrdinal - _leadingCount;
    const int group = _visibleCounts.lowerBound(withinGroups + 1);
    if (group >= 0) {
        return _groups.at(group).startIndex + withinGroups
            - _visibleCounts.sumBefore(group);
    }
    const int trailing = withinGroups - _visibleCounts.total();
    const int sourceIndex = _catalogCount - _trailingCount + trailing;
    return sourceIndex < _catalogCount ? sourceIndex : -1;
}

QVector<int> GalleryGroupIndex::visibleSourceIndexesInRange(
    int first, int last) const
{
    QVector<int> result;
    if (_catalogCount <= 0) {
        return result;
    }
    first = qBound(0, first, _catalogCount - 1);
    last = qBound(0, last, _catalogCount - 1);
    if (first > last) {
        std::swap(first, last);
    }
    if (_groups.isEmpty()) {
        result.reserve(last - first + 1);
        for (int index = first; index <= last; ++index) {
            result.append(index);
        }
        return result;
    }

    const int firstGroupStart = _groups.first().startIndex;
    if (first < firstGroupStart) {
        const int end = qMin(last, firstGroupStart - 1);
        result.reserve(end - first + 1);
        for (int index = first; index <= end; ++index) {
            result.append(index);
        }
    }

    int group = int(std::upper_bound(_starts.cbegin(), _starts.cend(), first)
                    - _starts.cbegin()) - 1;
    if (group < 0) {
        group = 0;
    }
    while (group < _groups.size()) {
        const GalleryGroupDescriptor &descriptor = _groups.at(group);
        if (descriptor.startIndex > last) {
            break;
        }
        const int begin = qMax(first, descriptor.startIndex);
        const int end = qMin(last, descriptor.startIndex + descriptor.count - 1);
        if (begin <= end && !isCollapsed(group)) {
            result.reserve(result.size() + end - begin + 1);
            for (int index = begin; index <= end; ++index) {
                result.append(index);
            }
        }
        ++group;
    }

    const int trailingStart = _groups.last().startIndex
        + _groups.last().count;
    if (last >= trailingStart) {
        const int begin = qMax(first, trailingStart);
        result.reserve(result.size() + last - begin + 1);
        for (int index = begin; index <= last; ++index) {
            result.append(index);
        }
    }
    return result;
}

int GalleryGroupIndex::nearestVisibleSourceIndex(
    int sourceIndex, bool forward) const
{
    if (sourceIndex < 0) {
        return forward ? sourceIndexForVisibleOrdinal(0) : -1;
    }
    if (sourceIndex >= _catalogCount) {
        return forward ? -1 : sourceIndexForVisibleOrdinal(
            _visibleTotal - 1);
    }
    if (_catalogCount <= 0) {
        return -1;
    }
    const int candidate = sourceIndex + (forward ? 1 : -1);
    if (candidate < 0 || candidate >= _catalogCount) {
        return -1;
    }
    if (!active()) {
        return candidate;
    }
    if (visibleOrdinalForSourceIndex(candidate) >= 0) {
        return candidate;
    }

    const int group = groupForSourceIndex(candidate);
    if (group < 0) {
        return -1;
    }
    const int ordinal = forward
        ? visibleFileCountBeforeGroup(group)
            + (isCollapsed(group) ? 0 : _groups.at(group).count)
        : visibleFileCountBeforeGroup(group) - 1;
    return sourceIndexForVisibleOrdinal(ordinal);
}

int GalleryGroupIndex::visibleFileCountBeforeGroup(int group) const
{
    if (group <= 0) {
        return group == 0 && !_groups.isEmpty() ? _leadingCount : 0;
    }
    if (group >= _groups.size()) {
        return visibleFileCount();
    }
    return _leadingCount + _visibleCounts.sumBefore(group);
}

int GalleryGroupIndex::visibleFileCount() const
{
    return _visibleTotal;
}

bool GalleryGroupIndex::isCollapsed(int group) const
{
    const GalleryGroupDescriptor *item = descriptor(group);
    return item && _collapsedKeys.contains(item->key);
}

bool GalleryGroupIndex::isCollapsed(const QString &key) const
{
    return _collapsedKeys.contains(key);
}

bool GalleryGroupIndex::setCollapsed(int group, bool collapsed)
{
    const GalleryGroupDescriptor *item = descriptor(group);
    return item && setCollapsed(item->key, collapsed);
}

bool GalleryGroupIndex::setCollapsed(const QString &key, bool collapsed)
{
    const int group = _groupIndexByKey.value(key, -1);
    if (group < 0) {
        return false;
    }
    if (!isCollapsed(key) && !collapsed) {
        return false;
    }
    if (isCollapsed(key) == collapsed) {
        return false;
    }
    if (collapsed) {
        _collapsedKeys.insert(key);
    } else {
        _collapsedKeys.remove(key);
    }
    const int count = _groups.at(group).count;
    _visibleCounts.add(group, collapsed ? -count : count);
    _hiddenCounts.add(group, collapsed ? count : -count);
    _visibleTotal += collapsed ? -count : count;
    return true;
}

bool GalleryGroupIndex::toggleCollapsed(const QString &key)
{
    return setCollapsed(key, !isCollapsed(key));
}

void GalleryGroupIndex::setCollapsedKeys(const QSet<QString> &keys)
{
    _collapsedKeys = keys;
    QSet<QString> validKeys;
    for (const GalleryGroupDescriptor &group : std::as_const(_groups)) {
        validKeys.insert(group.key);
    }
    _collapsedKeys.intersect(validKeys);
    rebuildPrefixes();
}

QSet<QString> GalleryGroupIndex::collapsedKeys() const
{
    return _collapsedKeys;
}

void GalleryGroupIndex::setSectionGeometry(const QVector<qreal> &offsets,
                                           const QVector<qreal> &extents,
                                           qreal headerExtent)
{
    if (offsets.size() != _groups.size()
        || extents.size() != _groups.size()) {
        _sectionOffsets.clear();
        _sectionExtents.clear();
        _headerExtent = 0;
        return;
    }
    _sectionOffsets = offsets;
    _sectionExtents = extents;
    _headerExtent = qMax<qreal>(0, headerExtent);
}

QVariantList GalleryGroupIndex::headersForRange(qreal start, qreal end,
                                                qreal overscan) const
{
    QVariantList result;
    if (_sectionOffsets.size() != _groups.size()) {
        return result;
    }
    if (end < start) {
        std::swap(start, end);
    }
    start -= qMax<qreal>(0, overscan);
    end += qMax<qreal>(0, overscan);
    int first = 0;
    int last = _groups.size() - 1;
    while (first <= last && _sectionOffsets.at(first)
               + _sectionExtents.at(first) < start) {
        const int middle = first + (last - first) / 2;
        if (_sectionOffsets.at(middle) + _sectionExtents.at(middle) < start) {
            first = middle + 1;
        }
        else {
            last = middle - 1;
        }
    }
    const int firstIntersecting = first;
    if (firstIntersecting >= _groups.size()) {
        return result;
    }
    last = _groups.size() - 1;
    while (first <= last) {
        const int middle = first + (last - first) / 2;
        if (_sectionOffsets.at(middle) <= end) {
            first = middle + 1;
        }
        else {
            last = middle - 1;
        }
    }
    const int lastIntersecting = last;
    for (int group = firstIntersecting; group <= lastIntersecting; ++group) {
        const qreal offset = _sectionOffsets.at(group);
        const qreal sectionEnd = offset + _sectionExtents.at(group);
        if (sectionEnd < start || offset > end) {
            continue;
        }
        const GalleryGroupDescriptor &descriptor = _groups.at(group);
        result.append(QVariantMap{
            {QStringLiteral("key"), descriptor.key},
            {QStringLiteral("title"), descriptor.title},
            {QStringLiteral("count"), descriptor.count},
            {QStringLiteral("collapsed"), isCollapsed(group)},
            {QStringLiteral("groupIndex"), group},
            {QStringLiteral("offset"), offset},
            {QStringLiteral("extent"), _sectionExtents.at(group)},
            {QStringLiteral("headerExtent"), _headerExtent},
        });
    }
    return result;
}

void GalleryGroupIndex::rebuildPrefixes()
{
    _visibleCounts.reset(_groups.size());
    _hiddenCounts.reset(_groups.size());
    _leadingCount = _groups.isEmpty() ? 0 : _groups.first().startIndex;
    const int lastEnd = _groups.isEmpty()
        ? 0
        : _groups.last().startIndex + _groups.last().count;
    _trailingCount = qMax(0, _catalogCount - lastEnd);
    _visibleTotal = _catalogCount;
    for (int group = 0; group < _groups.size(); ++group) {
        const GalleryGroupDescriptor &descriptor = _groups.at(group);
        if (isCollapsed(group)) {
            _hiddenCounts.add(group, descriptor.count);
            _visibleTotal -= descriptor.count;
        }
        else {
            _visibleCounts.add(group, descriptor.count);
        }
    }
}

} // namespace ZoinGallery
