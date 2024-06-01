#include "PersistentCache.h"

#include <QBuffer>
#include <QImage>
#include <QFile>
#include <QDebug>
#include <QElapsedTimer>

PersistentCache::PersistentCache()
    : QObject(nullptr) {
    loadDb();

    QFile f(QString("C:/tmp/zg_0"));
    f.open(QFile::ReadOnly);

    QFile f2(QString("C:/tmp/zg_0"));
    qDebug() << "ZZZ??" << f2.open(QFile::ReadOnly) << f2.read(100);
}

QDataStream& operator<<(QDataStream& out, const PersistentCache::ThumbnailLocation& obj)
{
    out << obj.chunkFileIndex << obj.offsetInChunk << obj.thumbnailSize;
    return out;
}

QDataStream& operator>>(QDataStream& in, PersistentCache::ThumbnailLocation& obj)
{
    in >> obj.chunkFileIndex >> obj.offsetInChunk >> obj.thumbnailSize;
    return in;
}

QDataStream& operator<<(QDataStream& out, const PersistentCache::ThumbnailInfo& obj)
{
    out << obj.lastModified << obj.location << obj.exif;
    return out;
}

QDataStream& operator>>(QDataStream& in, PersistentCache::ThumbnailInfo& obj)
{
    in >> obj.lastModified >> obj.location >> obj.exif;
    return in;
}

void PersistentCache::add(const QString &path, const QDateTime &lastModified, const QImage &image, const QVariantMap &exif) {
    auto it = _db.find(path);
    // TODO: Update when date changes
    if (it == _db.end()) {
        qDebug() << "ADD" << path << lastModified;
        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        QImage scaled = image.scaled(QSize(1920, 1080), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        scaled.invertPixels();
        scaled.save(&buffer, "webp", 10);

        uint64_t thumbnailSize = bytes.size();

        if (!_currentChunkFile.isOpen()) {
            _currentChunkFile.setFileName(QString("C:/tmp/zg_%1").arg(_currentChunkFileIndex));
            _currentChunkFile.open(QFile::WriteOnly | QFile::Append);
            _currentChunkFileSize = _currentChunkFile.size();
        }

        _currentChunkFile.write(bytes);

        //tmp
        _currentChunkFile.close();

        _currentChunkFileLastThumbnail++;
        ThumbnailInfo info{lastModified, {_currentChunkFileIndex, _currentChunkFileSize, thumbnailSize}, exif};
        _currentChunkFileSize += thumbnailSize;

        _db.insert(path, info);
    }
}

void PersistentCache::requestThumbnail(const QString &path, const QDateTime &lastModified) {
    // qDebug() << "REQUESTED" << path;
    auto it = _db.find(path);
    if (it != _db.end()) {
        if (lastModified == it.value().lastModified) {
            if (!_currentChunkFileData.isEmpty() && 0) {
                QElapsedTimer t;
                t.start();

                // qDebug() << "FOUND IN MEMORY";
                QImage cachedImage = QImage::fromData((const uchar *)_currentChunkFileData.constData() + it.value().location.offsetInChunk, it.value().location.thumbnailSize, "webp");

                int took = t.restart();
                static int time = 0;
                time += took;
                qDebug() << "DECODED" << took << ", TOTAL:" << time;
                emit cachedThumbnailAvailable(path, cachedImage);
            }
            else if (!_currentChunkFile.isOpen()) {
                QElapsedTimer t;
                t.start();

                _currentChunkFile.setFileName(QString("C:/tmp/zg_%1").arg(it.value().location.chunkFileIndex));
                if (_currentChunkFile.open(QFile::ReadOnly)) {
                    if (_currentChunkFile.seek(it.value().location.offsetInChunk)) {
                        QByteArray thumbnailData = _currentChunkFile.read(it.value().location.thumbnailSize);
                        // qDebug() << "FOUND" << thumbnailData.size() << it.value().location.thumbnailSize;

                        QImage cachedImage = QImage::fromData(thumbnailData, "webp");

                        int took = t.restart();
                        static int time = 0;
                        time += took;
                        qDebug() << "DECODED" << took << ", TOTAL:" << time;

                        emit cachedThumbnailAvailable(path, cachedImage);
                    }
                    _currentChunkFile.close();
                }
            }
        }
    }
}

void PersistentCache::loadDb() {
    QFile dbFile("C:/tmp/zg.db");
    if (dbFile.open(QIODevice::ReadOnly)) {
        QDataStream stream(&dbFile);
        stream >> _db;

        dbFile.close();

        qDebug() << "Loaded DB with" << _db.size() << "entities";
    }

    loadChunkFile();
}

void PersistentCache::dumpDb() {
    QFile dbFile("C:/tmp/zg.db");
    if (dbFile.open(QIODevice::WriteOnly)) {
        QDataStream stream(&dbFile);
        stream << _db;
        dbFile.close();

        qDebug() << "Saved DB with" << _db.size() << "entities";
    }
}

void PersistentCache::loadChunkFile() {
    if (!_currentChunkFile.isOpen()) {
        _currentChunkFile.setFileName(QString("C:/tmp/zg_%1").arg(0));
        if (_currentChunkFile.open(QFile::ReadOnly)) {
            _currentChunkFileData = _currentChunkFile.readAll();
            _currentChunkFile.close();
        }
    }
}
