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

    engine->rootContext()->setContextProperty("viewerController", this);

    _fileListModel = new FileListModel(engine);
    engine->rootContext()->setContextProperty("fileListModel", _fileListModel);

    QmlImageProvider *imageProvider = new QmlImageProvider("thumbnails", _fileListModel);
    engine->addImageProvider("thumbnails", imageProvider);

    QmlResourcesProvider *resourcesProvider = new QmlResourcesProvider("resources");
    engine->addImageProvider("resources", resourcesProvider);
}

void ViewerController::doCd() {
    //    _fileListModel->cd("C:\\Users\\xs\\Documents\\____");
//    QString path = "C:\\Users\\xs\\Documents\\____\\new";
    QString path = "B:\\_Photos\\[2022.01.29] Irina's DR in Gosti";
//    QString path = "B:\\_Photos\\"; //[2021.12.29] New Year in Rostum\\pro\\1"; //"P:\\RAED";
//    QString path = "P:\\[Year 2015]\\[2015.03.14] Sputnik";
//    QString path = "C:\\";
    _currentPath = path;
    emit currentPathChanged();

    _fileListModel->cd(_currentPath);
}

void ViewerController::cd(QString folder) {
    if (_currentPath == "Computer") {
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
