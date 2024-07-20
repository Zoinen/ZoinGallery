#ifndef PERSISTENTFOLDERCACHE_H
#define PERSISTENTFOLDERCACHE_H

#include <QObject>
#include <QHash>
#include <QDateTime>
#include <QFile>
#include <QDataStream>
#include <QReadWriteLock>

#include "ImageFile.h"

class PersistentFolderCache {
public:
    static void retrieveFolders(const QStringList &folders, QList<FolderInfo> &outInfoList, QStringList &outNotFound);
    static void storeFolders(QList<FolderInfo> &folders);
    static void storeFolder(const FolderInfo &folder);

    static void loadDb();
    static void dumpDb();

private:
    static uint16_t _currentChunkFileIndex;
    static QHash<QString, QList<FileInfo>> _db;
    static QReadWriteLock _dbAccess;

    friend QDataStream& operator<<(QDataStream& out, const FileInfo& obj);
    friend QDataStream& operator>>(QDataStream& in, FileInfo& obj);

    friend QDataStream& operator<<(QDataStream& out, const FolderInfo& obj);
    friend QDataStream& operator>>(QDataStream& in, FolderInfo& obj);
};

#endif // PERSISTENTIMAGECACHE_H
