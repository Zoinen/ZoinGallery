#include "PersistentSelectionCache.h"

#include <QDataStream>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QMap>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>

#include <algorithm>

namespace {
constexpr quint32 SelectionDbMagic = 0x5a47534c; // "ZGSL"
constexpr quint32 SelectionDbVersionV1 = 1;
constexpr quint32 SelectionDbVersionV2 = 2;
constexpr quint32 SelectionDbVersionV3 = 3;
constexpr quint32 SelectionOrderDbMagic = 0x5a47534f; // "ZGSO"
constexpr quint32 SelectionOrderDbVersionV1 = 1;
constexpr quint32 SelectionOrderDbVersionV2 = 2;

const QString DefaultGroupId = QStringLiteral("selection-default");
constexpr qsizetype SelectionHistoryLimit = 256;

const QStringList GroupPalette = {
    QStringLiteral("#FFD43B"),
    QStringLiteral("#40C057"),
    QStringLiteral("#15AABF"),
    QStringLiteral("#228BE6"),
    QStringLiteral("#7950F2"),
    QStringLiteral("#D6336C"),
    QStringLiteral("#FA5252"),
    QStringLiteral("#FD7E14"),
};

const QStringList GroupColorNames = {
    QStringLiteral("Yellow"),
    QStringLiteral("Green"),
    QStringLiteral("Cyan"),
    QStringLiteral("Blue"),
    QStringLiteral("Purple"),
    QStringLiteral("Pink"),
    QStringLiteral("Red"),
    QStringLiteral("Orange"),
};

QString groupNameForColor(const QString &color) {
    for (int i = 0; i < GroupPalette.size(); ++i) {
        if (QString::compare(GroupPalette[i], color, Qt::CaseInsensitive) == 0) {
            return GroupColorNames[i];
        }
    }
    return QString();
}

bool isLegacyGeneratedGroupName(const QString &name) {
    constexpr qsizetype PrefixLength = 10;
    if (!name.startsWith(QStringLiteral("Selection "), Qt::CaseInsensitive)) {
        return false;
    }
    bool isNumber = false;
    name.mid(PrefixLength).toInt(&isNumber);
    return isNumber;
}

QString dataFilePath(const QString &fileName) {
    const QString testDataPath =
        qEnvironmentVariable("ZOIN_SELECTION_DATA_DIR");
    if (!testDataPath.isEmpty()) {
        QDir testDir(testDataPath);
        testDir.mkpath(".");
        return testDir.filePath(fileName);
    }
    const QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir dir(dataPath);
    dir.mkpath(".");
    return dir.filePath(fileName);
}

QString selectionDbV1Path() {
    return dataFilePath(QStringLiteral("selection_v1.db"));
}

QString selectionDbV2Path() {
    return dataFilePath(QStringLiteral("selection_v2.db"));
}

QString selectionDbV3Path() {
    return dataFilePath(QStringLiteral("selection_v3.db"));
}

QString selectionOrderDbV1Path() {
    return dataFilePath(QStringLiteral("selection_order_v1.db"));
}

QString selectionOrderDbV2Path() {
    return dataFilePath(QStringLiteral("selection_order_v2.db"));
}

struct HistoryEntryV1 {
    QString description;
    QDateTime timestamp;
    QSet<QString> selectedNames;
};

struct ContainerStateV1 {
    QSet<QString> selectedNames;
    QList<HistoryEntryV1> history;
    int historyIndex = -1;
};

struct HistoryEntryV2 {
    QString description;
    QDateTime timestamp;
    QHash<QString, QString> selectedGroups;
};

struct ContainerStateV2 {
    QHash<QString, QString> selectedGroups;
    QList<HistoryEntryV2> history;
    int historyIndex = -1;
};

QDataStream& operator>>(QDataStream& in, HistoryEntryV1& obj) {
    QStringList selectedNames;
    in >> obj.description >> obj.timestamp >> selectedNames;
    obj.selectedNames = QSet<QString>(selectedNames.begin(), selectedNames.end());
    return in;
}

QDataStream& operator>>(QDataStream& in, ContainerStateV1& obj) {
    QStringList selectedNames;
    in >> selectedNames >> obj.history >> obj.historyIndex;
    obj.selectedNames = QSet<QString>(selectedNames.begin(), selectedNames.end());
    if (obj.historyIndex >= obj.history.size()) {
        obj.historyIndex = obj.history.size() - 1;
    }
    if (obj.historyIndex < -1) {
        obj.historyIndex = -1;
    }
    return in;
}

QDataStream& operator>>(QDataStream& in, HistoryEntryV2& obj) {
    QMap<QString, QString> selectedGroups;
    in >> obj.description >> obj.timestamp >> selectedGroups;
    obj.selectedGroups.clear();
    for (auto it = selectedGroups.constBegin();
         it != selectedGroups.constEnd(); ++it) {
        obj.selectedGroups.insert(it.key(), it.value());
    }
    return in;
}

QDataStream& operator>>(QDataStream& in, ContainerStateV2& obj) {
    QMap<QString, QString> selectedGroups;
    in >> selectedGroups >> obj.history >> obj.historyIndex;
    obj.selectedGroups.clear();
    for (auto it = selectedGroups.constBegin();
         it != selectedGroups.constEnd(); ++it) {
        obj.selectedGroups.insert(it.key(), it.value());
    }
    if (obj.historyIndex >= obj.history.size()) {
        obj.historyIndex = obj.history.size() - 1;
    }
    if (obj.historyIndex < -1) {
        obj.historyIndex = -1;
    }
    return in;
}

void applyGroupChanges(QHash<QString, QString> &selection,
                       const QHash<QString, QString> &changes) {
    for (auto it = changes.constBegin(); it != changes.constEnd(); ++it) {
        if (it.value().isEmpty()) {
            selection.remove(it.key());
        }
        else {
            selection.insert(it.key(), it.value());
        }
    }
}

PersistentSelectionCache::HistoryEntry deltaHistoryEntry(
    const QString &description, const QDateTime &timestamp,
    const QHash<QString, QString> &before,
    const QHash<QString, QString> &after) {
    PersistentSelectionCache::HistoryEntry result{
        .description = description,
        .timestamp = timestamp,
        .selectedCount = static_cast<int>(after.size()),
    };
    QSet<QString> names(before.keyBegin(), before.keyEnd());
    names.unite(QSet<QString>(after.keyBegin(), after.keyEnd()));
    for (const QString &name : std::as_const(names)) {
        const QString beforeGroup = before.value(name);
        const QString afterGroup = after.value(name);
        if (beforeGroup != afterGroup) {
            result.beforeGroups.insert(name, beforeGroup);
            result.afterGroups.insert(name, afterGroup);
        }
    }
    return result;
}

void compactHistory(PersistentSelectionCache::ContainerState &state) {
    if (state.history.size() <= SelectionHistoryLimit) {
        return;
    }

    // Keep a synthetic base row plus the most recent actions. Advancing the
    // base through discarded deltas preserves exact undo/redo semantics.
    const qsizetype removeCount =
        state.history.size() - SelectionHistoryLimit;
    if (state.historyIndex < removeCount) {
        // Preserve the current cursor if the user shut down while far back in
        // history. Dropping only distant redo entries needs no base rewrite.
        state.history.resize(SelectionHistoryLimit);
        return;
    }
    for (qsizetype i = 1; i <= removeCount; ++i) {
        applyGroupChanges(state.historyBaseGroups,
                          state.history[i].afterGroups);
    }
    state.history.remove(1, removeCount);
    state.historyIndex -= removeCount;
    state.history[0] = PersistentSelectionCache::HistoryEntry{
        .description = QStringLiteral("Earlier state"),
        .timestamp = state.history[1].timestamp,
        .selectedCount =
            static_cast<int>(state.historyBaseGroups.size()),
    };
}

void recomputeHistoryCounts(
    PersistentSelectionCache::ContainerState &state) {
    if (state.history.isEmpty()) {
        state.historyIndex = -1;
        return;
    }
    QHash<QString, QString> selection = state.historyBaseGroups;
    state.history[0].selectedCount = selection.size();
    for (qsizetype i = 1; i < state.history.size(); ++i) {
        applyGroupChanges(selection, state.history[i].afterGroups);
        state.history[i].selectedCount = selection.size();
    }
}

void replaceGroupReference(
    PersistentSelectionCache::ContainerState &state,
    const QString &groupId, const QString &replacementGroupId) {
    auto replaceCurrentValue = [&](QHash<QString, QString> &groups) {
        for (auto it = groups.begin(); it != groups.end();) {
            if (it.value() != groupId) {
                ++it;
            }
            else if (replacementGroupId.isEmpty()) {
                it = groups.erase(it);
            }
            else {
                it.value() = replacementGroupId;
                ++it;
            }
        }
    };
    replaceCurrentValue(state.selectedGroups);
    replaceCurrentValue(state.historyBaseGroups);

    for (auto &entry : state.history) {
        for (auto it = entry.beforeGroups.begin();
             it != entry.beforeGroups.end(); ++it) {
            if (it.value() == groupId) {
                it.value() = replacementGroupId;
            }
        }
        for (auto it = entry.afterGroups.begin();
             it != entry.afterGroups.end(); ++it) {
            if (it.value() == groupId) {
                it.value() = replacementGroupId;
            }
        }
        QSet<QString> names(entry.beforeGroups.keyBegin(),
                            entry.beforeGroups.keyEnd());
        names.unite(QSet<QString>(entry.afterGroups.keyBegin(),
                                  entry.afterGroups.keyEnd()));
        for (const QString &name : std::as_const(names)) {
            if (entry.beforeGroups.value(name) ==
                entry.afterGroups.value(name)) {
                entry.beforeGroups.remove(name);
                entry.afterGroups.remove(name);
            }
        }
    }
    recomputeHistoryCounts(state);
}

PersistentSelectionCache::ContainerState migrateV2State(
    const ContainerStateV2 &legacy) {
    PersistentSelectionCache::ContainerState result;
    result.selectedGroups = legacy.selectedGroups;
    if (legacy.history.isEmpty()) {
        return result;
    }

    QList<HistoryEntryV2> snapshots = legacy.history;
    const int historyIndex =
        qBound(0, legacy.historyIndex, snapshots.size() - 1);
    snapshots[historyIndex].selectedGroups = legacy.selectedGroups;
    result.historyBaseGroups = snapshots.first().selectedGroups;
    result.history.append(PersistentSelectionCache::HistoryEntry{
        .description = snapshots.first().description,
        .timestamp = snapshots.first().timestamp,
        .selectedCount =
            static_cast<int>(result.historyBaseGroups.size()),
    });
    for (qsizetype i = 1; i < snapshots.size(); ++i) {
        result.history.append(deltaHistoryEntry(
            snapshots[i].description, snapshots[i].timestamp,
            snapshots[i - 1].selectedGroups,
            snapshots[i].selectedGroups));
    }
    result.historyIndex = historyIndex;
    compactHistory(result);
    return result;
}

QMap<QString, QString> sortedHash(const QHash<QString, QString> &hash) {
    QMap<QString, QString> sorted;
    for (auto it = hash.constBegin(); it != hash.constEnd(); ++it) {
        sorted.insert(it.key(), it.value());
    }
    return sorted;
}
}

