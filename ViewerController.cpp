#include "ViewerController.h"

#include "BackgroundInstance.h"
#include "ThumbnailLoader.h"
#include "FileListModel.h"
#include "QmlImageProvider.h"
#include "QmlAsyncImageProvider.h"
#include "QmlResourcesProvider.h"
#include "ProviderImageStore.h"
#include "SelectedImagesModel.h"
#include "GalleryViewModel.h"
#include "ImageModel.h"
#include "CacheViewer.h"
#include "TrayController.h"
#include "LaunchOptions.h"
#include "MacApplication.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlContext>
#include <QQmlEngine>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

#if defined(Q_OS_MACOS)
#include <CoreFoundation/CoreFoundation.h>

extern "C" {
OSStatus LSRegisterURL(CFURLRef inURL, Boolean inUpdate);
OSStatus LSSetDefaultRoleHandlerForContentType(CFStringRef inContentType, UInt32 inRole, CFStringRef inHandlerBundleID);
}
#endif

namespace
{
QString expandHomePath(const QString &path)
{
    if (path == QStringLiteral("~")) {
        return QDir::homePath();
    }

    if (path.startsWith(QStringLiteral("~/")) || path.startsWith(QStringLiteral("~\\"))) {
        return QDir::homePath() + path.mid(1);
    }

    return path;
}

#if defined(Q_OS_MACOS)
constexpr const char *kZoinGalleryBundleIdentifier = "gallery.zoin";
constexpr UInt32 kZoinGalleryLsRolesAll = 0xFFFFFFFF;

bool registerCurrentBundle()
{
    CFBundleRef bundle = CFBundleGetMainBundle();
    if (!bundle) {
        return false;
    }

    CFURLRef bundleUrl = CFBundleCopyBundleURL(bundle);
    if (!bundleUrl) {
        return false;
    }

    const OSStatus status = LSRegisterURL(bundleUrl, true);
    CFRelease(bundleUrl);
    return status == noErr;
}
#endif
}

ViewerController::ViewerController(QQmlEngine *engine)
    : QObject(engine) {
    ThumbnailLoader::init();
    _fileListModel = nullptr;
    _selectedImagesModel = nullptr;
    _galleryViewModel = nullptr;
    _canUp = true;
    _canBack = false;
    _canForward = false;
    _indexInHistory = -1;
    _savedContentY = -1;
    _savedCurrentIndex = -1;
    _trayController = nullptr;
    _backgroundInstance = nullptr;
    _backgroundMode = false;
    _pendingOpenInViewer = false;
    _explicitQuitRequested = false;

    engine->rootContext()->setContextProperty("viewerController", this);

    const auto providerImageStore =
        QSharedPointer<ProviderImageStore>::create();

    _fileListModel = new FileListModel(providerImageStore, this);
    engine->rootContext()->setContextProperty("fileListModel", _fileListModel);
    connect(_fileListModel, &FileListModel::directOpenReady, this, [this] (int index) {
        emit setCurrentIndex(mapSourceRowToViewRow(index));
    });
    connect(_fileListModel, &FileListModel::directOpenFailed, this,
            [this](const QString &) { clearPendingOpenInViewer(); });
    connect(_fileListModel, &FileListModel::panelReloaded, this, [this] (int index) {
        emit setCurrentIndex(mapSourceRowToViewRow(index));
    });

    _galleryViewModel = new GalleryViewModel(_fileListModel, this);
    engine->rootContext()->setContextProperty("galleryViewModel", _galleryViewModel);

    _selectedImagesModel = new SelectedImagesModel(
        _fileListModel, providerImageStore, this);
    engine->rootContext()->setContextProperty("selectedImagesModel", _selectedImagesModel);

    _imageModel = new ImageModel(_galleryViewModel);
    engine->rootContext()->setContextProperty("imageModel", _imageModel);

    QmlImageProvider *imageProvider = new QmlImageProvider(
        "thumbnails", providerImageStore);
    engine->addImageProvider("thumbnails", imageProvider);

    QmlAsyncImageProvider *asyncImageProvider =
        new QmlAsyncImageProvider("async", providerImageStore);
    engine->addImageProvider("async", asyncImageProvider);

    QmlResourcesProvider *resourcesProvider = new QmlResourcesProvider("resources");
    engine->addImageProvider("resources", resourcesProvider);

    qmlRegisterType<ImageInfoModel>("com.example.imagecache", 1, 0, "ImageInfoModel");
}

