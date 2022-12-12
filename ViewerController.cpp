#include "ViewerController.h"

#include "ThumbnailLoader.h"
#include "FileListModel.h"
#include "QmlImageProvider.h"
#include "QmlResourcesProvider.h"

#include <QQmlContext>
#include <QQmlEngine>
#include <QDir>

ViewerController::ViewerController(QQmlEngine *engine)
    : QObject(engine) {
    ThumbnailLoader::init();
    _fileListModel = nullptr;
    _mainWindow = nullptr;

    engine->rootContext()->setContextProperty("viewerController", this);

    _fileListModel = new FileListModel(engine);
    engine->rootContext()->setContextProperty("fileListModel", _fileListModel);

    QmlImageProvider *imageProvider = new QmlImageProvider("thumbnails", _fileListModel);
    engine->addImageProvider("thumbnails", imageProvider);

    QmlResourcesProvider *resourcesProvider = new QmlResourcesProvider("resources");
    engine->addImageProvider("resources", resourcesProvider);
}

void ViewerController::cd(QString folder) {
//    qDebug() << folder;
    if (folder.contains("\\") || folder.contains("/")) {
        _currentPath = QDir::toNativeSeparators(QDir(folder).absolutePath());
        emit currentPathChanged();
        _fileListModel->cd(_currentPath);
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

QWindow *ViewerController::mainWindow() const {
    return _mainWindow;
}

void ViewerController::setMainWindow(QWindow *mainWindow) {
    _mainWindow = mainWindow;
    _mainWindow->installEventFilter(this);
    _lastSize = _mainWindow->size();
}

bool ViewerController::eventFilter(QObject *watched, QEvent *event) {
    if (watched == _mainWindow) {
        if (event->type() == QEvent::NonClientAreaMouseButtonPress) {
            _leftButtonPressed = true;
            _lastSize = _mainWindow->size();
        }
        else if (event->type() == QEvent::NonClientAreaMouseButtonRelease && _leftButtonPressed) {
            _leftButtonPressed = false;
            if (_lastSize != _mainWindow->size()) {
                emit mainWindowResized();
            }
        }
        else if (event->type() == QEvent::Resize && !_leftButtonPressed) {
            if (_lastSize != _mainWindow->size()) {
                emit mainWindowResized();
            }
            _lastSize = _mainWindow->size();
        }
    }
    return false;
}
