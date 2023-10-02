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
#include <QSettings>

ViewerController::ViewerController(QQmlEngine *engine)
    : QObject(engine) {
    ThumbnailLoader::init();
    _fileListModel = nullptr;
    _canUp = true;
    _canBack = false;
    _canForward = false;
    _indexInHistory = -1;
    _savedContentY = -1;
    _savedCurrentIndex = -1;

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
        setCurrentPath("Computer");
    }
    else if (folder.contains("\\") || folder.contains("/")) {
        QString newCurrentPath = QDir::toNativeSeparators(QDir(folder).absolutePath());
        if (QDir(newCurrentPath).exists()) {
            setCurrentPath(newCurrentPath);
        }
        else if (QFile::exists(newCurrentPath)) {
            setCurrentPath(QDir::toNativeSeparators(QDir(QFileInfo(newCurrentPath).path()).absolutePath()));
            QTimer::singleShot(10, this, [=] {
                emit setCurrentIndex(_fileListModel->fileIndex(QFileInfo(newCurrentPath).fileName()));
            });
        }
    }
    else if (_currentPath == "Computer") {
        setCurrentPath(QDir::toNativeSeparators(QDir(folder).absolutePath()));
    }
    else {
        QDir dir(_currentPath);
        if (dir.cd(folder)) {
            setCurrentPath(QDir::toNativeSeparators(dir.absolutePath()));
        }
    }
    loadSavedState();
    updateHistory(changeHistory);
}

int ViewerController::up() {
    int indexToSelect = 0;
    if (_currentPath != "Computer") {
        QString previousFolder = QFileInfo(_currentPath).fileName();
        QDir dir(_currentPath);
        if (dir.cdUp()) {
            setCurrentPath(QDir::toNativeSeparators(dir.absolutePath()), previousFolder);
        }
        else {
            setCurrentPath("Computer", _currentPath);
        }
    }
    loadSavedState();
    updateHistory(true);
    return indexToSelect;
}

bool ViewerController::canUp() const {
    return _canUp;
}

void ViewerController::saveCurrentState(qreal contentY, int currentIndex) {
    if (_indexInHistory >= 0 && _indexInHistory <= _history.size() - 1) {
        _history[_indexInHistory].contentY = contentY;
        _history[_indexInHistory].currentIndex = currentIndex;
    }
}

qreal ViewerController::savedContentY() const {
    return _savedContentY;
}

int ViewerController::savedCurrentIndex() const {
    return _savedCurrentIndex;
}

void ViewerController::back() {
    if (_indexInHistory > 0) {
        _indexInHistory--;
        cd(_history[_indexInHistory].path, false);
    }
}

bool ViewerController::canBack() const {
    return _canBack;
}

QStringList ViewerController::backMenu() const {
    if (_indexInHistory >= 0 && _indexInHistory <= _history.size() - 1) {
        QList<HistoryEntity> backHistoryList = _history.first(_indexInHistory);
        QStringList backList;
        for (int i = 0; i < backHistoryList.size(); i++) {
            QString fileName = QFileInfo(backHistoryList[i].path).fileName();
            if (!fileName.isEmpty()) {
                backList.append(fileName);
            }
            else {
                backList.append(backHistoryList[i].path);
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
        cd(_history[_indexInHistory].path, false);
    }
}

bool ViewerController::canForward() const {
    return _canForward;
}

QStringList ViewerController::forwardMenu() const {
    if (_indexInHistory >= 0 && _indexInHistory <= _history.size() - 1) {
        QList<HistoryEntity> forwardHistoryList = _history.last(_history.size() - _indexInHistory - 1);
        QStringList forwardList;
        for (int i = 0; i < forwardHistoryList.size(); i++) {
            QString fileName = QFileInfo(forwardHistoryList[i].path).fileName();
            if (!fileName.isEmpty()) {
                forwardList.append(fileName);
            }
            else {
                forwardList.append(forwardHistoryList[i].path);
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
        cd(_history[_indexInHistory].path, false);
    }
}

void ViewerController::jumpForward(int forwardIndex) {
    int index = _indexInHistory + forwardIndex + 1;
    if (index >= 0 && index <= _history.size() - 1) {
        _indexInHistory = index;
        cd(_history[_indexInHistory].path, false);
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

QUrl ViewerController::indexUrl(int index) {
    if (index < 0) {
        return QUrl();
    }
    return QUrl::fromLocalFile(_fileListModel->itemFromIndex(_fileListModel->index(index))->fullPath());
}

void ViewerController::updateHistory(bool changeHistory) {
    if (changeHistory) {
        if (_history.size() - 1 != _indexInHistory) {
            _history.resize(_indexInHistory + 1);
        }
        _history.append(HistoryEntity(_currentPath));
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

void ViewerController::loadSavedState() {
    _savedContentY = -1;
    _savedCurrentIndex = -1;
    for (int i = _indexInHistory; i >= 0; i--) {
        if (_history[i].path == _currentPath) {
            _savedContentY = _history[i].contentY;
            _savedCurrentIndex = _history[i].currentIndex;
            break;
        }
    }
}

void ViewerController::setCurrentPath(const QString &newPath, const QString &itemToSelect) {
    _currentPath = newPath;
    emit currentPathChanged();
    _fileListModel->cd(_currentPath, itemToSelect);
    QSettings set;
    set.setValue("currentPath", _currentPath);
}
