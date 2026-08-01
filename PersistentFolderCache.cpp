#include "PersistentFolderCache.h"

#include "NaturalSort.h"

#include <QBuffer>
#include <QImage>
#include <QFile>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QMutexLocker>
#include <QStandardPaths>

namespace {
QString folderCacheDbPath() {
    const QString tempPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    const QString basePath = tempPath.isEmpty() ? QDir::tempPath() : tempPath;
    return QDir(basePath).filePath("zg_folders_v2.db");
}
}

QHash<QString, QList<FileInfo>> PersistentFolderCache::_db;
QReadWriteLock PersistentFolderCache::_dbAccess;
QMutex PersistentFolderCache::_dbLoadAccess;
bool PersistentFolderCache::_dbLoaded = false;
std::atomic<quint64> PersistentFolderCache::_generation = 0;

void PersistentFolderCache::retrieveFolders(const QStringList &folders, QList<FolderInfo> &outInfoList, QStringList &outNotFound) {
    loadDb();

    QReadLocker locker(&_dbAccess);
    for (const QString &path : folders) {
        const auto it = _db.constFind(path);
        if (it != _db.cend()) {
            FolderInfo result{
                .path = path,
                .subfiles = it.value(),
            };
            sortFileInfosNaturally(result.subfiles);
            outInfoList.append(result);
        }
        else {
            outNotFound.append(path);
        }
    }
}

bool PersistentFolderCache::retrieveFolder(const QString &path, FolderInfo &outFolder) {
    QList<FolderInfo> results;
    QStringList notFound;
    retrieveFolders({path}, results, notFound);
    if (results.isEmpty()) {
        return false;
    }
    outFolder = results.first();
    return true;
}

void PersistentFolderCache::storeFolders(const QList<FolderInfo> &folders) {
    loadDb();
    QWriteLocker locker(&_dbAccess);
    for (const FolderInfo &info : folders) {
        QList<FileInfo> subfiles = info.subfiles;
        sortFileInfosNaturally(subfiles);
        _db.insert(info.path, subfiles);
    }
}

void PersistentFolderCache::storeFolder(const FolderInfo &folder) {
    storeFolder(folder, generation());
}

void PersistentFolderCache::storeFolder(const FolderInfo &folder, quint64 expectedGeneration) {
    loadDb();
    QList<FileInfo> subfiles = folder.subfiles;
    sortFileInfosNaturally(subfiles);

    QWriteLocker locker(&_dbAccess);
    if (expectedGeneration != generation()) {
        return;
    }
    _db.insert(folder.path, subfiles);
}

void PersistentFolderCache::removeFolder(const QString &path) {
    removeFolders({path});
}

void PersistentFolderCache::removeFolders(const QStringList &paths) {
    if (paths.isEmpty()) {
        return;
    }
    loadDb();
    _generation.fetch_add(1);
    {
        QWriteLocker locker(&_dbAccess);
        for (const QString &path : paths) {
            _db.remove(path);
        }
    }
    dumpDb();
}

QDataStream& operator<<(QDataStream& out, const FileInfo& obj) {
    out << obj.name << obj.lastModified << obj.fileSize << obj.isDirectory;
    return out;
}

QDataStream& operator>>(QDataStream& in, FileInfo& obj) {
    in >> obj.name >> obj.lastModified >> obj.fileSize >> obj.isDirectory;
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
    QMutexLocker loadLocker(&_dbLoadAccess);
    if (_dbLoaded) {
        return;
    }

    QWriteLocker locker(&_dbAccess);
    QFile dbFile(folderCacheDbPath());
    if (dbFile.open(QIODevice::ReadOnly)) {
        QDataStream stream(&dbFile);
        stream >> _db;
        if (stream.status() == QDataStream::Ok) {
            qDebug() << "Loaded folders DB with" << _db.size() << "entities";
        }
        else {
            _db.clear();
            qWarning() << "Ignoring unreadable folder cache DB" << folderCacheDbPath();
        }
    }
    _dbLoaded = true;
}

void PersistentFolderCache::dumpDb() {
    loadDb();
    QReadLocker locker(&_dbAccess);

    if (_db.isEmpty()) {
        QFile::remove(folderCacheDbPath());
        return;
    }

    QFile dbFile(folderCacheDbPath());
    if (dbFile.open(QIODevice::WriteOnly)) {
        QDataStream stream(&dbFile);
        stream << _db;
        if (stream.status() == QDataStream::Ok) {
            qDebug() << "Saved folders DB with" << _db.size() << "entities";
        }
        else {
            qWarning() << "Failed to save folder cache DB" << folderCacheDbPath();
        }
    }
}

qint64 PersistentFolderCache::cacheSize() {
    loadDb();

    qint64 serializedDbSize = 0;
    {
        QReadLocker locker(&_dbAccess);
        if (!_db.isEmpty()) {
            QByteArray serializedDb;
            QDataStream stream(&serializedDb, QIODevice::WriteOnly);
            stream << _db;
            if (stream.status() == QDataStream::Ok) {
                serializedDbSize = serializedDb.size();
            }
        }
    }
    return qMax(serializedDbSize, QFileInfo(folderCacheDbPath()).size());
}

QString PersistentFolderCache::cacheLocation() {
    return QDir::toNativeSeparators(folderCacheDbPath());
}

void PersistentFolderCache::clear() {
    loadDb();
    _generation.fetch_add(1);

    QWriteLocker locker(&_dbAccess);
    _db.clear();
    QFile::remove(folderCacheDbPath());
}

quint64 PersistentFolderCache::generation() {
    return _generation.load();
}
