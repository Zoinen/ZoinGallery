#include "FolderListReadRunner.h"

#include <QDir>
#include "ThumbnailLoader.h"

FolderListReadRunner::FolderListReadRunner(const QString &path, int totalImages)
    : _path(path), _totalImages(totalImages) {
}

void FolderListReadRunner::run() {
    QDir dir(_path);
    auto images = dir.entryInfoList(ThumbnailLoader::supportedFormats(), QDir::Files, QDir::Name);
    if (_totalImages == -1) {
        emit folderListReady(_path, _totalImages, images);
    }
    else {
        QList<QFileInfo> imagesFiltered;
        for (float i = 0; i < images.size(); i += qMax(1.0f, float(images.size()) / _totalImages)) {
            imagesFiltered.append(images.at(i));
        }
        emit folderListReady(_path, _totalImages, imagesFiltered);
    }
    emit finished(this);
}
