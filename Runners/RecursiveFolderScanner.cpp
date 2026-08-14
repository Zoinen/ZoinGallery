#include "RecursiveFolderScanner.h"

#include "NaturalSort.h"
#include "PersistentImageCache.h"
#include "ThumbnailLoader.h"

#include <QDir>
#include <QFileInfoList>
#include <QStack>

RecursiveFolderScanner::RecursiveFolderScanner(
    const QString &root, const QString &requestNamespace)
    : _root(root), _requestNamespace(requestNamespace) {
}

void RecursiveFolderScanner::run() {
    QStack<QDir> stack;
    stack.push(QDir(_root));
    qDebug() << "RecursiveFolderScanner: Starting folder scanning...";

    while (!stack.isEmpty() && !isCanceled()) {
        QDir currentDir = stack.pop();
        QStringList dirs = currentDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::NoSort);
        sortNamesNaturally(dirs);
        for (int i = 0; i < dirs.size() && !isCanceled(); ++i) {
            stack.push(QDir(currentDir.absoluteFilePath(dirs[i])));
        }

        QStringList images;
        QStringList files = currentDir.entryList(ThumbnailLoader::supportedFormats(), QDir::Files | QDir::NoDotAndDotDot, QDir::NoSort);
        sortNamesNaturally(files);
        for (int i = 0; i < files.size() && !isCanceled(); ++i) {
            QString filePath = currentDir.absoluteFilePath(files.at(i));
            if (!PersistentImageCache::hasImage(filePath)) {
                // qDebug() << "RecursiveFolderScanner: File added" << filePath;
                images.append(filePath);
            }
            // else {
                // qDebug() << "RecursiveFolderScanner: File already cached" << filePath;
            // }
        }
        if (!images.isEmpty()) {
            _foldersToDecode.append(images);
        }
        qDebug() << "RecursiveFolderScanner: Folder added" << currentDir.absolutePath();
    }

    qDebug() << "RecursiveFolderScanner: Folder scanning complete. Starting decode";
    for (QStringList &images : _foldersToDecode) {
        if (isCanceled()) {
            break;
        }
        emit scanImages(images);
    }

    qDebug() << "RecursiveFolderScanner: Complete";

    emit finished(this);
}