QHash<QString, PersistentSelectionCache::ContainerState> PersistentSelectionCache::_db;
QList<PersistentSelectionCache::SelectionGroup> PersistentSelectionCache::_groups;
QString PersistentSelectionCache::_activeGroupId;
QHash<QString, QDateTime> PersistentSelectionCache::_selectedFileDates;
QReadWriteLock PersistentSelectionCache::_dbAccess;
bool PersistentSelectionCache::_dbLoaded = false;

QString PersistentSelectionCache::defaultGroupId() {
    return DefaultGroupId;
}

QString PersistentSelectionCache::normalizeContainerKey(const QString &path) {
    if (path == "Computer") {
        return path;
    }

    const QString cleaned = QDir::cleanPath(QDir::fromNativeSeparators(path));
    if (cleaned.isEmpty()) {
        return cleaned;
    }

    return QDir(cleaned).absolutePath();
}

PersistentSelectionCache::ContainerState PersistentSelectionCache::retrieveContainer(
    const QString &containerKey) {
    if (!_dbLoaded) {
        loadDb();
    }

    const QString normalizedKey = normalizeContainerKey(containerKey);
    QReadLocker locker(&_dbAccess);
    return _db.value(normalizedKey);
}

void PersistentSelectionCache::storeContainer(const QString &containerKey,
                                               const ContainerState &state,
                                               bool persistImmediately) {
    if (!_dbLoaded) {
        loadDb();
    }

    const QString normalizedKey = normalizeContainerKey(containerKey);
    {
        QWriteLocker locker(&_dbAccess);
        const QHash<QString, QString> previousGroups =
            _db.value(normalizedKey).selectedGroups;
        const QDateTime now = QDateTime::currentDateTimeUtc();
        QStringList selectedNames = state.selectedGroups.keys();
        selectedNames.sort(Qt::CaseInsensitive);
        int additionOffset = 0;
        for (const QString &name : selectedNames) {
            if (previousGroups.value(name) != state.selectedGroups.value(name)) {
                _selectedFileDates.insert(
                    selectedFilePath(normalizedKey, name),
                    now.addMSecs(additionOffset++));
            }
        }
        for (auto it = previousGroups.constBegin(); it != previousGroups.constEnd(); ++it) {
            if (!state.selectedGroups.contains(it.key())) {
                _selectedFileDates.remove(selectedFilePath(normalizedKey, it.key()));
            }
        }
        _db.insert(normalizedKey, state);
    }
    if (persistImmediately) {
        dumpDb();
    }
}

