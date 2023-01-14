#include "ViewerController.h"

#include "ThumbnailLoader.h"
#include "FileListModel.h"
#include "QmlImageProvider.h"
#include "QmlResourcesProvider.h"

#include <QQmlContext>
#include <QQmlEngine>
#include <QDir>
#include <QSettings>
#include <QScreen>
#include <QResizeEvent>
#include <QMoveEvent>
#include <QTimer>
#include <QClipboard>
#include <QGuiApplication>

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
    folder = folder.trimmed();
    if (folder.startsWith("\"")) {
        folder = folder.right(folder.size() - 1);
    }
    if (folder.endsWith("\"")) {
        folder = folder.left(folder.size() - 1);
    }
    folder = folder.trimmed();

    qDebug() << folder;
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
    clipboard->setText(QDir::toNativeSeparators(_fileListModel->fullPath(_fileListModel->index(index).data().toString())));
}

QWindow *ViewerController::mainWindow() const {
    return _mainWindow;
}

void ViewerController::setMainWindow(QWindow *mainWindow) {
    _mainWindow = mainWindow;
    _mainWindow->installEventFilter(this);
    _lastSize = _mainWindow->size();

    QSettings set;
    QVariant normalGeometry = set.value("normalGeometry");
    if (normalGeometry.isValid()) {
        _normalGeometry = normalGeometry.toRect();
    }
    else {
        QSize size(1250, 700);
        _normalGeometry = QRect(_mainWindow->screen()->virtualGeometry().center() - QPoint(size.width() / 2, size.height() / 2), size);
    }
    _mainWindow->setGeometry(_normalGeometry);

    QVariant windowState = set.value("windowState");
    if (windowState.isValid()) {
        _mainWindow->setWindowState(windowState.value<Qt::WindowState>());
    }
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

        if (event->type() == QEvent::Resize) {
            if (_mainWindow->windowState() == Qt::WindowNoState) {
                if (static_cast<QResizeEvent *>(event)->size() != _normalGeometry.size()) {
                    _normalGeometry.setSize(static_cast<QResizeEvent *>(event)->oldSize());
                }
            }
        }
        else if (event->type() == QEvent::Move) {
            if (_mainWindow->windowState() == Qt::WindowNoState) {
                if (static_cast<QMoveEvent *>(event)->pos() != _normalGeometry.topLeft()) {
                    _normalGeometry.moveTopLeft(static_cast<QMoveEvent *>(event)->oldPos());
                }
            }
        }
        else if (event->type() == QEvent::Close) {
            if (_mainWindow->windowState() == Qt::WindowNoState) {
                _normalGeometry = _mainWindow->geometry();
            }
            QSettings set;
            set.setValue("normalGeometry", _normalGeometry);
            set.setValue("windowState", _mainWindow->windowState());
        }
    }
    return false;
}
