#include "PersistentSelectionCache.h"

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QSet>
#include <QTemporaryDir>
#include <QtTest>

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
        QCOMPARE(state.history.first().selectedGroups.value(QStringLiteral("a.jpg")),
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
        QVERIFY(QFile::exists(_dataDir.filePath(QStringLiteral("selection_v2.db"))));
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
        state.history.append(PersistentSelectionCache::HistoryEntry{
            .description = QStringLiteral("Two groups"),
            .timestamp = QDateTime::currentDateTimeUtc(),
            .selectedGroups = state.selectedGroups,
        });
        state.historyIndex = 0;
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
        QVERIFY(!repairedState.history.first().selectedGroups.contains(
            QStringLiteral("b.jpg")));
    }

    void repairsCorruptGroupReferencesAndRefreshesTransferDate() {
        const QString container = _dataDir.path() + QStringLiteral("/images");
        PersistentSelectionCache::ContainerState state;
        state.selectedGroups.insert(QStringLiteral("a.jpg"),
                                    PersistentSelectionCache::defaultGroupId());
        state.history.append(PersistentSelectionCache::HistoryEntry{
            .description = QStringLiteral("Selected"),
            .timestamp = QDateTime::currentDateTimeUtc(),
            .selectedGroups = state.selectedGroups,
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

        QFile dbFile(_dataDir.filePath(QStringLiteral("selection_v2.db")));
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
        QCOMPARE(repairedState.history.first().selectedGroups.value(
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
