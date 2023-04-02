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
    _canUp = true;
    _canBack = false;
    _canForward = false;
    _indexInHistory = -1;

    engine->rootContext()->setContextProperty("viewerController", this);

    _fileListModel = new FileListModel(this);
    engine->rootContext()->setContextProperty("fileListModel", _fileListModel);

    QmlImageProvider *imageProvider = new QmlImageProvider("thumbnails", _fileListModel);
    engine->addImageProvider("thumbnails", imageProvider);

    QmlResourcesProvider *resourcesProvider = new QmlResourcesProvider("resources");
    engine->addImageProvider("resources", resourcesProvider);
}

void ViewerController::cd(QString folder, bool changeHistory) {
    folder = folder.trimmed();
    if (folder.startsWith("\"")) {
        folder = folder.right(folder.size() - 1);
    }
    if (folder.endsWith("\"")) {
        folder = folder.left(folder.size() - 1);
    }
    folder = folder.trimmed();

    if (folder == "Computer\\" || folder == "Computer/" || folder == "Computer") {
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
    updateHistory(changeHistory);
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
    updateHistory(true);
    return indexToSelect;
}

bool ViewerController::canUp() const {
    return _canUp;
}

void ViewerController::back() {
    if (_indexInHistory > 0) {
        _indexInHistory--;
        cd(_history[_indexInHistory], false);
    }
}

bool ViewerController::canBack() const {
    return _canBack;
}

QStringList ViewerController::backMenu() const {
    if (_indexInHistory >= 0 && _indexInHistory <= _history.size() - 1) {
        QStringList backList = _history.first(_indexInHistory);
        for (int i = 0; i < backList.size(); i++) {
            QString fileName = QFileInfo(backList[i]).fileName();
            if (!fileName.isEmpty()) {
                backList[i] = fileName;
            }
        }
        std::reverse(backList.begin(), backList.end());
        return backList;
    }
    return QStringList();
}

void ViewerController::forward() {
    if (_indexInHistory < _history.size() - 1) {
        _indexInHistory++;
        cd(_history[_indexInHistory], false);
    }
}

bool ViewerController::canForward() const {
    return _canForward;
}

QStringList ViewerController::forwardMenu() const {
    if (_indexInHistory >= 0 && _indexInHistory <= _history.size() - 1) {
        QStringList forwardList = _history.last(_history.size() - _indexInHistory - 1);
        for (int i = 0; i < forwardList.size(); i++) {
            QString fileName = QFileInfo(forwardList[i]).fileName();
            if (!fileName.isEmpty()) {
                forwardList[i] = fileName;
            }
        }
        return forwardList;
    }
    return QStringList();
}

void ViewerController::jumpBack(int backIndex) {
    int index = _indexInHistory - backIndex - 1;
    if (index >= 0 && index <= _history.size() - 1) {
        _indexInHistory = index;
        cd(_history[_indexInHistory], false);
    }
}

void ViewerController::jumpForward(int forwardIndex) {
    int index = _indexInHistory + forwardIndex + 1;
    if (index >= 0 && index <= _history.size() - 1) {
        _indexInHistory = index;
        cd(_history[_indexInHistory], false);
    }
}

void ViewerController::prepareToClose() {
    QTimer::singleShot(0, this, [&] () {
        _fileListModel->prepareToClose();
    });
}

void ViewerController::clipboardCopyIndexName(int index) {
    QClipboard *clipboard = QGuiApplication::clipboard();
    clipboard->setText(_fileListModel->index(index).data().toString());
}

void ViewerController::clipboardCopyIndexFullPath(int index) {
    QClipboard *clipboard = QGuiApplication::clipboard();
    clipboard->setText(QDir::toNativeSeparators(_fileListModel->itemFromIndex(_fileListModel->index(index))->fullPath()));
}

void ViewerController::updateHistory(bool changeHistory) {
    if (changeHistory) {
        if (_history.size() - 1 != _indexInHistory) {
            _history.resize(_indexInHistory + 1);
        }
        _history.append(_currentPath);
        _indexInHistory = _history.size() - 1;
    }

    if ((_currentPath == "Computer") == _canUp) {
        _canUp = !_canUp;
        emit canUpChanged();
    }

    if ((_indexInHistory != 0) != _canBack) {
        _canBack = !_canBack;
        emit canBackChanged();
    }

    if ((_indexInHistory != _history.size() - 1) != _canForward) {
        _canForward = !_canForward;
        emit canForwardChanged();
    }

    emit historyChanged();
}
