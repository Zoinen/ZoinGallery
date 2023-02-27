#include "ViewerController.h"

#include "ThumbnailLoader.h"
#include "FileListModel.h"
#include "QmlImageProvider.h"
#include "QmlResourcesProvider.h"

#include <QQmlContext>
#include <QQmlEngine>
#include <QDir>
#include <QTimer>
#include <QClipboard>
#include <QGuiApplication>

ViewerController::ViewerController(QQmlEngine *engine)
    : QObject(engine) {
    ThumbnailLoader::init();
    _fileListModel = nullptr;

    engine->rootContext()->setContextProperty("viewerController", this);

    _fileListModel = new FileListModel(this);
    engine->rootContext()->setContextProperty("fileListModel", _fileListModel);

    QmlImageProvider *imageProvider = new QmlImageProvider("thumbnails", _fileListModel);
    engine->addImageProvider("thumbnails", imageProvider);

    QmlResourcesProvider *resourcesProvider = new QmlResourcesProvider("resources");
    engine->addImageProvider("resources", resourcesProvider);
}

void ViewerController::cd(QString folder) {
    folder = folder.trimmed();
    if (folder.startsWith("\"")) {
        folder = folder.right(folder.size() - 1);
    }
    if (folder.endsWith("\"")) {
        folder = folder.left(folder.size() - 1);
    }
    folder = folder.trimmed();

    if (folder == "Computer\\") {
        _currentPath = "Computer";
        emit currentPathChanged();
        _fileListModel->cd(_currentPath, "");
    }
    else if (folder.contains("\\") || folder.contains("/")) {
        QString newCurrentPath = QDir::toNativeSeparators(QDir(folder).absolutePath());
        if (QDir(newCurrentPath).exists()) {
            _currentPath = newCurrentPath;
            emit currentPathChanged();
            _fileListModel->cd(_currentPath);
        }
        else if (QFile::exists(newCurrentPath)) {
            _currentPath = QDir::toNativeSeparators(QDir(QFileInfo(newCurrentPath).path()).absolutePath());
            emit currentPathChanged();
            _fileListModel->cd(_currentPath);
            QTimer::singleShot(10, this, [=] {
                emit setCurrentIndex(_fileListModel->fileIndex(QFileInfo(newCurrentPath).fileName()));
            });
        }
    }
    else if (_currentPath == "Computer") {
        QDir dir(folder);
        _currentPath = QDir::toNativeSeparators(dir.absolutePath());
        emit currentPathChanged();
        _fileListModel->cd(_currentPath);
    }
    else {
        QDir dir(_currentPath);
        if (dir.cd(folder)) {
            _currentPath = QDir::toNativeSeparators(dir.absolutePath());
            emit currentPathChanged();
            _fileListModel->cd(_currentPath);
        }
    }
}

int ViewerController::up() {
    int indexToSelect = 0;
    if (_currentPath != "Computer") {
        QString previousFolder = QFileInfo(_currentPath).fileName();
        QDir dir(_currentPath);
        if (dir.cdUp()) {
            _currentPath = QDir::toNativeSeparators(dir.absolutePath());
            emit currentPathChanged();
            indexToSelect = _fileListModel->cd(_currentPath, previousFolder);
        }
        else {
            previousFolder = _currentPath;
            _currentPath = "Computer";
            emit currentPathChanged();
            indexToSelect = _fileListModel->cd(_currentPath, previousFolder);
        }
    }
    return indexToSelect;
}

void ViewerController::prepareToClose() {
    _fileListModel->prepareToClose();
}

void ViewerController::clipboardCopyIndexName(int index) {
    QClipboard *clipboard = QGuiApplication::clipboard();
    clipboard->setText(_fileListModel->index(index).data().toString());
}

void ViewerController::clipboardCopyIndexFullPath(int index) {
    QClipboard *clipboard = QGuiApplication::clipboard();
    clipboard->setText(QDir::toNativeSeparators(_fileListModel->itemFromIndex(_fileListModel->index(index))->fullPath()));
}