void PersistentSelectionCache::appendHistoryEntry(
    ContainerState &state, const QString &description,
    const QHash<QString, QString> &previousSelectedGroups,
    const QDateTime &timestamp) {
    if (state.historyIndex < state.history.size() - 1) {
        state.history.resize(state.historyIndex + 1);
    }
    if (state.history.isEmpty()) {
        state.historyBaseGroups = previousSelectedGroups;
        state.history.append(HistoryEntry{
            .description = QStringLiteral("Initial state"),
            .timestamp = timestamp,
            .selectedCount =
                static_cast<int>(previousSelectedGroups.size()),
        });
        state.historyIndex = 0;
    }

    HistoryEntry entry = deltaHistoryEntry(
        description, timestamp, previousSelectedGroups, state.selectedGroups);
    if (entry.beforeGroups.isEmpty() && entry.afterGroups.isEmpty()) {
        return;
    }
    state.history.append(std::move(entry));
    state.historyIndex = state.history.size() - 1;
    compactHistory(state);
}

bool PersistentSelectionCache::applyHistoryIndex(
    ContainerState &state, int historyIndex) {
    if (historyIndex < 0 || historyIndex >= state.history.size() ||
        historyIndex == state.historyIndex) {
        return false;
    }

    if (state.historyIndex < 0) {
        state.selectedGroups = state.historyBaseGroups;
        state.historyIndex = 0;
    }
    if (historyIndex < state.historyIndex) {
        for (int i = state.historyIndex; i > historyIndex; --i) {
            applyGroupChanges(state.selectedGroups,
                              state.history[i].beforeGroups);
        }
    }
    else {
        for (int i = state.historyIndex + 1; i <= historyIndex; ++i) {
            applyGroupChanges(state.selectedGroups,
                              state.history[i].afterGroups);
        }
    }
    state.historyIndex = historyIndex;
    return true;
}

