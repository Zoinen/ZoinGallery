#include "PersistentFolderCache.h"

#include <QBuffer>
#include <QImage>
#include <QFile>
#include <QDebug>
#include <QElapsedTimer>

QHash<QString, QList<FileInfo>> PersistentFolderCache::_db;
QReadWriteLock PersistentFolderCache::_dbAccess;
uint16_t PersistentFolderCache::_currentChunkFileIndex = 0;

void PersistentFolderCache::retrieveFolders(const QStringList &folders, QList<FolderInfo> &outInfoList, QStringList &outNotFound) {
    static bool cacheDbLoaded = false;
    if (!cacheDbLoaded) {
        cacheDbLoaded = true;
        loadDb();
    }

    _dbAccess.lockForRead();
    for (const QString &path : folders) {
        auto it = _db.find(path);
        if (it != _db.end()) {
            FolderInfo result{
                .path = path,
                .subfiles = it.value(),
            };
            outInfoList.append(result);
        }
        else {
            outNotFound.append(path);
        }
    }
    _dbAccess.unlock();
}

void PersistentFolderCache::storeFolders(QList<FolderInfo> &folders) {
    _dbAccess.lockForWrite();
    for (FolderInfo &info : folders) {
        _db.insert(info.path, info.subfiles);
    }
    _dbAccess.unlock();
}

void PersistentFolderCache::storeFolder(const FolderInfo &folder) {
    _dbAccess.lockForWrite();
    _db.insert(folder.path, folder.subfiles);
    _dbAccess.unlock();
}

QDataStream& operator<<(QDataStream& out, const FileInfo& obj) {
    out << obj.name << obj.lastModified;
    return out;
}

QDataStream& operator>>(QDataStream& in, FileInfo& obj) {
    in >> obj.name >> obj.lastModified;
    return in;
}

QDataStream& operator<<(QDataStream& out, const FolderInfo& obj) {
    out << obj.path << obj.subfiles;
    return out;
}

QDataStream& operator>>(QDataStream& in, FolderInfo& obj) {
    in >> obj.path >> obj.subfiles;
    return in;
}

void PersistentFolderCache::loadDb() {
    _dbAccess.lockForWrite();

    QFile dbFile("C:/tmp/zg_folders.db");
    if (dbFile.open(QIODevice::ReadOnly)) {
        QDataStream stream(&dbFile);
        stream >> _db;

        dbFile.close();

        qDebug() << "Loaded folders DB with" << _db.size() << "entities";
    }

    _dbAccess.unlock();
}

void PersistentFolderCache::dumpDb() {
    _dbAccess.lockForWrite();

    QFile dbFile("C:/tmp/zg_folders.db");
    if (dbFile.open(QIODevice::WriteOnly)) {
        QDataStream stream(&dbFile);
        stream << _db;
        dbFile.close();

        qDebug() << "Saved folders DB with" << _db.size() << "entities";
    }

    _dbAccess.unlock();
}
