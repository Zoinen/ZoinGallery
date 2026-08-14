#include "PersistentSelectionCache.h"
#include "StorageLocations.h"

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QSet>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>

namespace {
constexpr quint32 SelectionDbMagic = 0x5a47534c;
constexpr quint32 SelectionOrderDbMagic = 0x5a47534f;

struct LegacyHistoryEntry {
    QString description;
    QDateTime timestamp;
    QSet<QString> selectedNames;
};

struct LegacyContainerState {
    QSet<QString> selectedNames;
    QList<LegacyHistoryEntry> history;
    int historyIndex = -1;
};

struct SnapshotHistoryEntry {
    QString description;
    QDateTime timestamp;
    QHash<QString, QString> selectedGroups;
};

struct SnapshotContainerState {
    QHash<QString, QString> selectedGroups;
    QList<SnapshotHistoryEntry> history;
    int historyIndex = -1;
};

QDataStream& operator<<(QDataStream& out, const LegacyHistoryEntry& obj) {
    QStringList names(obj.selectedNames.begin(), obj.selectedNames.end());
    names.sort(Qt::CaseInsensitive);
    out << obj.description << obj.timestamp << names;
    return out;
}

QDataStream& operator<<(QDataStream& out, const LegacyContainerState& obj) {
    QStringList names(obj.selectedNames.begin(), obj.selectedNames.end());
    names.sort(Qt::CaseInsensitive);
    out << names << obj.history << obj.historyIndex;
    return out;
}

QDataStream& operator<<(QDataStream& out,
                        const SnapshotHistoryEntry& obj) {
    QMap<QString, QString> groups;
    for (auto it = obj.selectedGroups.constBegin();
         it != obj.selectedGroups.constEnd(); ++it) {
        groups.insert(it.key(), it.value());
    }
    out << obj.description << obj.timestamp << groups;
    return out;
}

QDataStream& operator<<(QDataStream& out,
                        const SnapshotContainerState& obj) {
    QMap<QString, QString> groups;
    for (auto it = obj.selectedGroups.constBegin();
         it != obj.selectedGroups.constEnd(); ++it) {
        groups.insert(it.key(), it.value());
    }
    out << groups << obj.history << obj.historyIndex;
    return out;
}
}

class PersistentSelectionCacheTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QVERIFY(_dataDir.isValid());
        qputenv("ZOIN_SELECTION_DATA_DIR", _dataDir.path().toUtf8());
    }

    void init() {
        PersistentSelectionCache::resetForTests();
        const QDir dir(_dataDir.path());
        for (const QString &fileName :
             dir.entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot)) {
            QVERIFY(QFile::remove(dir.filePath(fileName)));
        }
    }

    void isolatesStandaloneAndEmbeddedStorageRoots() {
        const QString standaloneCache =
            ZoinGallery::StorageLocations::cacheRootForNamespace(
                QStringLiteral("standalone"));
        const QString f4Cache =
            ZoinGallery::StorageLocations::cacheRootForNamespace(
                QStringLiteral("f4"));
        const QString standaloneData =
            ZoinGallery::StorageLocations::dataRootForNamespace(
                QStringLiteral("standalone"));
        const QString f4Data =
            ZoinGallery::StorageLocations::dataRootForNamespace(
                QStringLiteral("f4"));

        QVERIFY(!standaloneCache.isEmpty());
        QVERIFY(!standaloneData.isEmpty());
        QVERIFY(standaloneCache != f4Cache);
        QVERIFY(standaloneData != f4Data);
        QVERIFY(f4Cache.endsWith(QStringLiteral("ZoinGallery/f4")));
        QVERIFY(f4Data.endsWith(QStringLiteral("ZoinGallery/f4")));
    }

    void createsDistinctPresetGroups() {
        const QStringList expectedColors{
            QStringLiteral("#FFD43B"),
            QStringLiteral("#40C057"),
            QStringLiteral("#15AABF"),
            QStringLiteral("#228BE6"),
            QStringLiteral("#7950F2"),
            QStringLiteral("#D6336C"),
            QStringLiteral("#FA5252"),
            QStringLiteral("#FD7E14"),
        };
        const QStringList expectedNames{
            QStringLiteral("Yellow"),
            QStringLiteral("Green"),
            QStringLiteral("Cyan"),
            QStringLiteral("Blue"),
            QStringLiteral("Purple"),
            QStringLiteral("Pink"),
            QStringLiteral("Red"),
            QStringLiteral("Orange"),
        };
        const auto initialGroups = PersistentSelectionCache::selectionGroups();
        QCOMPARE(initialGroups.size(), 1);
        QCOMPARE(initialGroups.first().id,
                 PersistentSelectionCache::defaultGroupId());
        QCOMPARE(initialGroups.first().name, expectedNames.first());
        QCOMPARE(initialGroups.first().color, expectedColors.first());
        QVERIFY(initialGroups.first().isDefault);

        QSet<QString> colors{initialGroups.first().color};
        for (int i = 0; i < 7; ++i) {
            const QString groupId = PersistentSelectionCache::addSelectionGroup();
            QVERIFY(!groupId.isEmpty());
            const auto groups = PersistentSelectionCache::selectionGroups();
            QCOMPARE(groups.last().color, expectedColors[i + 1]);
            QCOMPARE(groups.last().name, expectedNames[i + 1]);
            colors.insert(groups.last().color);
            QCOMPARE(PersistentSelectionCache::activeSelectionGroupId(), groupId);
        }
        QCOMPARE(colors.size(), 8);
        QVERIFY(PersistentSelectionCache::addSelectionGroup().isEmpty());
        QVERIFY(!PersistentSelectionCache::removeSelectionGroup(
            PersistentSelectionCache::defaultGroupId()));
    }

    void migratesLegacyGeneratedGroupNames() {
        const QString secondGroup =
            PersistentSelectionCache::addSelectionGroup();
        QVERIFY(PersistentSelectionCache::renameSelectionGroup(
            PersistentSelectionCache::defaultGroupId(),
            QStringLiteral("Selection 1")));
        QVERIFY(PersistentSelectionCache::renameSelectionGroup(
            secondGroup, QStringLiteral("Selection 2")));

        PersistentSelectionCache::resetForTests();
        const auto migratedGroups =
            PersistentSelectionCache::selectionGroups();
        QCOMPARE(migratedGroups.size(), 2);
        QCOMPARE(migratedGroups[0].name, QStringLiteral("Yellow"));
        QCOMPARE(migratedGroups[1].name, QStringLiteral("Green"));

        QVERIFY(PersistentSelectionCache::renameSelectionGroup(
            secondGroup, QStringLiteral("Keepers")));
        PersistentSelectionCache::resetForTests();
        const auto customGroups =
            PersistentSelectionCache::selectionGroups();
        QCOMPARE(customGroups[1].name, QStringLiteral("Keepers"));
    }

    void migratesV1SelectionAndOrder() {
        const QString container = _dataDir.path() + QStringLiteral("/images");
        const QDateTime addedAt =
            QDateTime::fromString(QStringLiteral("2024-01-02T03:04:05.006Z"),
                                  Qt::ISODateWithMs);
        QHash<QString, LegacyContainerState> legacyDb;
        LegacyContainerState legacyState;
        legacyState.selectedNames = {QStringLiteral("a.jpg"), QStringLiteral("b.png")};
        legacyState.history.append(LegacyHistoryEntry{
            .description = QStringLiteral("Initial state"),
            .timestamp = addedAt,
            .selectedNames = {QStringLiteral("a.jpg")},
        });
        legacyState.historyIndex = 0;
        legacyDb.insert(container, legacyState);

        QFile dbFile(_dataDir.filePath(QStringLiteral("selection_v1.db")));
        QVERIFY(dbFile.open(QIODevice::WriteOnly));
        QDataStream dbStream(&dbFile);
        dbStream << SelectionDbMagic << quint32(1) << legacyDb;
        dbFile.close();

        QHash<QString, QDateTime> order;
        order.insert(QDir(container).absoluteFilePath(QStringLiteral("a.jpg")), addedAt);
        order.insert(QDir(container).absoluteFilePath(QStringLiteral("b.png")),
                     addedAt.addMSecs(1));
        QFile orderFile(_dataDir.filePath(QStringLiteral("selection_order_v1.db")));
        QVERIFY(orderFile.open(QIODevice::WriteOnly));
        QDataStream orderStream(&orderFile);
        orderStream << SelectionOrderDbMagic << quint32(1) << order;
        orderFile.close();

        const auto state = PersistentSelectionCache::retrieveContainer(container);
        QCOMPARE(state.selectedGroups.size(), 2);
        QCOMPARE(state.selectedGroups.value(QStringLiteral("a.jpg")),
                 PersistentSelectionCache::defaultGroupId());
        QCOMPARE(state.historyBaseGroups.value(QStringLiteral("a.jpg")),
                 PersistentSelectionCache::defaultGroupId());

        const auto files = PersistentSelectionCache::selectedFilesByAdditionDate();
        QCOMPARE(files.size(), 2);
        QCOMPARE(files.first().path,
                 QDir(container).absoluteFilePath(QStringLiteral("a.jpg")));
        QCOMPARE(files.first().addedAt, addedAt);
        QCOMPARE(files.first().groupId,
                 PersistentSelectionCache::defaultGroupId());
        QCOMPARE(PersistentSelectionCache::selectionGroups().first().name,
                 QStringLiteral("Yellow"));
        QVERIFY(QFile::exists(_dataDir.filePath(QStringLiteral("selection_v3.db"))));
    }

    void persistsGroupsAndSanitizesDeletedGroupHistory() {
        const QString secondGroup = PersistentSelectionCache::addSelectionGroup();
        QVERIFY(PersistentSelectionCache::renameSelectionGroup(
            secondGroup, QStringLiteral("Keepers")));

        const QString container = _dataDir.path() + QStringLiteral("/images");
        PersistentSelectionCache::ContainerState state;
        state.selectedGroups.insert(QStringLiteral("a.jpg"),
                                    PersistentSelectionCache::defaultGroupId());
        state.selectedGroups.insert(QStringLiteral("b.jpg"), secondGroup);
        PersistentSelectionCache::appendHistoryEntry(
            state, QStringLiteral("Two groups"), {},
            QDateTime::currentDateTimeUtc());
        PersistentSelectionCache::storeContainer(container, state);

        PersistentSelectionCache::resetForTests();
        QCOMPARE(PersistentSelectionCache::activeSelectionGroupId(), secondGroup);
        QCOMPARE(PersistentSelectionCache::selectionGroups().size(), 2);
        QCOMPARE(PersistentSelectionCache::totalSelectedCount(), 2);

        QVERIFY(PersistentSelectionCache::removeSelectionGroup(secondGroup));
        QCOMPARE(PersistentSelectionCache::activeSelectionGroupId(),
                 PersistentSelectionCache::defaultGroupId());
        const auto repairedState =
            PersistentSelectionCache::retrieveContainer(container);
        QCOMPARE(repairedState.selectedGroups.size(), 1);
        QVERIFY(!repairedState.selectedGroups.contains(QStringLiteral("b.jpg")));
        QVERIFY(!repairedState.historyBaseGroups.contains(
            QStringLiteral("b.jpg")));
        for (const auto &entry : repairedState.history) {
            QVERIFY(!entry.beforeGroups.values().contains(secondGroup));
            QVERIFY(!entry.afterGroups.values().contains(secondGroup));
        }
    }

    void restoresRemovedGroupWithOriginalSelections() {
        const QString groupId = PersistentSelectionCache::addSelectionGroup();
        QVERIFY(PersistentSelectionCache::renameSelectionGroup(
            groupId, QStringLiteral("Trip")));
        const auto groups = PersistentSelectionCache::selectionGroups();
        const auto groupIt = std::find_if(
            groups.constBegin(), groups.constEnd(),
            [&groupId](const auto &group) { return group.id == groupId; });
        QVERIFY(groupIt != groups.constEnd());

        const QString firstPath =
            QDir(_dataDir.path()).absoluteFilePath(QStringLiteral("one.jpg"));
        const QString secondPath =
            QDir(_dataDir.path()).absoluteFilePath(QStringLiteral("two.jpg"));
        const QDateTime firstAdded =
            QDateTime::currentDateTimeUtc().addSecs(-2);
        const QDateTime secondAdded = firstAdded.addSecs(1);
        PersistentSelectionCache::ContainerState state;
        state.selectedGroups.insert(QStringLiteral("one.jpg"), groupId);
        state.selectedGroups.insert(QStringLiteral("two.jpg"), groupId);
        PersistentSelectionCache::storeContainer(_dataDir.path(), state);

        auto selectedFiles =
            PersistentSelectionCache::selectedFilesByAdditionDate();
        QCOMPARE(selectedFiles.size(), 2);
        selectedFiles[0].addedAt = firstAdded;
        selectedFiles[1].addedAt = secondAdded;

        QVERIFY(PersistentSelectionCache::removeSelectionGroup(groupId));
        QCOMPARE(PersistentSelectionCache::selectedCountForGroup(groupId), 0);
        QVERIFY(PersistentSelectionCache::restoreSelectionGroup(
            *groupIt, selectedFiles));

        QCOMPARE(PersistentSelectionCache::activeSelectionGroupId(), groupId);
        const auto restoredState =
            PersistentSelectionCache::retrieveContainer(_dataDir.path());
        QCOMPARE(restoredState.selectedGroups.value(QStringLiteral("one.jpg")),
                 groupId);
        QCOMPARE(restoredState.selectedGroups.value(QStringLiteral("two.jpg")),
                 groupId);
        QCOMPARE(PersistentSelectionCache::selectedCountForGroup(groupId), 2);
        QCOMPARE(PersistentSelectionCache::selectedFilesByAdditionDate().first().path,
                 firstPath);
        QCOMPARE(PersistentSelectionCache::selectedFilesByAdditionDate().last().path,
                 secondPath);
    }

    void replacesAndRestoresDefaultGroupForMove() {
        const QString defaultId = PersistentSelectionCache::defaultGroupId();
        QVERIFY(PersistentSelectionCache::renameSelectionGroup(
            defaultId, QStringLiteral("Family")));
        const auto originalGroup =
            PersistentSelectionCache::selectionGroups().first();
        const QString imagePath =
            QDir(_dataDir.path()).absoluteFilePath(QStringLiteral("family.jpg"));
        PersistentSelectionCache::ContainerState state;
        state.selectedGroups.insert(QStringLiteral("family.jpg"), defaultId);
        PersistentSelectionCache::storeContainer(_dataDir.path(), state);
        const auto selectedFiles =
            PersistentSelectionCache::selectedFilesByAdditionDate();

        QVERIFY(PersistentSelectionCache::removeSelectionGroupForMove(defaultId));
        QCOMPARE(PersistentSelectionCache::selectionGroups().first().name,
                 QStringLiteral("Yellow"));
        QCOMPARE(PersistentSelectionCache::selectedCountForGroup(defaultId), 0);

        QVERIFY(PersistentSelectionCache::restoreSelectionGroup(
            originalGroup, selectedFiles));
        QCOMPARE(PersistentSelectionCache::selectionGroups().first().name,
                 QStringLiteral("Family"));
        QCOMPARE(PersistentSelectionCache::selectedCountForGroup(defaultId), 1);
        QCOMPARE(PersistentSelectionCache::selectedFilesByAdditionDate().first().path,
                 imagePath);
    }

    void migratesSnapshotHistoryToCompactDeltas() {
        constexpr int SelectionCount = 400;
        const QString container =
            _dataDir.path() + QStringLiteral("/large");
        const QString groupId =
            PersistentSelectionCache::defaultGroupId();
        SnapshotContainerState legacyState;
        const QDateTime start = QDateTime::currentDateTimeUtc();
        for (int i = 0; i < SelectionCount; ++i) {
            legacyState.selectedGroups.insert(
                QStringLiteral("image-%1.jpg").arg(i, 4, 10, QLatin1Char('0')),
                groupId);
            legacyState.history.append(SnapshotHistoryEntry{
                .description = QStringLiteral("Select image"),
                .timestamp = start.addMSecs(i),
                .selectedGroups = legacyState.selectedGroups,
            });
        }
        legacyState.historyIndex = legacyState.history.size() - 1;
        QHash<QString, SnapshotContainerState> legacyDb;
        legacyDb.insert(container, legacyState);

        QFile dbFile(
            _dataDir.filePath(QStringLiteral("selection_v2.db")));
        QVERIFY(dbFile.open(QIODevice::WriteOnly));
        QDataStream stream(&dbFile);
        const QList<PersistentSelectionCache::SelectionGroup> groups{
            PersistentSelectionCache::SelectionGroup{
                .id = groupId,
                .name = QStringLiteral("Yellow"),
                .color = QStringLiteral("#FFD43B"),
                .createdAt = start,
                .isDefault = true,
            },
        };
        stream << SelectionDbMagic << quint32(2) << legacyDb
               << groups << groupId;
        dbFile.close();
        const qint64 legacySize = QFileInfo(dbFile).size();

        PersistentSelectionCache::resetForTests();
        const auto migrated =
            PersistentSelectionCache::retrieveContainer(container);
        QCOMPARE(migrated.selectedGroups.size(), SelectionCount);
        QVERIFY(migrated.history.size() <= 256);
        QCOMPARE(migrated.historyIndex, migrated.history.size() - 1);
        for (int i = 1; i < migrated.history.size(); ++i) {
            QVERIFY(migrated.history[i].afterGroups.size() <= 1);
            QVERIFY(migrated.history[i].beforeGroups.size() <= 1);
        }

        const QFileInfo compactDb(
            _dataDir.filePath(QStringLiteral("selection_v3.db")));
        QVERIFY(compactDb.exists());
        QVERIFY2(compactDb.size() * 5 < legacySize,
                 qPrintable(QStringLiteral(
                     "v3=%1 bytes, v2=%2 bytes")
                     .arg(compactDb.size()).arg(legacySize)));
    }

    void preservesMidHistoryCursorAcrossV2MigrationAndV3Reload() {
        const QString container =
            _dataDir.path() + QStringLiteral("/cursor");
        const QString groupId =
            PersistentSelectionCache::defaultGroupId();
        const QDateTime start = QDateTime::currentDateTimeUtc();
        SnapshotContainerState legacyState;
        for (int i = 0; i < 5; ++i) {
            legacyState.selectedGroups.insert(
                QStringLiteral("image-%1.jpg").arg(i), groupId);
            legacyState.history.append(SnapshotHistoryEntry{
                .description = QStringLiteral("Select %1").arg(i),
                .timestamp = start.addMSecs(i),
                .selectedGroups = legacyState.selectedGroups,
            });
        }
        legacyState.historyIndex = 2;
        legacyState.selectedGroups =
            legacyState.history[legacyState.historyIndex].selectedGroups;
        QHash<QString, SnapshotContainerState> legacyDb;
        legacyDb.insert(container, legacyState);
        const QList<PersistentSelectionCache::SelectionGroup> groups{
            PersistentSelectionCache::SelectionGroup{
                .id = groupId,
                .name = QStringLiteral("Yellow"),
                .color = QStringLiteral("#FFD43B"),
                .createdAt = start,
                .isDefault = true,
            },
        };

        QFile dbFile(
            _dataDir.filePath(QStringLiteral("selection_v2.db")));
        QVERIFY(dbFile.open(QIODevice::WriteOnly));
        QDataStream stream(&dbFile);
        stream << SelectionDbMagic << quint32(2) << legacyDb
               << groups << groupId;
        dbFile.close();

        PersistentSelectionCache::resetForTests();
        auto migrated =
            PersistentSelectionCache::retrieveContainer(container);
        QCOMPARE(migrated.historyIndex, 2);
        QCOMPARE(migrated.selectedGroups.size(), 3);
        QVERIFY(PersistentSelectionCache::applyHistoryIndex(migrated, 1));
        QCOMPARE(migrated.selectedGroups.size(), 2);
        QVERIFY(PersistentSelectionCache::applyHistoryIndex(migrated, 4));
        QCOMPARE(migrated.selectedGroups.size(), 5);
        PersistentSelectionCache::storeContainer(container, migrated);

        PersistentSelectionCache::resetForTests();
        auto reloaded =
            PersistentSelectionCache::retrieveContainer(container);
        QCOMPARE(reloaded.historyIndex, 4);
        QCOMPARE(reloaded.selectedGroups.size(), 5);
        QVERIFY(PersistentSelectionCache::applyHistoryIndex(reloaded, 2));
        QCOMPARE(reloaded.selectedGroups.size(), 3);
        QVERIFY(PersistentSelectionCache::applyHistoryIndex(reloaded, 0));
        QCOMPARE(reloaded.selectedGroups.size(), 1);
        QVERIFY(!PersistentSelectionCache::applyHistoryIndex(reloaded, 0));
        QVERIFY(!PersistentSelectionCache::applyHistoryIndex(reloaded, -1));
        QVERIFY(!PersistentSelectionCache::applyHistoryIndex(reloaded, 5));
    }

    void deltaHistorySupportsUndoRedoAndStaysBounded() {
        PersistentSelectionCache::ContainerState state;
        const QString groupId =
            PersistentSelectionCache::defaultGroupId();
        for (int i = 0; i < 1000; ++i) {
            const auto before = state.selectedGroups;
            state.selectedGroups.insert(
                QStringLiteral("image-%1.jpg").arg(i), groupId);
            PersistentSelectionCache::appendHistoryEntry(
                state, QStringLiteral("Select"), before);
        }

        QCOMPARE(state.selectedGroups.size(), 1000);
        QVERIFY(state.history.size() <= 256);
        QCOMPARE(state.historyIndex, state.history.size() - 1);
        qsizetype storedChanges = 0;
        for (const auto &entry : state.history) {
            storedChanges += entry.beforeGroups.size();
            storedChanges += entry.afterGroups.size();
        }
        QVERIFY(storedChanges < 600);

        const int lastIndex = state.historyIndex;
        QVERIFY(PersistentSelectionCache::applyHistoryIndex(
            state, lastIndex - 1));
        QCOMPARE(state.selectedGroups.size(), 999);
        QVERIFY(PersistentSelectionCache::applyHistoryIndex(
            state, lastIndex));
        QCOMPARE(state.selectedGroups.size(), 1000);

        QVERIFY(PersistentSelectionCache::applyHistoryIndex(state, 0));
        QCOMPARE(state.selectedGroups, state.historyBaseGroups);
        const auto branchBase = state.selectedGroups;
        state.selectedGroups.insert(QStringLiteral("branch.jpg"), groupId);
        PersistentSelectionCache::appendHistoryEntry(
            state, QStringLiteral("Branch"), branchBase);
        QCOMPARE(state.historyIndex, 1);
        QCOMPARE(state.history.size(), 2);
    }

    void deltaHistoryRestoresGroupTransfers() {
        PersistentSelectionCache::ContainerState state;
        const QString firstGroup =
            PersistentSelectionCache::defaultGroupId();
        const QString secondGroup = QStringLiteral("selection-second");
        state.selectedGroups.insert(QStringLiteral("image.jpg"), firstGroup);
        const auto beforeTransfer = state.selectedGroups;
        state.selectedGroups[QStringLiteral("image.jpg")] = secondGroup;
        PersistentSelectionCache::appendHistoryEntry(
            state, QStringLiteral("Transfer"), beforeTransfer);

        QCOMPARE(state.history.size(), 2);
        QCOMPARE(state.history.last().beforeGroups.value(
                     QStringLiteral("image.jpg")), firstGroup);
        QCOMPARE(state.history.last().afterGroups.value(
                     QStringLiteral("image.jpg")), secondGroup);
        QVERIFY(PersistentSelectionCache::applyHistoryIndex(state, 0));
        QCOMPARE(state.selectedGroups.value(QStringLiteral("image.jpg")),
                 firstGroup);
        QVERIFY(PersistentSelectionCache::applyHistoryIndex(state, 1));
        QCOMPARE(state.selectedGroups.value(QStringLiteral("image.jpg")),
                 secondGroup);
    }

    void repairsCorruptGroupReferencesAndRefreshesTransferDate() {
        const QString container = _dataDir.path() + QStringLiteral("/images");
        PersistentSelectionCache::ContainerState state;
        state.selectedGroups.insert(QStringLiteral("a.jpg"),
                                    PersistentSelectionCache::defaultGroupId());
        state.historyBaseGroups = state.selectedGroups;
        state.history.append(PersistentSelectionCache::HistoryEntry{
            .description = QStringLiteral("Selected"),
            .timestamp = QDateTime::currentDateTimeUtc(),
            .selectedCount = 1,
        });
        state.historyIndex = 0;
        PersistentSelectionCache::storeContainer(container, state);

        QByteArray expectedId;
        {
            QDataStream stream(&expectedId, QIODevice::WriteOnly);
            stream << PersistentSelectionCache::defaultGroupId();
        }
        QByteArray corruptId;
        {
            QDataStream stream(&corruptId, QIODevice::WriteOnly);
            stream << QStringLiteral("selection-missing");
        }
        QCOMPARE(corruptId.size(), expectedId.size());

        QFile dbFile(_dataDir.filePath(QStringLiteral("selection_v3.db")));
        QVERIFY(dbFile.open(QIODevice::ReadOnly));
        QByteArray contents = dbFile.readAll();
        dbFile.close();
        for (int replacement = 0; replacement < 2; ++replacement) {
            const qsizetype offset = contents.indexOf(expectedId);
            QVERIFY(offset >= 0);
            contents.replace(offset, expectedId.size(), corruptId);
        }
        QVERIFY(dbFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(dbFile.write(contents), contents.size());
        dbFile.close();

        PersistentSelectionCache::resetForTests();
        const auto repairedState =
            PersistentSelectionCache::retrieveContainer(container);
        QCOMPARE(repairedState.selectedGroups.value(QStringLiteral("a.jpg")),
                 PersistentSelectionCache::defaultGroupId());
        QCOMPARE(repairedState.historyBaseGroups.value(
                     QStringLiteral("a.jpg")),
                 PersistentSelectionCache::defaultGroupId());

        auto files = PersistentSelectionCache::selectedFilesByAdditionDate();
        QCOMPARE(files.first().groupId,
                 PersistentSelectionCache::defaultGroupId());
        const QDateTime originalDate = files.first().addedAt;

        const QString secondGroup = PersistentSelectionCache::addSelectionGroup();
        state = PersistentSelectionCache::retrieveContainer(container);
        state.selectedGroups[QStringLiteral("a.jpg")] = secondGroup;
        PersistentSelectionCache::storeContainer(container, state);
        files = PersistentSelectionCache::selectedFilesByAdditionDate();
        QCOMPARE(files.first().groupId, secondGroup);
        QVERIFY(files.first().addedAt >= originalDate);
    }

private:
    QTemporaryDir _dataDir;
};

QTEST_MAIN(PersistentSelectionCacheTest)

#include "PersistentSelectionCacheTest.moc"
