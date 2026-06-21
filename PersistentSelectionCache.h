#ifndef PERSISTENTSELECTIONCACHE_H
#define PERSISTENTSELECTIONCACHE_H

#include <QDateTime>
#include <QHash>
#include <QReadWriteLock>
#include <QSet>
#include <QString>

class QDataStream;

class PersistentSelectionCache {
public:
    struct HistoryEntry {
        QString description;
        QDateTime timestamp;
        QSet<QString> selectedNames;
    };

    struct ContainerState {
        QSet<QString> selectedNames;
        QList<HistoryEntry> history;
        int historyIndex = -1;
    };

    static QString normalizeContainerKey(const QString &path);
    static ContainerState retrieveContainer(const QString &containerKey);
    static void storeContainer(const QString &containerKey, const ContainerState &state);

    static void loadDb();
    static void dumpDb();

private:
    static QHash<QString, ContainerState> _db;
    static QReadWriteLock _dbAccess;
    static bool _dbLoaded;

    friend QDataStream& operator<<(QDataStream& out, const HistoryEntry& obj);
    friend QDataStream& operator>>(QDataStream& in, HistoryEntry& obj);

    friend QDataStream& operator<<(QDataStream& out, const ContainerState& obj);
    friend QDataStream& operator>>(QDataStream& in, ContainerState& obj);
};

#endif // PERSISTENTSELECTIONCACHE_H
