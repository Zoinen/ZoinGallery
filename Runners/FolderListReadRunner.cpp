#include "FolderListReadRunner.h"

#include "PersistentFolderCache.h"
#include "ThumbnailLoader.h"

#include <QDir>

FolderListReadRunner::FolderListReadRunner(const QString &path, int totalImages)
    : _path(path), _totalImages(totalImages) {
}

void FolderListReadRunner::run() {
    QDir dir(_path);
    auto images = dir.entryInfoList(ThumbnailLoader::supportedFormats(), QDir::Files, QDir::Name);
    if (_totalImages == -1) {
        QList<FileInfo> subfiles;
        for (QFileInfo &image : images) {
            subfiles.append({image.fileName(), image.lastModified()});
        }
        PersistentFolderCache::storeFolder(FolderInfo{_path, subfiles});
        emit folderListReady(_path, subfiles);
    }
    else {
        QList<FileInfo> imagesFiltered;
        for (float i = 0; i < images.size(); i += qMax(1.0f, float(images.size()) / _totalImages)) {
            imagesFiltered.append({images.at(i).fileName(), images.at(i).lastModified()});
        }
        PersistentFolderCache::storeFolder(FolderInfo{_path, imagesFiltered});

        emit folderListReady(_path, imagesFiltered);
    }
    emit finished(this);
}