QList<PersistentSelectionCache::SelectedFile>
PersistentSelectionCache::selectedFilesByAdditionDate() {
    if (!_dbLoaded) {
        loadDb();
    }

    QList<SelectedFile> result;
    QReadLocker locker(&_dbAccess);
    for (auto containerIt = _db.constBegin(); containerIt != _db.constEnd(); ++containerIt) {
        for (auto itemIt = containerIt->selectedGroups.constBegin();
             itemIt != containerIt->selectedGroups.constEnd(); ++itemIt) {
            const QString path = selectedFilePath(containerIt.key(), itemIt.key());
            result.append(SelectedFile{
                .path = path,
                .addedAt = _selectedFileDates.value(path),
                .groupId = itemIt.value(),
            });
        }
    }
    std::sort(result.begin(), result.end(), [](const SelectedFile &left, const SelectedFile &right) {
        if (left.addedAt != right.addedAt) {
            return left.addedAt < right.addedAt;
        }
        return QString::compare(left.path, right.path, Qt::CaseInsensitive) < 0;
    });
    return result;
}

bool PersistentSelectionCache::selectedFile(
    const QString &path, SelectedFile &selectedFile) {
    if (!_dbLoaded) {
        loadDb();
    }
    const QFileInfo fileInfo(path);
    const QString containerKey =
        normalizeContainerKey(fileInfo.absolutePath());
    const QString itemKey = fileInfo.fileName();
    const QString normalizedPath = selectedFilePath(containerKey, itemKey);
    QReadLocker locker(&_dbAccess);
    const auto stateIt = _db.constFind(containerKey);
    if (stateIt == _db.constEnd()) {
        return false;
    }
    const QString groupId = stateIt->selectedGroups.value(itemKey);
    if (groupId.isEmpty()) {
        return false;
    }
    selectedFile = SelectedFile{
        .path = normalizedPath,
        .addedAt = _selectedFileDates.value(normalizedPath),
        .groupId = groupId,
    };
    return true;
}

QList<PersistentSelectionCache::SelectionGroup> PersistentSelectionCache::selectionGroups() {
    if (!_dbLoaded) {
        loadDb();
    }
    QReadLocker locker(&_dbAccess);
    return _groups;
}

QString PersistentSelectionCache::activeSelectionGroupId() {
    if (!_dbLoaded) {
        loadDb();
    }
    QReadLocker locker(&_dbAccess);
    return _activeGroupId;
}

bool PersistentSelectionCache::setActiveSelectionGroupId(const QString &groupId) {
    if (!_dbLoaded) {
        loadDb();
    }

    {
        QWriteLocker locker(&_dbAccess);
        const auto it = std::find_if(_groups.constBegin(), _groups.constEnd(),
                                     [&groupId](const SelectionGroup &group) {
                                         return group.id == groupId;
                                     });
        if (it == _groups.constEnd() || _activeGroupId == groupId) {
            return false;
        }
        _activeGroupId = groupId;
    }
    dumpDb();
    return true;
}

QString PersistentSelectionCache::addSelectionGroup() {
    if (!_dbLoaded) {
        loadDb();
    }

    QString newGroupId;
    {
        QWriteLocker locker(&_dbAccess);
        if (_groups.size() >= GroupPalette.size()) {
            return QString();
        }

        QSet<QString> usedColors;
        for (const SelectionGroup &group : _groups) {
            usedColors.insert(group.color.toUpper());
        }

        QString color;
        for (const QString &candidate : GroupPalette) {
            if (!usedColors.contains(candidate.toUpper())) {
                color = candidate;
                break;
            }
        }
        if (color.isEmpty()) {
            return QString();
        }

        newGroupId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        _groups.append(SelectionGroup{
            .id = newGroupId,
            .name = groupNameForColor(color),
            .color = color,
            .createdAt = QDateTime::currentDateTimeUtc(),
            .isDefault = false,
        });
        _activeGroupId = newGroupId;
    }
    dumpDb();
    return newGroupId;
}

bool PersistentSelectionCache::renameSelectionGroup(const QString &groupId,
                                                     const QString &name) {
    if (!_dbLoaded) {
        loadDb();
    }

    const QString trimmedName = name.trimmed();
    if (trimmedName.isEmpty()) {
        return false;
    }

    {
        QWriteLocker locker(&_dbAccess);
        for (const SelectionGroup &group : _groups) {
            if (group.id != groupId &&
                QString::compare(group.name, trimmedName, Qt::CaseInsensitive) == 0) {
                return false;
            }
        }
        auto it = std::find_if(_groups.begin(), _groups.end(),
                               [&groupId](const SelectionGroup &group) {
                                   return group.id == groupId;
                               });
        if (it == _groups.end() || it->name == trimmedName) {
            return false;
        }
        it->name = trimmedName;
    }
    dumpDb();
    return true;
}