void ViewerController::cd(const QString &folder, bool changeHistory) {
    qDebug() << "CD" << folder;
    QString currentFolder = folder.trimmed();
    if (currentFolder.startsWith("\"")) {
        currentFolder = currentFolder.right(currentFolder.size() - 1);
    }
    if (currentFolder.endsWith("\"")) {
        currentFolder = currentFolder.left(currentFolder.size() - 1);
    }
    currentFolder = currentFolder.trimmed();
    currentFolder = expandHomePath(currentFolder);
    if (currentFolder.endsWith(":") && !currentFolder.contains("/") && !currentFolder.contains("\\")) {
        currentFolder.append("/");
    }

    if (!_fileListModel->fileListSourceAccessEnabled()) {
        if (currentFolder == "Computer\\" || currentFolder == "Computer/" || currentFolder == "Computer") {
            setCurrentPath("Computer");
        }
        else if (currentFolder.contains("\\") || currentFolder.contains("/")) {
            setCurrentPath(QDir(currentFolder).absolutePath());
        }
        else if (_currentPath == "Computer") {
            setCurrentPath(QDir(currentFolder).absolutePath());
        }
        else {
            setCurrentPath(QDir::cleanPath(QDir(_currentPath).absoluteFilePath(currentFolder)));
        }
        loadSavedState();
        updateHistory(changeHistory);
        return;
    }

    if (currentFolder == "Computer\\" || currentFolder == "Computer/" || currentFolder == "Computer") {
        setCurrentPath("Computer");
    }
    else if (currentFolder.contains("\\") || currentFolder.contains("/")) {
        QString newCurrentPath = QDir(currentFolder).absolutePath();
        if (QDir(newCurrentPath).exists()) {
            setCurrentPath(newCurrentPath);
        }
        else if (QFile::exists(newCurrentPath)) {
            setCurrentPath(QDir(QFileInfo(newCurrentPath).path()).absolutePath());
            QTimer::singleShot(10, this, [=] {
                emit setCurrentIndex(mapSourceRowToViewRow(_fileListModel->fileIndex(QFileInfo(newCurrentPath).fileName())));
            });
        }
    }
    else if (_currentPath == "Computer") {
        setCurrentPath(QDir(currentFolder).absolutePath());
    }
    else {
        QDir dir(_currentPath);
        if (dir.cd(currentFolder)) {
            setCurrentPath(dir.absolutePath());
        }
    }
    loadSavedState();
    updateHistory(changeHistory);
}

