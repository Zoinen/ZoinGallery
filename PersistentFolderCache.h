#ifndef PERSISTENTFOLDERCACHE_H
#define PERSISTENTFOLDERCACHE_H

#include <QObject>
#include <QHash>
#include <QDateTime>
#include <QFile>
#include <QDataStream>
#include <QMutex>
#include <QReadWriteLock>

#include <atomic>

#include "ImageFile.h"

class PersistentFolderCache {
public:
    static void retrieveFolders(const QStringList &folders, QList<FolderInfo> &outInfoList, QStringList &outNotFound);
    static bool retrieveFolder(const QString &path, FolderInfo &outFolder);
    static void storeFolders(const QList<FolderInfo> &folders);
    static void storeFolder(const FolderInfo &folder);
    static void storeFolder(const FolderInfo &folder, quint64 expectedGeneration);

    static void loadDb();
    static void dumpDb();
    static qint64 cacheSize();
    static QString cacheLocation();
    static void clear();
    static quint64 generation();

private:
    static QHash<QString, QList<FileInfo>> _db;
    static QReadWriteLock _dbAccess;
    static QMutex _dbLoadAccess;
    static bool _dbLoaded;
    static std::atomic<quint64> _generation;

    friend QDataStream& operator<<(QDataStream& out, const FileInfo& obj);
    friend QDataStream& operator>>(QDataStream& in, FileInfo& obj);

    friend QDataStream& operator<<(QDataStream& out, const FolderInfo& obj);
    friend QDataStream& operator>>(QDataStream& in, FolderInfo& obj);
};

#endif // PERSISTENTFOLDERCACHE_H
