#include "ViewerController.h"

#include "ThumbnailLoader.h"
#include "FileListModel.h"
#include "QmlImageProvider.h"

#include <QQmlContext>
#include <QQmlEngine>

ViewerController::ViewerController(QQmlEngine *engine)
    : QObject(engine) {
    ThumbnailLoader::init();

    engine->rootContext()->setContextProperty("viewerController", this);

    _fileListModel = new FileListModel(engine);
    engine->rootContext()->setContextProperty("fileListModel", _fileListModel);

    QmlImageProvider *imageProvider = new QmlImageProvider("thumbnails", _fileListModel);
    engine->addImageProvider("thumbnails", imageProvider);
}

void ViewerController::doCd() {
    //    _fileListModel->cd("C:\\Users\\xs\\Documents\\____");
    _fileListModel->cd("B:\\_Photos\\[2022.01.29] Irina's DR in Gosti");
}