int ViewerController::up() {
    int indexToSelect = 0;
    if (_currentPath != "Computer") {
        QString previousFolder = QFileInfo(_currentPath).fileName();
        if (!_fileListModel->fileListSourceAccessEnabled()) {
            const QString currentPath = QDir::cleanPath(_currentPath);
            const QString parentPath = QDir::cleanPath(QDir(currentPath).absoluteFilePath(".."));
            if (parentPath != currentPath) {
                indexToSelect = setCurrentPath(parentPath, previousFolder);
            }
            else {
                indexToSelect = setCurrentPath("Computer", _currentPath);
            }
        }
        else {
            QDir dir(_currentPath);
            if (dir.cdUp()) {
                indexToSelect = setCurrentPath(dir.absolutePath(), previousFolder);
            }
            else {
                indexToSelect = setCurrentPath("Computer", _currentPath);
            }
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
        const int sourceIndex = mapViewRowToSourceRow(currentIndex);
        _history[_indexInHistory].currentIndex = sourceIndex != -1 ? sourceIndex : currentIndex;
    }
}

qreal ViewerController::savedContentY() const {
    return _savedContentY;
}

int ViewerController::savedCurrentIndex() const {
    return mapSourceRowToViewRow(_savedCurrentIndex);
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
    qInfo() << "[Shutdown] ViewerController::prepareToClose scheduled";
    QTimer::singleShot(0, this, [&] () {
        qInfo() << "[Shutdown] ViewerController::prepareToClose running scheduled close";
        _selectedImagesModel->prepareToClose();
        _fileListModel->prepareToClose();
    });
}

void ViewerController::hideToTray() {
    qInfo() << "[Shutdown] ViewerController::hideToTray"
            << "hasTrayController" << (_trayController != nullptr)
            << "trayAvailable" << (_trayController ? _trayController->isAvailable() : false);
    if (_trayController && _trayController->isAvailable()) {
        _trayController->showTray();
    }
    else {
        qInfo() << "[Shutdown] ViewerController::hideToTray falling back to quitApplication";
        quitApplication();
    }
}

void ViewerController::quitApplication() {
    qInfo() << "[Shutdown] ViewerController::quitApplication begin"
            << "explicitQuitRequested" << _explicitQuitRequested
            << "hasBackgroundInstance" << (_backgroundInstance != nullptr)
            << "hasTrayController" << (_trayController != nullptr)
            << "hasFileListModel" << (_fileListModel != nullptr);
    if (!_explicitQuitRequested) {
        _explicitQuitRequested = true;
        emit explicitQuitRequestedChanged();
        qInfo() << "[Shutdown] ViewerController::quitApplication marked explicit quit";
    }
    if (QCoreApplication *app = QCoreApplication::instance()) {
        app->setProperty("explicitQuitRequested", true);
        qInfo() << "[Shutdown] ViewerController::quitApplication set app explicitQuitRequested property";
    }
    if (_backgroundInstance) {
        qInfo() << "[Shutdown] ViewerController::quitApplication stopping background server";
        _backgroundInstance->stopServer();
        qInfo() << "[Shutdown] ViewerController::quitApplication background server stop returned";
    }
    if (_trayController) {
        qInfo() << "[Shutdown] ViewerController::quitApplication hiding tray without restoring Dock";
        _trayController->hideTray(false);
        qInfo() << "[Shutdown] ViewerController::quitApplication tray hide returned";
    }
    if (_fileListModel) {
        if (_selectedImagesModel) {
            qInfo() << "[Shutdown] ViewerController::quitApplication calling SelectedImagesModel::prepareToClose";
            _selectedImagesModel->prepareToClose();
            qInfo() << "[Shutdown] ViewerController::quitApplication SelectedImagesModel::prepareToClose returned";
        }
        qInfo() << "[Shutdown] ViewerController::quitApplication calling FileListModel::prepareToClose";
        _fileListModel->prepareToClose();
        qInfo() << "[Shutdown] ViewerController::quitApplication FileListModel::prepareToClose returned";
    }
    else {
        qInfo() << "[Shutdown] ViewerController::quitApplication no file model, calling QCoreApplication::quit";
        QCoreApplication::quit();
    }
    qInfo() << "[Shutdown] ViewerController::quitApplication end";
}

bool ViewerController::backgroundMode() const {
    return _backgroundMode;
}

bool ViewerController::pendingOpenInViewer() const {
    return _pendingOpenInViewer;
}

bool ViewerController::explicitQuitRequested() const {
    return _explicitQuitRequested;
}

void ViewerController::applyDockIconPreference(bool windowVisible) {
    applyMacApplicationDockIconPolicy(windowVisible);
}

void ViewerController::setBackgroundMode(bool enabled) {
    _backgroundMode = enabled;
}

void ViewerController::setStartupFilePath(const QString &path) {
    _startupFilePath = normalizePathArgument(path);
}

void ViewerController::initializeBackgroundMode(QWindow *mainWindow) {
    _trayController = new TrayController(mainWindow, this);
    if (!_trayController->isAvailable()) {
        qWarning() << "System tray is not available; background mode will fall back to quitting on close";
    }
    else {
        connect(_trayController, &TrayController::quitRequested, this, &ViewerController::quitApplication);
        _trayController->showTray(false);
        qDebug() << "Background mode: system tray ready";
    }

    _backgroundInstance = new BackgroundInstance(this, this);
    if (!_backgroundInstance->startServer()) {
        qWarning() << "Failed to start background instance server:" << BackgroundInstance::serverName();
    }
    else {
        qDebug() << "Background mode: IPC server listening on" << BackgroundInstance::serverName();
    }
}

void ViewerController::openExternalFile(const QString &path) {
    const QString normalizedPath = normalizePathArgument(path);
    QFileInfo fileInfo(normalizedPath);
    if (fileInfo.isFile() && FileListModel::isImage(fileInfo.fileName())) {
        _pendingExternalFilePath = normalizedPath;
        _pendingOpenInViewer = true;
        emit pendingOpenInViewerChanged();
    }
    else {
        _pendingExternalFilePath.clear();
        _pendingOpenInViewer = false;
        emit pendingOpenInViewerChanged();
        cd(normalizedPath);
    }
    emit externalFileOpened();
}

void ViewerController::activateFromExternal() {
    emit externalActivateRequested();
}

void ViewerController::openPendingExternalFileInViewer(int viewerWidth, int viewerHeight) {
    if (_pendingExternalFilePath.isEmpty()) {
        return;
    }

    QString path = _pendingExternalFilePath;
    _pendingExternalFilePath.clear();
    openFileInViewer(path, viewerWidth, viewerHeight);
}

void ViewerController::clearPendingOpenInViewer() {
    if (_pendingOpenInViewer) {
        _pendingOpenInViewer = false;
        emit pendingOpenInViewerChanged();
    }
}

void ViewerController::clipboardCopyIndexName(int index) {
    index = mapViewRowToSourceRow(index);
    if (index < 0 || index > _fileListModel->rowCount() - 1) {
        return;
    }
    QClipboard *clipboard = QGuiApplication::clipboard();
    clipboard->setText(_fileListModel->index(index).data().toString());
}

void ViewerController::clipboardCopyIndexFullPath(int index) {
    index = mapViewRowToSourceRow(index);
    if (index < 0 || index > _fileListModel->rowCount() - 1) {
        return;
    }
    QClipboard *clipboard = QGuiApplication::clipboard();
    clipboard->setText(QDir::toNativeSeparators(_fileListModel->itemFromIndex(_fileListModel->index(index))->fullPath()));
}

QUrl ViewerController::indexUrl(int index) {
    index = mapViewRowToSourceRow(index);
    if (index < 0 || index > _fileListModel->rowCount() - 1) {
        return QUrl();
    }
    return QUrl::fromLocalFile(_fileListModel->itemFromIndex(_fileListModel->index(index))->fullPath());
}

QString ViewerController::makeDefaultImageViewer() {
#if defined(Q_OS_MACOS)
    registerCurrentBundle();

    CFStringRef bundleIdentifier = CFStringCreateWithCString(kCFAllocatorDefault,
                                                             kZoinGalleryBundleIdentifier,
                                                             kCFStringEncodingUTF8);
    if (!bundleIdentifier) {
        return QStringLiteral("Could not set default image viewer.");
    }

    CFStringRef imageContentType = CFSTR("public.image");
    const OSStatus status = LSSetDefaultRoleHandlerForContentType(imageContentType,
                                                                  kZoinGalleryLsRolesAll,
                                                                  bundleIdentifier);
    CFRelease(bundleIdentifier);

    if (status == noErr) {
        return QStringLiteral("ZoinGallery is now the default image viewer.");
    }

    qWarning() << "Failed to set default handler for public.image:" << status;
    return QStringLiteral("Could not set default image viewer.");
#else
    return QStringLiteral("Default image viewer setup is available on macOS.");
#endif
}

void ViewerController::enterRecursiveView() {
    _fileListModel->enterRecursiveView();
}

void ViewerController::initialCd(int viewerWidth, int viewerHeight) {
    QSettings set;
    QString savedPath = set.value("currentPath").toString();

    if (!_startupFilePath.isEmpty()) {
        QFileInfo startupFileInfo(_startupFilePath);
        if ((!_fileListModel->imageSourceAccessEnabled() || startupFileInfo.isFile())
            && FileListModel::isImage(startupFileInfo.fileName())) {
            _pendingOpenInViewer = true;
            emit pendingOpenInViewerChanged();
            openFileInViewer(_startupFilePath, viewerWidth, viewerHeight);
        }
        else {
            _pendingOpenInViewer = false;
            emit pendingOpenInViewerChanged();
            cd(_startupFilePath);
        }
    }
    else if (!savedPath.isEmpty()) {
        cd(savedPath);
    }
    else {
        cd(QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).first());
    }
}

