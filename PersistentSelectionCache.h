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
        QHash<QString, QString> beforeGroups;
        QHash<QString, QString> afterGroups;
        int selectedCount = 0;
    };

    struct ContainerState {
        QHash<QString, QString> selectedGroups;
        QHash<QString, QString> historyBaseGroups;
        QList<HistoryEntry> history;
        int historyIndex = -1;
    };

    static QString defaultGroupId();
    static QString normalizeContainerKey(const QString &path);
    static ContainerState retrieveContainer(const QString &containerKey);
    static void storeContainer(const QString &containerKey,
                               const ContainerState &state,
                               bool persistImmediately = true);
    static void appendHistoryEntry(
        ContainerState &state, const QString &description,
        const QHash<QString, QString> &previousSelectedGroups,
        const QDateTime &timestamp = QDateTime::currentDateTime());
    static bool applyHistoryIndex(ContainerState &state, int historyIndex);
    static QList<SelectedFile> selectedFilesByAdditionDate();
    static bool selectedFile(const QString &path, SelectedFile &selectedFile);

    static QList<SelectionGroup> selectionGroups();
    static QString activeSelectionGroupId();
    static bool setActiveSelectionGroupId(const QString &groupId);
    static QString addSelectionGroup();
    static bool renameSelectionGroup(const QString &groupId, const QString &name);
    static bool removeSelectionGroup(const QString &groupId);
    static bool removeSelectionGroupForMove(const QString &groupId);
    static bool restoreSelectionGroup(const SelectionGroup &group,
                                      const QList<SelectedFile> &selectedFiles,
                                      bool makeActive = true);
    static int totalSelectedCount();
    static int selectedCountForGroup(const QString &groupId);
    static QHash<QString, int> selectedCountsByGroup();
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
