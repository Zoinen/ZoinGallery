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
constexpr quint32 SelectionOrderDbMagic = 0x5a47534f; // "ZGSO"
constexpr quint32 SelectionOrderDbVersionV1 = 1;
constexpr quint32 SelectionOrderDbVersionV2 = 2;

const QString DefaultGroupId = QStringLiteral("selection-default");

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
                                               const ContainerState &state) {
    if (!_dbLoaded) {
        loadDb();
    }

    const QString normalizedKey = normalizeContainerKey(containerKey);
    {
        QWriteLocker locker(&_dbAccess);
        QSet<QString> validGroupIds;
        for (const SelectionGroup &group : _groups) {
            validGroupIds.insert(group.id);
        }

        ContainerState repairedState = state;
        for (auto it = repairedState.selectedGroups.begin();
             it != repairedState.selectedGroups.end(); ++it) {
            if (!validGroupIds.contains(it.value())) {
                it.value() = DefaultGroupId;
            }
        }
        for (HistoryEntry &entry : repairedState.history) {
            for (auto it = entry.selectedGroups.begin(); it != entry.selectedGroups.end(); ++it) {
                if (!validGroupIds.contains(it.value())) {
                    it.value() = DefaultGroupId;
                }
            }
        }

        const QHash<QString, QString> previousGroups =
            _db.value(normalizedKey).selectedGroups;
        const QDateTime now = QDateTime::currentDateTimeUtc();
        QStringList selectedNames = repairedState.selectedGroups.keys();
        selectedNames.sort(Qt::CaseInsensitive);
        int additionOffset = 0;
        for (const QString &name : selectedNames) {
            if (previousGroups.value(name) != repairedState.selectedGroups.value(name)) {
                _selectedFileDates.insert(
                    selectedFilePath(normalizedKey, name),
                    now.addMSecs(additionOffset++));
            }
        }
        for (auto it = previousGroups.constBegin(); it != previousGroups.constEnd(); ++it) {
            if (!repairedState.selectedGroups.contains(it.key())) {
                _selectedFileDates.remove(selectedFilePath(normalizedKey, it.key()));
            }
        }
        _db.insert(normalizedKey, repairedState);
    }
    dumpDb();
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
            for (auto itemIt = containerIt->selectedGroups.begin();
                 itemIt != containerIt->selectedGroups.end();) {
                if (itemIt.value() == groupId) {
                    _selectedFileDates.remove(
                        selectedFilePath(containerIt.key(), itemIt.key()));
                    itemIt = containerIt->selectedGroups.erase(itemIt);
                }
                else {
                    ++itemIt;
                }
            }
            for (HistoryEntry &entry : containerIt->history) {
                for (auto itemIt = entry.selectedGroups.begin();
                     itemIt != entry.selectedGroups.end();) {
                    if (itemIt.value() == groupId) {
                        itemIt = entry.selectedGroups.erase(itemIt);
                    }
                    else {
                        ++itemIt;
                    }
                }
            }
        }

        _groups.erase(groupIt);
        if (_activeGroupId == groupId) {
            _activeGroupId = DefaultGroupId;
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
        for (auto it = state.selectedGroups.begin(); it != state.selectedGroups.end(); ++it) {
            if (!validGroupIds.contains(it.value())) {
                it.value() = DefaultGroupId;
            }
        }
        for (HistoryEntry &entry : state.history) {
            for (auto it = entry.selectedGroups.begin(); it != entry.selectedGroups.end(); ++it) {
                if (!validGroupIds.contains(it.value())) {
                    it.value() = DefaultGroupId;
                }
            }
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
        QStringList names = containerIt->selectedGroups.keys();
        names.sort(Qt::CaseInsensitive);
        for (const QString &name : names) {
            QDateTime addedAt;
            bool previouslySelected = false;
            const int lastHistoryIndex =
                qMin(containerIt->historyIndex, containerIt->history.size() - 1);
            for (int i = 0; i <= lastHistoryIndex; i++) {
                const bool selected =
                    containerIt->history[i].selectedGroups.contains(name);
                if (selected && !previouslySelected) {
                    addedAt = containerIt->history[i].timestamp.toUTC();
                }
                previouslySelected = selected;
            }
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
    out << obj.description << obj.timestamp << sortedHash(obj.selectedGroups);
    return out;
}

QDataStream& operator>>(QDataStream& in,
                        PersistentSelectionCache::HistoryEntry& obj) {
    QMap<QString, QString> selectedGroups;
    in >> obj.description >> obj.timestamp >> selectedGroups;
    obj.selectedGroups.clear();
    for (auto it = selectedGroups.constBegin(); it != selectedGroups.constEnd(); ++it) {
        obj.selectedGroups.insert(it.key(), it.value());
    }
    return in;
}

QDataStream& operator<<(QDataStream& out,
                        const PersistentSelectionCache::ContainerState& obj) {
    out << sortedHash(obj.selectedGroups) << obj.history << obj.historyIndex;
    return out;
}

QDataStream& operator>>(QDataStream& in,
                        PersistentSelectionCache::ContainerState& obj) {
    QMap<QString, QString> selectedGroups;
    in >> selectedGroups >> obj.history >> obj.historyIndex;
    obj.selectedGroups.clear();
    for (auto it = selectedGroups.constBegin(); it != selectedGroups.constEnd(); ++it) {
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

void PersistentSelectionCache::loadDb() {
    bool migratedFromV1 = false;
    {
        QWriteLocker locker(&_dbAccess);
        if (_dbLoaded) {
            return;
        }

        QFile dbV2(selectionDbV2Path());
        if (dbV2.open(QIODevice::ReadOnly)) {
            QDataStream stream(&dbV2);
            quint32 magic = 0;
            quint32 version = 0;
            stream >> magic >> version;
            if (magic == SelectionDbMagic && version == SelectionDbVersionV2) {
                stream >> _db >> _groups >> _activeGroupId;
                if (stream.status() != QDataStream::Ok) {
                    _db.clear();
                    _groups.clear();
                    _activeGroupId.clear();
                }
                else {
                    qDebug() << "Loaded selection DB v2 with" << _db.size()
                             << "containers and" << _groups.size() << "groups";
                }
            }
        }

        if (_groups.isEmpty() && _db.isEmpty()) {
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
                            ContainerState state;
                            for (const QString &name : containerIt->selectedNames) {
                                state.selectedGroups.insert(name, DefaultGroupId);
                            }
                            for (const HistoryEntryV1 &legacyEntry : containerIt->history) {
                                HistoryEntry entry{
                                    .description = legacyEntry.description,
                                    .timestamp = legacyEntry.timestamp,
                                };
                                for (const QString &name : legacyEntry.selectedNames) {
                                    entry.selectedGroups.insert(name, DefaultGroupId);
                                }
                                state.history.append(entry);
                            }
                            state.historyIndex = containerIt->historyIndex;
                            _db.insert(containerIt.key(), state);
                        }
                        migratedFromV1 = true;
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
    QSaveFile dbFile(selectionDbV2Path());
    if (dbFile.open(QIODevice::WriteOnly)) {
        QDataStream stream(&dbFile);
        stream << SelectionDbMagic << SelectionDbVersionV2
               << _db << _groups << _activeGroupId;
        if (!dbFile.commit()) {
            qWarning() << "Failed to save selection DB"
                       << selectionDbV2Path() << dbFile.errorString();
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