void ViewerController::openFileInViewer(const QString &path, int viewerWidth, int viewerHeight, bool changeHistory) {
    const QString imagePath = _fileListModel->imageSourceAccessEnabled()
        ? normalizePathArgument(path)
        : normalizePathArgumentWithoutFileAccess(path);

    QFileInfo fileInfo(imagePath);
    if ((_fileListModel->imageSourceAccessEnabled() && !fileInfo.isFile())
        || !FileListModel::isImage(fileInfo.fileName())) {
        clearPendingOpenInViewer();
        cd(imagePath, changeHistory);
        return;
    }

    QString folderPath = fileInfo.dir().absolutePath();
    if (_currentPath != folderPath) {
        _currentPath = folderPath;
        QSettings set;
        set.setValue("currentPath", _currentPath);
        emit currentPathChanged();
    }

    loadSavedState();
    updateHistory(changeHistory);
    _fileListModel->openImageDirectly(fileInfo.absoluteFilePath(), viewerWidth, viewerHeight);
}

/*QColor ViewerController::adjustHSL(const QColor &color, qreal h, qreal s, qreal l) {
    qreal newHue = color.hsvHueF() + h;
    return QColor::fromHsvF(newHue - qFloor(newHue),
                            qBound(0.0, color.hsvSaturationF() + s, 1.0),
                            qBound(0.0, color.valueF() + l, 1.0));
}*/