bool PersistentSelectionCache::removeSelectionGroup(const QString &groupId) {
    if (!_dbLoaded) {
        loadDb();
    }
    if (groupId == DefaultGroupId) {
        return false;
    }

    {
        QWriteLocker locker(&_dbAccess);
        auto groupIt = std::find_if(_groups.begin(), _groups.end(),
                                    [&groupId](const SelectionGroup &group) {
                                        return group.id == groupId;
                                    });
        if (groupIt == _groups.end()) {
            return false;
        }

        for (auto containerIt = _db.begin(); containerIt != _db.end(); ++containerIt) {
            for (auto itemIt = containerIt->selectedGroups.constBegin();
                 itemIt != containerIt->selectedGroups.constEnd(); ++itemIt) {
                if (itemIt.value() == groupId) {
                    _selectedFileDates.remove(
                        selectedFilePath(containerIt.key(), itemIt.key()));
                }
            }
            replaceGroupReference(*containerIt, groupId, QString());
        }

        _groups.erase(groupIt);
        if (_activeGroupId == groupId) {
            _activeGroupId = DefaultGroupId;
        }
    }
    dumpDb();
    return true;
}

bool PersistentSelectionCache::removeSelectionGroupForMove(
    const QString &groupId) {
    if (groupId != DefaultGroupId) {
        return removeSelectionGroup(groupId);
    }
    if (!_dbLoaded) {
        loadDb();
    }

    {
        QWriteLocker locker(&_dbAccess);
        auto groupIt = std::find_if(
            _groups.begin(), _groups.end(),
            [](const SelectionGroup &group) {
                return group.id == DefaultGroupId;
            });
        if (groupIt == _groups.end()) {
            return false;
        }

        for (auto containerIt = _db.begin(); containerIt != _db.end();
             ++containerIt) {
            for (auto itemIt = containerIt->selectedGroups.constBegin();
                 itemIt != containerIt->selectedGroups.constEnd(); ++itemIt) {
                if (itemIt.value() == DefaultGroupId) {
                    _selectedFileDates.remove(
                        selectedFilePath(containerIt.key(), itemIt.key()));
                }
            }
            replaceGroupReference(*containerIt, DefaultGroupId, QString());
        }

        *groupIt = SelectionGroup{
            .id = DefaultGroupId,
            .name = groupNameForColor(GroupPalette.first()),
            .color = GroupPalette.first(),
            .createdAt = QDateTime::currentDateTimeUtc(),
            .isDefault = true,
        };
        _activeGroupId = DefaultGroupId;
    }
    dumpDb();
    return true;
}

bool PersistentSelectionCache::restoreSelectionGroup(
    const SelectionGroup &group, const QList<SelectedFile> &selectedFiles,
    bool makeActive) {
    if (!_dbLoaded) {
        loadDb();
    }
    if (group.id.isEmpty() || group.name.trimmed().isEmpty()) {
        return false;
    }
    for (const SelectedFile &selectedFile : selectedFiles) {
        if (QFileInfo(selectedFile.path).fileName().isEmpty()) {
            return false;
        }
    }

    {
        QWriteLocker locker(&_dbAccess);
        auto defaultReplacement = _groups.end();
        for (auto it = _groups.begin(); it != _groups.end(); ++it) {
            if (group.isDefault && it->id == DefaultGroupId) {
                defaultReplacement = it;
                continue;
            }
            if (it->id == group.id ||
                QString::compare(it->name, group.name,
                                 Qt::CaseInsensitive) == 0 ||
                QString::compare(it->color, group.color,
                                 Qt::CaseInsensitive) == 0) {
                return false;
            }
        }
        if (group.isDefault) {
            if (defaultReplacement == _groups.end()) {
                return false;
            }
            for (const ContainerState &state : std::as_const(_db)) {
                if (state.selectedGroups.values().contains(DefaultGroupId)) {
                    return false;
                }
            }
            *defaultReplacement = group;
        }
        else {
            _groups.append(group);
        }

        QHash<QString, QHash<QString, QString>> previousSelections;
        for (const SelectedFile &selectedFile : selectedFiles) {
            const QFileInfo fileInfo(selectedFile.path);
            const QString containerKey =
                normalizeContainerKey(fileInfo.absolutePath());
            const QString itemKey = fileInfo.fileName();
            ContainerState &state = _db[containerKey];
            if (!previousSelections.contains(containerKey)) {
                previousSelections.insert(containerKey, state.selectedGroups);
            }
            state.selectedGroups.insert(itemKey, group.id);
            _selectedFileDates.insert(
                selectedFilePath(containerKey, itemKey),
                selectedFile.addedAt.isValid()
                    ? selectedFile.addedAt
                    : QDateTime::currentDateTimeUtc());
        }
        const QDateTime restoredAt = QDateTime::currentDateTime();
        for (auto it = previousSelections.constBegin();
             it != previousSelections.constEnd(); ++it) {
            ContainerState &state = _db[it.key()];
            appendHistoryEntry(
                state, QStringLiteral("Undo move selection group"),
                it.value(), restoredAt);
        }
        if (makeActive) {
            _activeGroupId = group.id;
        }
    }
    dumpDb();
    return true;
}

int PersistentSelectionCache::totalSelectedCount() {
    if (!_dbLoaded) {
        loadDb();
    }
    QReadLocker locker(&_dbAccess);
    int count = 0;
    for (const ContainerState &state : std::as_const(_db)) {
        count += state.selectedGroups.size();
    }
    return count;
}

