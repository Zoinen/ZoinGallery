#include "PersistentSelectionCache.h"

#include <QDataStream>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QStandardPaths>

namespace {
constexpr quint32 SelectionDbMagic = 0x5a47534c; // "ZGSL"
constexpr quint32 SelectionDbVersion = 1;

QString selectionDbPath() {
    const QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir dir(dataPath);
    dir.mkpath(".");
    return dir.filePath("selection_v1.db");
}

QStringList sortedSetValues(const QSet<QString> &set) {
    QStringList values(set.begin(), set.end());
    values.sort(Qt::CaseInsensitive);
    return values;
}
}

QHash<QString, PersistentSelectionCache::ContainerState> PersistentSelectionCache::_db;
QReadWriteLock PersistentSelectionCache::_dbAccess;
bool PersistentSelectionCache::_dbLoaded = false;

QString PersistentSelectionCache::normalizeContainerKey(const QString &path) {
    if (path == "Computer") {
        return path;
    }

    QString cleaned = QDir::cleanPath(QDir::fromNativeSeparators(path));
    if (cleaned.isEmpty()) {
        return cleaned;
    }

    QDir dir(cleaned);
    return dir.absolutePath();
}

PersistentSelectionCache::ContainerState PersistentSelectionCache::retrieveContainer(const QString &containerKey) {
    if (!_dbLoaded) {
        loadDb();
    }

    const QString normalizedKey = normalizeContainerKey(containerKey);
    QReadLocker locker(&_dbAccess);
    return _db.value(normalizedKey);
}

void PersistentSelectionCache::storeContainer(const QString &containerKey, const ContainerState &state) {
    if (!_dbLoaded) {
        loadDb();
    }

    const QString normalizedKey = normalizeContainerKey(containerKey);
    {
        QWriteLocker locker(&_dbAccess);
        _db.insert(normalizedKey, state);
    }
    dumpDb();
}

QDataStream& operator<<(QDataStream& out, const PersistentSelectionCache::HistoryEntry& obj) {
    out << obj.description << obj.timestamp << sortedSetValues(obj.selectedNames);
    return out;
}

QDataStream& operator>>(QDataStream& in, PersistentSelectionCache::HistoryEntry& obj) {
    QStringList selectedNames;
    in >> obj.description >> obj.timestamp >> selectedNames;
    obj.selectedNames = QSet<QString>(selectedNames.begin(), selectedNames.end());
    return in;
}

QDataStream& operator<<(QDataStream& out, const PersistentSelectionCache::ContainerState& obj) {
    out << sortedSetValues(obj.selectedNames) << obj.history << obj.historyIndex;
    return out;
}

QDataStream& operator>>(QDataStream& in, PersistentSelectionCache::ContainerState& obj) {
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

void PersistentSelectionCache::loadDb() {
    QWriteLocker locker(&_dbAccess);
    if (_dbLoaded) {
        return;
    }

    QFile dbFile(selectionDbPath());
    if (dbFile.open(QIODevice::ReadOnly)) {
        QDataStream stream(&dbFile);
        quint32 magic = 0;
        quint32 version = 0;
        stream >> magic >> version;
        if (magic == SelectionDbMagic && version == SelectionDbVersion) {
            stream >> _db;
            qDebug() << "Loaded selection DB with" << _db.size() << "containers";
        }
        else {
            qWarning() << "Ignoring unsupported selection DB" << selectionDbPath();
        }
    }

    _dbLoaded = true;
}

void PersistentSelectionCache::dumpDb() {
    if (!_dbLoaded) {
        loadDb();
    }

    QReadLocker locker(&_dbAccess);

    QSaveFile dbFile(selectionDbPath());
    if (dbFile.open(QIODevice::WriteOnly)) {
        QDataStream stream(&dbFile);
        stream << SelectionDbMagic << SelectionDbVersion << _db;
        if (!dbFile.commit()) {
            qWarning() << "Failed to save selection DB" << selectionDbPath() << dbFile.errorString();
        }
    }
}