QColor ViewerController::adjustHSL(const QColor &color, qreal h, qreal s, qreal l) {
    qreal newHue = color.hslHueF() + h;
    return QColor::fromHslF(newHue - qFloor(newHue),
                            qBound(0.0, color.hslSaturationF() + s, 1.0),
                            qBound(0.0, color.lightnessF() + l, 1.0));
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

int ViewerController::setCurrentPath(const QString &newPath, const QString &itemToSelect) {
    const bool pathChanged = _currentPath != newPath;
    _currentPath = newPath;
    int indexToSelect = _fileListModel->cd(_currentPath, itemToSelect);
    QSettings set;
    set.setValue("currentPath", _currentPath);
    if (pathChanged) {
        emit currentPathChanged();
    }
    return mapSourceRowToViewRow(indexToSelect);
}

int ViewerController::mapViewRowToSourceRow(int viewRow) const {
    if (!_galleryViewModel) {
        return viewRow;
    }
    return _galleryViewModel->mapToSourceRow(viewRow);
}

int ViewerController::mapSourceRowToViewRow(int sourceRow) const {
    if (!_galleryViewModel || sourceRow < 0) {
        return sourceRow;
    }
    const int viewRow = _galleryViewModel->mapFromSourceRow(sourceRow);
    if (viewRow != -1) {
        return viewRow;
    }
    return _galleryViewModel->nearestVisibleRow(sourceRow);
}