int PersistentSelectionCache::selectedCountForGroup(const QString &groupId) {
    if (!_dbLoaded) {
        loadDb();
    }
    QReadLocker locker(&_dbAccess);
    int count = 0;
    for (const ContainerState &state : std::as_const(_db)) {
        for (const QString &selectedGroupId : state.selectedGroups) {
            count += selectedGroupId == groupId ? 1 : 0;
        }
    }
    return count;
}

QHash<QString, int> PersistentSelectionCache::selectedCountsByGroup() {
    if (!_dbLoaded) {
        loadDb();
    }
    QHash<QString, int> counts;
    QReadLocker locker(&_dbAccess);
    for (const ContainerState &state : std::as_const(_db)) {
        for (const QString &groupId : state.selectedGroups) {
            counts[groupId]++;
        }
    }
    return counts;
}

QString PersistentSelectionCache::colorForGroup(const QString &groupId) {
    if (!_dbLoaded) {
        loadDb();
    }
    QReadLocker locker(&_dbAccess);
    const auto it = std::find_if(_groups.constBegin(), _groups.constEnd(),
                                 [&groupId](const SelectionGroup &group) {
                                     return group.id == groupId;
                                 });
    return it == _groups.constEnd() ? GroupPalette.first() : it->color;
}

QString PersistentSelectionCache::selectedFilePath(const QString &containerKey,
                                                    const QString &selectedName) {
    if (containerKey == "Computer") {
        return selectedName;
    }
    return QDir(containerKey).absoluteFilePath(selectedName);
}

void PersistentSelectionCache::ensureDefaultGroup() {
    auto defaultIt = std::find_if(_groups.begin(), _groups.end(),
                                  [](const SelectionGroup &group) {
                                      return group.id == DefaultGroupId;
                                  });
    if (defaultIt == _groups.end()) {
        _groups.prepend(SelectionGroup{
            .id = DefaultGroupId,
            .name = GroupColorNames.first(),
            .color = GroupPalette.first(),
            .createdAt = QDateTime::currentDateTimeUtc(),
            .isDefault = true,
        });
    }
    else {
        defaultIt->isDefault = true;
        defaultIt->color = GroupPalette.first();
        if (defaultIt != _groups.begin()) {
            const SelectionGroup defaultGroup = *defaultIt;
            _groups.erase(defaultIt);
            _groups.prepend(defaultGroup);
        }
    }
    for (SelectionGroup &group : _groups) {
        const QString colorName = groupNameForColor(group.color);
        if (!colorName.isEmpty() &&
            (group.name.trimmed().isEmpty() ||
             isLegacyGeneratedGroupName(group.name))) {
            group.name = colorName;
        }
    }
    if (_activeGroupId.isEmpty()) {
        _activeGroupId = DefaultGroupId;
    }
}

void PersistentSelectionCache::repairGroupReferences() {
    QSet<QString> validGroupIds;
    for (const SelectionGroup &group : std::as_const(_groups)) {
        validGroupIds.insert(group.id);
    }
    if (!validGroupIds.contains(_activeGroupId)) {
        _activeGroupId = DefaultGroupId;
    }
    for (ContainerState &state : _db) {
        QSet<QString> invalidGroupIds;
        for (const QString &groupId : std::as_const(state.selectedGroups)) {
            if (!validGroupIds.contains(groupId)) {
                invalidGroupIds.insert(groupId);
            }
        }
        for (const QString &groupId : std::as_const(state.historyBaseGroups)) {
            if (!groupId.isEmpty() && !validGroupIds.contains(groupId)) {
                invalidGroupIds.insert(groupId);
            }
        }
        for (const HistoryEntry &entry : std::as_const(state.history)) {
            for (const QString &groupId : entry.beforeGroups) {
                if (!groupId.isEmpty() && !validGroupIds.contains(groupId)) {
                    invalidGroupIds.insert(groupId);
                }
            }
            for (const QString &groupId : entry.afterGroups) {
                if (!groupId.isEmpty() && !validGroupIds.contains(groupId)) {
                    invalidGroupIds.insert(groupId);
                }
            }
        }
        for (const QString &invalidGroupId :
             std::as_const(invalidGroupIds)) {
            replaceGroupReference(state, invalidGroupId, DefaultGroupId);
        }
        if (state.historyIndex >= state.history.size()) {
            state.historyIndex = state.history.size() - 1;
        }
        if (state.historyIndex < -1) {
            state.historyIndex = -1;
        }
    }
}

void PersistentSelectionCache::loadSelectedFileDates(bool migratedFromV1) {
    const QStringList candidates = migratedFromV1
        ? QStringList{selectionOrderDbV1Path()}
        : QStringList{selectionOrderDbV2Path(), selectionOrderDbV1Path()};
    bool loaded = false;
    for (const QString &path : candidates) {
        QFile orderFile(path);
        if (!orderFile.open(QIODevice::ReadOnly)) {
            continue;
        }

        QDataStream stream(&orderFile);
        quint32 magic = 0;
        quint32 version = 0;
        stream >> magic >> version;
        if (magic == SelectionOrderDbMagic &&
            (version == SelectionOrderDbVersionV1 ||
             version == SelectionOrderDbVersionV2)) {
            stream >> _selectedFileDates;
            loaded = stream.status() == QDataStream::Ok;
        }
        if (loaded) {
            break;
        }
    }
    if (!loaded) {
        rebuildSelectedFileDates();
        return;
    }

    QSet<QString> currentlySelected;
    for (auto containerIt = _db.constBegin(); containerIt != _db.constEnd(); ++containerIt) {
        for (const QString &name : containerIt->selectedGroups.keys()) {
            currentlySelected.insert(selectedFilePath(containerIt.key(), name));
        }
    }
    for (auto it = _selectedFileDates.begin(); it != _selectedFileDates.end();) {
        if (!currentlySelected.contains(it.key())) {
            it = _selectedFileDates.erase(it);
        }
        else {
            currentlySelected.remove(it.key());
            ++it;
        }
    }
    QDateTime fallback = QDateTime::currentDateTimeUtc();
    QStringList missing(currentlySelected.begin(), currentlySelected.end());
    missing.sort(Qt::CaseInsensitive);
    for (const QString &path : missing) {
        _selectedFileDates.insert(path, fallback);
        fallback = fallback.addMSecs(1);
    }
}

