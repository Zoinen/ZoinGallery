#ifndef PERSISTENTSELECTIONCACHE_H
#define PERSISTENTSELECTIONCACHE_H

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QReadWriteLock>
#include <QString>

class QDataStream;

class PersistentSelectionCache {
public:
    struct SelectionGroup {
        QString id;
        QString name;
        QString color;
        QDateTime createdAt;
        bool isDefault = false;
    };

    struct SelectedFile {
        QString path;
        QDateTime addedAt;
        QString groupId;
    };

    struct HistoryEntry {
        QString description;
        QDateTime timestamp;
        QHash<QString, QString> selectedGroups;
    };

    struct ContainerState {
        QHash<QString, QString> selectedGroups;
        QList<HistoryEntry> history;
        int historyIndex = -1;
    };

    static QString defaultGroupId();
    static QString normalizeContainerKey(const QString &path);
    static ContainerState retrieveContainer(const QString &containerKey);
    static void storeContainer(const QString &containerKey, const ContainerState &state);
    static QList<SelectedFile> selectedFilesByAdditionDate();

    static QList<SelectionGroup> selectionGroups();
    static QString activeSelectionGroupId();
    static bool setActiveSelectionGroupId(const QString &groupId);
    static QString addSelectionGroup();
    static bool renameSelectionGroup(const QString &groupId, const QString &name);
    static bool removeSelectionGroup(const QString &groupId);
    static int totalSelectedCount();
    static int selectedCountForGroup(const QString &groupId);
    static QString colorForGroup(const QString &groupId);

    static void loadDb();
    static void dumpDb();

#ifdef ZOIN_ENABLE_SELECTION_CACHE_TESTS
    static void resetForTests();
#endif

private:
    static QHash<QString, ContainerState> _db;
    static QList<SelectionGroup> _groups;
    static QString _activeGroupId;
    static QHash<QString, QDateTime> _selectedFileDates;
    static QReadWriteLock _dbAccess;
    static bool _dbLoaded;

    static QString selectedFilePath(const QString &containerKey, const QString &selectedName);
    static void ensureDefaultGroup();
    static void repairGroupReferences();
    static void loadSelectedFileDates(bool migratedFromV1);
    static void dumpSelectedFileDates();
    static void rebuildSelectedFileDates();

    friend QDataStream& operator<<(QDataStream& out, const SelectionGroup& obj);
    friend QDataStream& operator>>(QDataStream& in, SelectionGroup& obj);

    friend QDataStream& operator<<(QDataStream& out, const HistoryEntry& obj);
    friend QDataStream& operator>>(QDataStream& in, HistoryEntry& obj);

    friend QDataStream& operator<<(QDataStream& out, const ContainerState& obj);
    friend QDataStream& operator>>(QDataStream& in, ContainerState& obj);
};

#endif // PERSISTENTSELECTIONCACHE_H
