#include "FolderListReadRunner.h"

#include "NaturalSort.h"
#include "PersistentFolderCache.h"
#include "ThumbnailLoader.h"

#include <QDir>

namespace {
QList<FileInfo> previewImages(const QList<FileInfo> &entries, int totalImages) {
    QList<FileInfo> images;
    for (const FileInfo &entry : entries) {
        if (!entry.isDirectory && ThumbnailLoader::isFormatSupported(entry.name)) {
            images.append(entry);
        }
    }
    sortFileInfosNaturally(images);
    if (totalImages < 0 || images.size() <= totalImages) {
        return images;
    }
    if (totalImages == 0) {
        return {};
    }

    QList<FileInfo> sampled;
    const float step = qMax(1.0f, float(images.size()) / totalImages);
    for (float index = 0; index < images.size() && sampled.size() < totalImages; index += step) {
        sampled.append(images.at(static_cast<int>(index)));
    }
    return sampled;
}
}

FolderListReadRunner::FolderListReadRunner(const QString &path, int totalImages, bool storeInCache)
    : _path(path), _totalImages(totalImages), _storeInCache(storeInCache),
      _cacheGeneration(PersistentFolderCache::generation()) {
}

void FolderListReadRunner::run() {
    QDir dir(_path);
    const auto sourceEntries = dir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden | QDir::System,
                                                  QDir::NoSort);
    QList<FileInfo> entries;
    entries.reserve(sourceEntries.size());
    for (const QFileInfo &entry : sourceEntries) {
        entries.append(FileInfo{
            .name = entry.fileName(),
            .lastModified = entry.lastModified(),
            .fileSize = entry.isDir() ? 0 : entry.size(),
            .isDirectory = entry.isDir(),
        });
    }
    if (_storeInCache && !isCanceled()) {
        PersistentFolderCache::storeFolder(FolderInfo{_path, entries}, _cacheGeneration);
    }
    if (!isCanceled()) {
        emit folderListReady(_path, previewImages(entries, _totalImages));
    }
    emit finished(this);
}