void PersistentSelectionCache::rebuildSelectedFileDates() {
    _selectedFileDates.clear();
    QDateTime fallback = QDateTime::currentDateTimeUtc();
    for (auto containerIt = _db.constBegin(); containerIt != _db.constEnd(); ++containerIt) {
        QHash<QString, QDateTime> additionDates;
        const QDateTime baseTimestamp =
            containerIt->history.isEmpty()
                ? QDateTime()
                : containerIt->history.first().timestamp.toUTC();
        for (const QString &name : containerIt->historyBaseGroups.keys()) {
            if (baseTimestamp.isValid()) {
                additionDates.insert(name, baseTimestamp);
            }
        }
        const int lastHistoryIndex =
            qMin(containerIt->historyIndex, containerIt->history.size() - 1);
        for (int i = 1; i <= lastHistoryIndex; ++i) {
            const HistoryEntry &entry = containerIt->history[i];
            for (auto changeIt = entry.afterGroups.constBegin();
                 changeIt != entry.afterGroups.constEnd(); ++changeIt) {
                if (changeIt.value().isEmpty()) {
                    additionDates.remove(changeIt.key());
                }
                else {
                    additionDates.insert(changeIt.key(),
                                         entry.timestamp.toUTC());
                }
            }
        }

        QStringList names = containerIt->selectedGroups.keys();
        names.sort(Qt::CaseInsensitive);
        for (const QString &name : names) {
            QDateTime addedAt = additionDates.value(name);
            if (!addedAt.isValid()) {
                addedAt = fallback;
                fallback = fallback.addMSecs(1);
            }
            _selectedFileDates.insert(
                selectedFilePath(containerIt.key(), name), addedAt);
        }
    }
}

void PersistentSelectionCache::dumpSelectedFileDates() {
    QSaveFile orderFile(selectionOrderDbV2Path());
    if (orderFile.open(QIODevice::WriteOnly)) {
        QDataStream stream(&orderFile);
        stream << SelectionOrderDbMagic << SelectionOrderDbVersionV2
               << _selectedFileDates;
        if (!orderFile.commit()) {
            qWarning() << "Failed to save selection order DB"
                       << selectionOrderDbV2Path() << orderFile.errorString();
        }
    }
}

QDataStream& operator<<(QDataStream& out,
                        const PersistentSelectionCache::SelectionGroup& obj) {
    out << obj.id << obj.name << obj.color << obj.createdAt << obj.isDefault;
    return out;
}

QDataStream& operator>>(QDataStream& in,
                        PersistentSelectionCache::SelectionGroup& obj) {
    in >> obj.id >> obj.name >> obj.color >> obj.createdAt >> obj.isDefault;
    return in;
}

QDataStream& operator<<(QDataStream& out,
                        const PersistentSelectionCache::HistoryEntry& obj) {
    out << obj.description << obj.timestamp
        << sortedHash(obj.beforeGroups) << sortedHash(obj.afterGroups)
        << obj.selectedCount;
    return out;
}

QDataStream& operator>>(QDataStream& in,
                        PersistentSelectionCache::HistoryEntry& obj) {
    QMap<QString, QString> beforeGroups;
    QMap<QString, QString> afterGroups;
    in >> obj.description >> obj.timestamp
       >> beforeGroups >> afterGroups >> obj.selectedCount;
    obj.beforeGroups.clear();
    obj.afterGroups.clear();
    for (auto it = beforeGroups.constBegin(); it != beforeGroups.constEnd(); ++it) {
        obj.beforeGroups.insert(it.key(), it.value());
    }
    for (auto it = afterGroups.constBegin(); it != afterGroups.constEnd(); ++it) {
        obj.afterGroups.insert(it.key(), it.value());
    }
    return in;
}

QDataStream& operator<<(QDataStream& out,
                        const PersistentSelectionCache::ContainerState& obj) {
    out << sortedHash(obj.selectedGroups)
        << sortedHash(obj.historyBaseGroups)
        << obj.history << obj.historyIndex;
    return out;
}

QDataStream& operator>>(QDataStream& in,
                        PersistentSelectionCache::ContainerState& obj) {
    QMap<QString, QString> selectedGroups;
    QMap<QString, QString> historyBaseGroups;
    in >> selectedGroups >> historyBaseGroups
       >> obj.history >> obj.historyIndex;
    obj.selectedGroups.clear();
    obj.historyBaseGroups.clear();
    for (auto it = selectedGroups.constBegin(); it != selectedGroups.constEnd(); ++it) {
        obj.selectedGroups.insert(it.key(), it.value());
    }
    for (auto it = historyBaseGroups.constBegin();
         it != historyBaseGroups.constEnd(); ++it) {
        obj.historyBaseGroups.insert(it.key(), it.value());
    }
    if (obj.historyIndex >= obj.history.size()) {
        obj.historyIndex = obj.history.size() - 1;
    }
    if (obj.historyIndex < -1) {
        obj.historyIndex = -1;
    }
    return in;
}

void PersistentSelectionCache::loadDb() {
    bool migratedFromV1 = false;
    bool loadedSuccessfully = false;
    {
        QWriteLocker locker(&_dbAccess);
        if (_dbLoaded) {
            return;
        }

        QFile dbV3(selectionDbV3Path());
        if (dbV3.open(QIODevice::ReadOnly)) {
            QDataStream stream(&dbV3);
            quint32 magic = 0;
            quint32 version = 0;
            stream >> magic >> version;
            if (magic == SelectionDbMagic &&
                version == SelectionDbVersionV3) {
                stream >> _db >> _groups >> _activeGroupId;
                if (stream.status() != QDataStream::Ok) {
                    _db.clear();
                    _groups.clear();
                    _activeGroupId.clear();
                }
                else {
                    loadedSuccessfully = true;
                    qDebug() << "Loaded selection DB v3 with" << _db.size()
                             << "containers and" << _groups.size() << "groups";
                }
            }
        }

        if (!loadedSuccessfully) {
            QFile dbV2(selectionDbV2Path());
            if (dbV2.open(QIODevice::ReadOnly)) {
                QDataStream stream(&dbV2);
                quint32 magic = 0;
                quint32 version = 0;
                stream >> magic >> version;
                if (magic == SelectionDbMagic &&
                    version == SelectionDbVersionV2) {
                    QHash<QString, ContainerStateV2> legacyDb;
                    stream >> legacyDb >> _groups >> _activeGroupId;
                    if (stream.status() == QDataStream::Ok) {
                        for (auto it = legacyDb.constBegin();
                             it != legacyDb.constEnd(); ++it) {
                            _db.insert(it.key(), migrateV2State(it.value()));
                        }
                        loadedSuccessfully = true;
                        qDebug() << "Migrated selection DB v2 with"
                                 << _db.size() << "containers and"
                                 << _groups.size() << "groups";
                    }
                }
            }
        }

        if (!loadedSuccessfully) {
            QFile dbV1(selectionDbV1Path());
            if (dbV1.open(QIODevice::ReadOnly)) {
                QDataStream stream(&dbV1);
                quint32 magic = 0;
                quint32 version = 0;
                stream >> magic >> version;
                if (magic == SelectionDbMagic && version == SelectionDbVersionV1) {
                    QHash<QString, ContainerStateV1> legacyDb;
                    stream >> legacyDb;
                    if (stream.status() == QDataStream::Ok) {
                        for (auto containerIt = legacyDb.constBegin();
                             containerIt != legacyDb.constEnd(); ++containerIt) {
                            ContainerStateV2 legacyState;
                            for (const QString &name : containerIt->selectedNames) {
                                legacyState.selectedGroups.insert(
                                    name, DefaultGroupId);
                            }
                            for (const HistoryEntryV1 &legacyEntry : containerIt->history) {
                                HistoryEntryV2 entry{
                                    .description = legacyEntry.description,
                                    .timestamp = legacyEntry.timestamp,
                                };
                                for (const QString &name : legacyEntry.selectedNames) {
                                    entry.selectedGroups.insert(name, DefaultGroupId);
                                }
                                legacyState.history.append(entry);
                            }
                            legacyState.historyIndex =
                                containerIt->historyIndex;
                            _db.insert(containerIt.key(),
                                       migrateV2State(legacyState));
                        }
                        migratedFromV1 = true;
                        loadedSuccessfully = true;
                        qDebug() << "Migrated selection DB v1 with" << _db.size()
                                 << "containers";
                    }
                }
            }
        }

        ensureDefaultGroup();
        repairGroupReferences();
        loadSelectedFileDates(migratedFromV1);
        _dbLoaded = true;
    }
    dumpDb();
}

void PersistentSelectionCache::dumpDb() {
    if (!_dbLoaded) {
        loadDb();
    }

    QReadLocker locker(&_dbAccess);
    QSaveFile dbFile(selectionDbV3Path());
    if (dbFile.open(QIODevice::WriteOnly)) {
        QDataStream stream(&dbFile);
        stream << SelectionDbMagic << SelectionDbVersionV3
               << _db << _groups << _activeGroupId;
        if (!dbFile.commit()) {
            qWarning() << "Failed to save selection DB"
                       << selectionDbV3Path() << dbFile.errorString();
        }
    }
    dumpSelectedFileDates();
}

#ifdef ZOIN_ENABLE_SELECTION_CACHE_TESTS
void PersistentSelectionCache::resetForTests() {
    QWriteLocker locker(&_dbAccess);
    _db.clear();
    _groups.clear();
    _activeGroupId.clear();
    _selectedFileDates.clear();
    _dbLoaded = false;
}
#endif
