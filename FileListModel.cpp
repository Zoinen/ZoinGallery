#include "FileListModel.h"
#include "DecodeManager.h"
#include "LaunchOptions.h"
#include "NaturalSort.h"
#include "PersistentFolderCache.h"
#include "PersistentImageCache.h"
#include "QmlAsyncImageProvider.h"
#include "ThumbnailLoader.h"

#include <QCoreApplication>
#include <QClipboard>
#include <QDir>
#include <QDebug>
#include <QFile>
#include <QSet>
#include <QFileInfo>
#include <QDeadlineTimer>
#include <QGuiApplication>
#include <QDragEnterEvent>
#include <QDrag>
#include <QDropEvent>
#include <QMimeData>
#include <QCursor>
#include <QFontMetricsF>
#include <QPainter>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>
#include <QRegularExpression>
#include <QStack>
#include <QStandardPaths>
#include <QSettings>
#include <QDateTime>
#include <QUrl>

#include <chrono>
#include <utility>
#ifdef Q_OS_WIN
#include <QtCore/qt_windows.h>
#endif
using namespace std::chrono_literals;

namespace {
constexpr const char *ImageCacheModeSettingsKey = "Cache/imageUsageMode";
constexpr const char *FileListCacheModeSettingsKey = "Cache/fileListUsageMode";

QVariantMap fileOperationResult(bool success, const QString &title,
                                const QString &message, int action = Qt::IgnoreAction,
                                int count = 0, const QString &destination = {}) {
    return {
        {QStringLiteral("success"), success},
        {QStringLiteral("title"), title},
        {QStringLiteral("message"), message},
        {QStringLiteral("action"), action},
        {QStringLiteral("count"), count},
        {QStringLiteral("destinationFolder"), destination},
    };
}

bool removePath(const QString &path) {
    const QFileInfo info(path);
    if (info.isDir() && !info.isSymLink()) {
        return !QDir(path).isRoot() && QDir(path).removeRecursively();
    }
    return QFile::remove(path);
}

bool copyPath(const QString &source, const QString &destination,
              QString *error) {
    const QFileInfo sourceInfo(source);
    if (!sourceInfo.exists() && !sourceInfo.isSymLink()) {
        *error = QStringLiteral("Source no longer exists: %1").arg(source);
        return false;
    }

    if (!sourceInfo.isDir() || sourceInfo.isSymLink()) {
        if (QFile::copy(source, destination)) {
            return true;
        }
        *error = QStringLiteral("Could not copy %1 to %2").arg(source, destination);
        return false;
    }

    if (!QDir().mkdir(destination)) {
        *error = QStringLiteral("Could not create folder: %1").arg(destination);
        return false;
    }
    const QDir sourceDir(source);
    const QFileInfoList entries = sourceDir.entryInfoList(
        QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    for (const QFileInfo &entry : entries) {
        const QString childDestination = QDir(destination).filePath(entry.fileName());
        if (!copyPath(entry.absoluteFilePath(), childDestination, error)) {
            removePath(destination);
            return false;
        }
    }
    return true;
}

bool movePath(const QString &source, const QString &destination,
              QString *error) {
    const QFileInfo info(source);
    const bool renamed = info.isDir() && !info.isSymLink()
        ? QDir().rename(source, destination)
        : QFile::rename(source, destination);
    if (renamed) {
        return true;
    }

    if (!copyPath(source, destination, error)) {
        return false;
    }
    if (removePath(source)) {
        return true;
    }
    removePath(destination);
    *error = QStringLiteral("Could not remove source after copying: %1").arg(source);
    return false;
}

bool isSameOrChildPath(const QString &candidate, const QString &parent) {
    const QString cleanCandidate = QDir::cleanPath(candidate);
    QString cleanParent = QDir::cleanPath(parent);
    if (!cleanParent.endsWith(QDir::separator())) {
        cleanParent += QDir::separator();
    }
    const Qt::CaseSensitivity sensitivity =
#ifdef Q_OS_WIN
        Qt::CaseInsensitive;
#else
        Qt::CaseSensitive;
#endif
    return cleanCandidate.compare(QDir::cleanPath(parent), sensitivity) == 0 ||
           cleanCandidate.startsWith(cleanParent, sensitivity);
}

#ifdef Q_OS_WIN
QPixmap systemCursorPixmap(LPCWSTR cursorId, qreal dpr) {
    const HCURSOR cursor = LoadCursorW(nullptr, cursorId);
    if (!cursor) {
        return {};
    }

    ICONINFO iconInfo{};
    BITMAP cursorBitmap{};
    if (!GetIconInfo(cursor, &iconInfo)) {
        return {};
    }
    const HBITMAP sizeBitmap = iconInfo.hbmColor
        ? iconInfo.hbmColor : iconInfo.hbmMask;
    const bool haveBitmapInfo = sizeBitmap &&
        GetObjectW(sizeBitmap, sizeof(BITMAP), &cursorBitmap) == sizeof(BITMAP);
    if (iconInfo.hbmColor) {
        DeleteObject(iconInfo.hbmColor);
    }
    if (iconInfo.hbmMask) {
        DeleteObject(iconInfo.hbmMask);
    }
    if (!haveBitmapInfo) {
        return {};
    }
    const int cursorWidth = cursorBitmap.bmWidth;
    const int cursorHeight = iconInfo.hbmColor
        ? cursorBitmap.bmHeight : cursorBitmap.bmHeight / 2;

    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = cursorWidth;
    bitmapInfo.bmiHeader.biHeight = -cursorHeight;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    void *pixels = nullptr;
    const HDC screenDc = GetDC(nullptr);
    const HBITMAP bitmap = CreateDIBSection(
        screenDc, &bitmapInfo, DIB_RGB_COLORS, &pixels, nullptr, 0);
    const HDC memoryDc = CreateCompatibleDC(screenDc);
    ReleaseDC(nullptr, screenDc);
    if (!bitmap || !memoryDc || !pixels) {
        if (memoryDc) {
            DeleteDC(memoryDc);
        }
        if (bitmap) {
            DeleteObject(bitmap);
        }
        return {};
    }

    const HGDIOBJ previousBitmap = SelectObject(memoryDc, bitmap);
    memset(pixels, 0, size_t(cursorWidth) * size_t(cursorHeight) * 4);
    DrawIconEx(memoryDc, 0, 0, cursor, cursorWidth, cursorHeight,
               0, nullptr, DI_NORMAL);
    const QImage image(static_cast<uchar *>(pixels), cursorWidth, cursorHeight,
                       cursorWidth * 4, QImage::Format_ARGB32_Premultiplied);
    QPixmap pixmap = QPixmap::fromImage(image.copy());
    SelectObject(memoryDc, previousBitmap);
    DeleteDC(memoryDc);
    DeleteObject(bitmap);
    pixmap.setDevicePixelRatio(dpr);
    return pixmap;
}

QFont windowsDragPillFont() {
    QFont font = QGuiApplication::font();
    font.setPixelSize(13);
    font.setWeight(QFont::DemiBold);
    return font;
}

QSizeF windowsDragPillSize(Qt::DropAction action) {
    const QString label = action == Qt::CopyAction
        ? QStringLiteral("Copy") : QStringLiteral("Move");
    const QFontMetricsF metrics(windowsDragPillFont());
    constexpr qreal horizontalPadding = 11.0;
    constexpr qreal iconWidth = 14.0;
    constexpr qreal iconTextSpacing = 7.0;
    constexpr qreal verticalPadding = 6.0;
    return QSizeF(qCeil(horizontalPadding + iconWidth + iconTextSpacing +
                        metrics.horizontalAdvance(label) + horizontalPadding),
                  qCeil(qMax(iconWidth, metrics.height()) +
                        verticalPadding * 2.0));
}

QPixmap windowsDragCursorPixmap(qreal dpr, const QSizeF &previewSize,
                                const QPointF &hotSpot,
                                Qt::DropAction action) {
    const bool showPill = action == Qt::CopyAction ||
                          action == Qt::MoveAction;
    const QString label = action == Qt::CopyAction
        ? QStringLiteral("Copy") : QStringLiteral("Move");
    const QFont labelFont = windowsDragPillFont();
    const QSizeF copyPillSize = windowsDragPillSize(Qt::CopyAction);
    const QSizeF movePillSize = windowsDragPillSize(Qt::MoveAction);
    const QSizeF pillSize = action == Qt::CopyAction
        ? copyPillSize : movePillSize;
    const qreal pillWidth = pillSize.width();
    const qreal pillHeight = pillSize.height();
    constexpr qreal shadowExtent = 7.0;
    const QSizeF effectivePreview = previewSize.isEmpty()
        ? QSizeF(52.0, 52.0) : previewSize;
    // QWindowsOleDropSource draws this action cursor with its origin at the
    // pointer. Offset the pill so it sits below the separate drag preview.
    const qreal pillCenterX = -hotSpot.x() +
                              effectivePreview.width() / 2.0;
    const qreal copyPillX = qMax(shadowExtent,
        pillCenterX - copyPillSize.width() / 2.0);
    const qreal movePillX = qMax(shadowExtent,
        pillCenterX - movePillSize.width() / 2.0);
    const qreal pillX = action == Qt::CopyAction
        ? copyPillX : movePillX;
    const qreal pillY = qMax(36.0,
        -hotSpot.y() + effectivePreview.height() + 8.0);
    // Every action cursor must have exactly the same outer geometry. Qt's
    // Windows backend combines this canvas with the drag preview into one
    // HCURSOR; differing sizes make the preview jump and flash on transitions.
    const qreal commonRight = qMax(copyPillX + copyPillSize.width(),
                                   movePillX + movePillSize.width());
    const qreal commonPillHeight = qMax(copyPillSize.height(),
                                        movePillSize.height());
    const QSizeF logicalSize(
        qMax(32.0, commonRight + shadowExtent),
        pillY + commonPillHeight + shadowExtent + 2.0);

    QPixmap pixmap(qCeil(logicalSize.width() * dpr),
                   qCeil(logicalSize.height() * dpr));
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.scale(dpr, dpr);

    // Use the actual Windows cursor artwork; only the action pill is custom.
    const QPixmap systemCursor = systemCursorPixmap(
        action == Qt::IgnoreAction ? IDC_NO : IDC_ARROW, dpr);
    if (!systemCursor.isNull()) {
        painter.drawPixmap(QPointF(0, 0), systemCursor);
    }

    if (showPill) {
        const QRectF pillRect(pillX, pillY, pillWidth, pillHeight);
        // Approximate a soft Windows-style elevation shadow without baking a
        // harsh, visibly offset duplicate of the pill.
        painter.setPen(Qt::NoPen);
        for (int layer = 7; layer >= 1; --layer) {
            const qreal spread = layer * 0.7;
            const QRectF shadowRect = pillRect
                .adjusted(-spread, -spread * 0.45,
                          spread, spread * 1.25)
                .translated(0, 1.5);
            painter.setBrush(QColor(0, 0, 0, 3 + (7 - layer) * 2));
            painter.drawRoundedRect(shadowRect,
                                    pillHeight / 3.0 + spread,
                                    pillHeight / 3.0 + spread);
        }

        painter.setPen(QPen(QColor(0, 0, 0, 60), 1.0));
        painter.setBrush(QColor(250, 250, 250, 246));
        painter.drawRoundedRect(pillRect, pillHeight / 3.0,
                                pillHeight / 3.0);

        constexpr qreal horizontalPadding = 11.0;
        constexpr qreal iconWidth = 14.0;
        constexpr qreal iconTextSpacing = 7.0;
        const QPointF iconCenter(
            pillX + horizontalPadding + iconWidth / 2.0,
            pillY + pillHeight / 2.0);
        painter.setPen(QPen(QColor(25, 25, 25), 2.0, Qt::SolidLine,
                            Qt::RoundCap, Qt::RoundJoin));
        if (action == Qt::CopyAction) {
            painter.drawLine(iconCenter + QPointF(-4.5, 0),
                             iconCenter + QPointF(4.5, 0));
            painter.drawLine(iconCenter + QPointF(0, -4.5),
                             iconCenter + QPointF(0, 4.5));
        }
        else {
            painter.drawLine(iconCenter + QPointF(-5.0, 0),
                             iconCenter + QPointF(4.5, 0));
            painter.drawLine(iconCenter + QPointF(0.5, -4.0),
                             iconCenter + QPointF(4.5, 0));
            painter.drawLine(iconCenter + QPointF(0.5, 4.0),
                             iconCenter + QPointF(4.5, 0));
        }

        painter.setFont(labelFont);
        painter.setPen(QColor(25, 25, 25));
        const qreal textX = pillX + horizontalPadding + iconWidth +
                            iconTextSpacing;
        const QRectF textRect(
            textX, pillY,
            pillRect.right() - horizontalPadding - textX, pillHeight);
        painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, label);
    }

    // QWindowsOleDropSource consumes action cursors as physical pixels; it
    // does not apply the drag preview's DPR scaling to a custom cursor.
    pixmap.setDevicePixelRatio(1.0);
    return pixmap;
}

#endif

}

FileListModel::FileListModel(
    QSharedPointer<ProviderImageStore> providerImageStore, QObject *parent)
    : QAbstractItemModel(parent),
      _providerImageStore(std::move(providerImageStore)),
      _viewerImageCache(QStringLiteral("main-viewer-"),
                        _providerImageStore) {
    qApp->installEventFilter(this);
    _lastId = 0;
    _currentViewIndex = -1;

    _decodeManager = new DecodeManager(this);
    QSettings settings;
    _imageCacheMode = cacheUsageModeFromInt(
        settings.value(ImageCacheModeSettingsKey, static_cast<int>(CacheUsageMode::On)).toInt());
    _fileListCacheMode = cacheUsageModeFromInt(
        settings.value(FileListCacheModeSettingsKey, static_cast<int>(CacheUsageMode::On)).toInt());
    _decodeManager->setImageCacheMode(_imageCacheMode);
    _decodeManager->setFileListCacheMode(_fileListCacheMode);

    connect(_decodeManager, &DecodeManager::viewerRunnerCanceled, this, [&] (const QString &path) {
        qDebug() << "REMOVE CANCELLED RUNNER???" << path;
        _viewerImageCache.removeIncomplete(path);
    });

    connect(_decodeManager, &DecodeManager::runningTasksChanged, [&] (const QString &runningTasks, const QStringList &tasksInfo) {
        if (runningTasksDebug()) {
            QFile f(QString("C:\\tmp\\log\\%1.txt").arg(QDateTime::currentMSecsSinceEpoch()));
            f.open(QFile::WriteOnly);
            f.write(runningTasks.toLatin1() + "\n");
            QByteArray ba;
            for (QString task : tasksInfo) {
                ba.append(task.toUtf8());
                ba.append("\n");
            }
            f.write(ba);
        }
        emit runningTasksChanged(runningTasks, tasksInfo);
    });

    connect(_decodeManager, &DecodeManager::imageInfoReady, this, [&] (const ImageInfo &result) {
        if (result.directOpenGeneration && result.directOpenGeneration != _directOpen.generation) {
            return;
        }
        auto it = _fileToItem.find(result.path);
        // qDebug() << "INFO RECEIVED" << result.path << result.imageSize;
        if (it != _fileToItem.end()) {
            ImageFile *item = it.value();
            ImageInfo itemInfo = result;
            if (itemInfo.fileSize < 0) {
                itemInfo.fileSize = item->fileSize();
            }
            if (!itemInfo.lastModified.isValid()) {
                itemInfo.lastModified = item->lastModified();
            }
            item->setFullSize(rotateToOrientation(itemInfo.imageSize, itemInfo.orientation));
            item->setInfo(itemInfo);
            if (!result.imageSize.isValid()) {
                handleDirectOpenImageInfo(itemInfo);
                return;
            }

            QModelIndex modelIndex = index(item->index(), 0, indexFromItem(item->imageFileParent()));
            if (!modelIndex.isValid()) {
                qDebug() << "Invalid model index" << item->index() << item->imageFileParent() << item->fullPath();
                return;
            }
            QList<int> roles = {ImageFullSizeRole};
            if (result.isLast) {
                roles.append(TimeToFlushRole);
            }
            emit dataChanged(modelIndex, modelIndex, roles);

            if (result.isFromEmbeddedView) {
                decodeImages({imageDecodeRequestFromEmbeddedImageInfo(itemInfo)});
            }

            handleDirectOpenImageInfo(itemInfo);
        }
        else {
            qDebug() << "ZZ NOT FOUND" << result.path << _fileToItem.keys();
        }
    });

    connect(_decodeManager, &DecodeManager::imagesInfoReady, this, [&] (const QList<ImageInfo> &results) {
        if (!results.size()) {
            return;
        }
        // qDebug() << "ZZ on DecodeManager::imagesInfoReady" << results.size();
        QList<ImageInfo> currentResults;
        currentResults.reserve(results.size());
        for (const ImageInfo &result : results) {
            if (!result.directOpenGeneration || result.directOpenGeneration == _directOpen.generation) {
                currentResults.append(result);
            }
        }
        if (!currentResults.size()) {
            return;
        }

        QList<ImageDecodeRequest> requests;

        int flushIndex = -1;
        int minIndex = 1000000;
        int maxIndex = -1;
        bool foundCurrentItem = false;
        // TODO: If differents parent come in one signal here it will mess everything up
        QModelIndex parent;

        for (const ImageInfo &result : currentResults) {
            auto it = _fileToItem.find(result.path);
            // qDebug() << "INFO RECEIVED" << result.path << result.imageSize << result.orientation;
            if (it != _fileToItem.end()) {
                foundCurrentItem = true;
                ImageFile *item = it.value();
                ImageInfo itemInfo = result;
                if (itemInfo.fileSize < 0) {
                    itemInfo.fileSize = item->fileSize();
                }
                if (!itemInfo.lastModified.isValid()) {
                    itemInfo.lastModified = item->lastModified();
                }
                item->setFullSize(rotateToOrientation(itemInfo.imageSize, itemInfo.orientation));
                item->setInfo(itemInfo);

                minIndex = qMin(minIndex, item->index());
                maxIndex = qMax(maxIndex, item->index());
                parent = indexFromItem(item->imageFileParent());

                if (result.isLast) {
                    flushIndex = item->index();
                }

                if (result.isFromEmbeddedView) {
                    requests.append(imageDecodeRequestFromEmbeddedImageInfo(itemInfo));
                }

                handleDirectOpenImageInfo(itemInfo);
            }
            else {
                qDebug() << "ZZ NOT FOUND" << result.path << _fileToItem.keys();
            }
        }

        if (!foundCurrentItem) {
            return;
        }

        QModelIndex minModelIndex = index(minIndex, 0, parent);
        QModelIndex maxModelIndex = index(maxIndex, 0, parent);
        if (!minModelIndex.isValid() || !maxModelIndex.isValid()) {
            qDebug() << "Invalid model index" << minModelIndex << maxModelIndex << parent;
            return;
        }
        emit dataChanged(minModelIndex, maxModelIndex, {ImageFullSizeRole});

        if (flushIndex != -1) {
            QModelIndex flushModelIndex = index(flushIndex, 0, parent);
            emit dataChanged(flushModelIndex, flushModelIndex, {TimeToFlushRole});
        }

        if (requests.size()) {
            decodeImages(requests);
        }
    });

    connect(_decodeManager, &DecodeManager::imageReady, this, [&] (const ImageDecodeRequest &request,
                                                                   const QImage &image, const DecodedImageInfo &decodedInfo) {
        if (request.info.directOpenGeneration && request.info.directOpenGeneration != _directOpen.generation) {
            return;
        }
        // image.save(QString("c:/tmp/zg/%1.png").arg(QFileInfo(request.info.path).fileName()));
        // qDebug() << "ZZ IMAGE READEY" << request.info.path << request.info.imageSize << request.targetSize << image.size();
        auto it = _fileToItem.find(request.info.path);
        if (it != _fileToItem.end()) {
            ImageFile *item = it.value();
            if (request.viewerRequest) {
                // qDebug() << "ZZ Viewer image came" << request.info.path << isFromCache << image.size() << item->image.size();
            }
            if (!request.viewerRequest && decodedInfo.isFromCache &&
                (image.width() <= item->image().width() ||
                 image.height() <= item->image().height())) {
                handleDirectOpenImageReady(request, image, decodedInfo);
                return;
            }
            if (request.viewerRequest) {
                const ViewerImageCache::StoredImage storedImage =
                    _viewerImageCache.storeDecodedImage(request, image,
                                                        decodedInfo);
                if (storedImage.accepted) {
                    emit viewerImageCacheChanged(item->index());
                    if (item->index() == _currentViewIndex) {
                        emit viewerImageIdUrlChanged(storedImage.url,
                                                     storedImage.level);
                    }
                }
            }
            else {
                item->setImage(image);
                item->setIsCachedThumbnail(decodedInfo.isFromCache);
                updateImageId(item);
            }

            handleDirectOpenImageReady(request, image, decodedInfo);
        }
        else {
            qDebug() << "Decoded image is not found in model" << request.info.path;
        }
    });

    connect(_decodeManager, &DecodeManager::folderListReady, this,
            [&] (const QString &path, const QList<FileInfo> &subfiles, bool isFromCache) {
        if (!subfiles.size()) {
            return;
        }

        _folderImagePaths.removeOne(path);
        auto it = _fileToItem.find(path);
        if (it != _fileToItem.end()) {
            ImageFile *item = it.value();
            ImageInfo folderInfo = item->info();
            folderInfo.isCached = isFromCache;
            item->setInfo(folderInfo);

            QList<ImageFile *> subImages;
            subImages.reserve(subfiles.size());
            QStringList imagePaths;
            imagePaths.reserve(subfiles.size());
            for (int i = 0; i < subfiles.size(); i++) {
                ImageFile *subItem = createFileItem(path, subfiles.at(i).name,
                                                    subfiles.at(i).lastModified, subfiles.at(i).fileSize);
                subItem->setImageFileParent(item);
                subItem->setIndex(subImages.size());
                subImages.append(subItem);
                imagePaths.append(QDir(path).absoluteFilePath(subfiles.at(i).name));
            }

            beginInsertRows(indexFromItem(item), 0, subImages.size() - 1);
            item->setSubfiles(subImages);
            endInsertRows();
            if (_folderModels.contains(item->index())) {
                _folderModels[item->index()]->resetModel();
            }

            QModelIndex modelIndex = index(item->index(), 0, indexFromItem(item->imageFileParent()));
            emit dataChanged(modelIndex, modelIndex, {FolderViewRole});

            _decodeManager->readImagesInfo(imagePaths, true);
        }
    });

    _selectionSaveTimer.setSingleShot(true);
    _selectionSaveTimer.setInterval(200);
    connect(&_selectionSaveTimer, &QTimer::timeout, this, []() {
        PersistentSelectionCache::dumpDb();
    });
    refreshAvailableSelectionCounts();
}

QHash<int, QByteArray> FileListModel::roleNames() const {
    QHash<int,QByteArray> names;
    // names[Qt::DisplayRole] = "displayRole";
    names[ImageIdUrlRole] = "imageIdUrlRole";
    names[SelectedRole] = "selectedRole";
    names[SelectionGroupIdRole] = "selectionGroupIdRole";
    names[SelectionGroupColorRole] = "selectionGroupColorRole";
    names[ImageFileRole] = "imageFileRole";
    names[FolderRole] = "folderRole";
    names[IsImageRole] = "isImageRole";
    names[LastModifiedRole] = "lastModifiedRole";
    names[FileSizeRole] = "fileSizeRole";
    return names;
}

int FileListModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid()) {
        ImageFile *imageFile = itemFromIndex(parent);
        if (imageFile) {
            return imageFile->subfiles().size();
        }
        return 0;
    }
    return _items.size();
}

QVariant FileListModel::data(const QModelIndex &index, int role) const {
    ImageFile *imageFile = itemFromIndex(index);
    if (imageFile) {
        if (role == ImageIdUrlRole) {
            return imageFile->imageIdUrl();
        }
        else if (role == FolderRole) {
            return imageFile->isFolder();
        }
        else if (role == IsImageRole) {
            return imageFile->isImage();
        }
        else if (role == ImageFullSizeRole) {
            return imageFile->fullSize();
        }
        else if (role == ImageFileRole) {
            return QVariant::fromValue(imageFile);
        }
        else if (role == FolderViewRole) {
            return imageFile->subfiles().size() != 0;
        }
        else if (role == SelectedRole) {
            return imageFile->isSelected();
        }
        else if (role == SelectionGroupIdRole) {
            return imageFile->selectionGroupId();
        }
        else if (role == SelectionGroupColorRole) {
            return imageFile->selectionGroupColor();
        }
        else if (role == LastModifiedRole) {
            return imageFile->lastModified();
        }
        else if (role == FileSizeRole) {
            return imageFile->fileSize();
        }
    }
    return QVariant();
}

QModelIndex FileListModel::index(int row, int column, const QModelIndex &parent) const {
    if (hasIndex(row, column, parent)) {
        if (parent.isValid()) {
            ImageFile *imageFile = itemFromIndex(parent);
            if (row < imageFile->subfiles().size()) {
                return createIndex(row, column, imageFile->subfiles().at(row));
            }
        }
        else if (row < _items.size()) {
            return createIndex(row, column, _items.at(row));
        }
    }

    // Invalid index, root element
    return QModelIndex();
}

QModelIndex FileListModel::parent(const QModelIndex &child) const {
    ImageFile *imageFile = itemFromIndex(child);
    if (!imageFile || !imageFile->imageFileParent()) {
        return QModelIndex();
    }

    return index(imageFile->imageFileParent()->index(), 0, QModelIndex());
}

int FileListModel::columnCount(const QModelIndex &parent) const {
    return 1;
}

void FileListModel::prepareToClose() {
    qInfo() << "[Shutdown] FileListModel::prepareToClose begin"
            << "alreadyClosing" << _isClosing
            << "items" << _items.size()
            << "viewerImages" << _viewerImageCache.viewerImageCount()
            << "fullSizeViewerImages"
            << _viewerImageCache.fullSizeImageCount();
    if (_isClosing) {
        qInfo() << "[Shutdown] FileListModel::prepareToClose already closing, calling QCoreApplication::exit(0)";
        QCoreApplication::exit(0);
        return;
    }
    _isClosing = true;

    qInfo() << "[Shutdown] FileListModel::prepareToClose stopping async image provider";
    QmlAsyncImageProvider::prepareToClose();
    qInfo() << "[Shutdown] FileListModel::prepareToClose async image provider stopped";
    qInfo() << "[Shutdown] FileListModel::prepareToClose dumping selection cache";
    _selectionSaveTimer.stop();
    PersistentSelectionCache::dumpDb();
    qInfo() << "[Shutdown] FileListModel::prepareToClose stopping decode manager";
    _decodeManager->prepareToClose();
    qInfo() << "[Shutdown] FileListModel::prepareToClose decode manager stopped";
    qInfo() << "[Shutdown] FileListModel::prepareToClose calling QCoreApplication::exit(0)";
    QCoreApplication::exit(0);
    qInfo() << "[Shutdown] FileListModel::prepareToClose end";
}

int FileListModel::cd(const QString &path, const QString &itemToSelect) {
    const bool hadDirectOpenPath = !_directOpen.path.isEmpty();
    _directOpen.generation++;
    _directOpen.stage = DirectOpenStage::None;
    _directOpen.path.clear();
    _directOpen.pendingNeighborInfoPaths.clear();
    _directOpen.pendingNeighborDecodePaths.clear();
    if (hadDirectOpenPath) {
        emit directOpenPathChanged();
    }

    _root = path;

    beginResetModel();
    cleanupModelBeforeCd();
    int indexToSelect = populateFolderItems(path, itemToSelect);
    loadSelectionStatesForVisibleItems();
    endResetModel();

    startRegularFolderWork();

    return indexToSelect;
}

int FileListModel::populateFolderItems(const QString &path, const QString &itemToSelect) {
    int indexToSelect = 0;
    QList<FileInfo> entries;
    if (!folderEntries(path, entries)) {
        return indexToSelect;
    }

    if (path == "Computer") {
        for (const FileInfo &drive : entries) {
            ImageFile *item = new ImageFile(this);
            item->setFileName(drive.name);
            item->setIsFolder(true);
            item->setIsImage(false);
            item->setIconPath("qrc:/resources/DriveIcon.svg");
            item->setInfo(ImageInfo{
                .path = item->fullPath(),
                .lastModified = drive.lastModified,
                .fileSize = 0,
            });
            item->setIndex(_items.size());
            _items.append(item);

            if (item->fileName() == itemToSelect) {
                indexToSelect = _items.size() - 1;
            }
        }
    }
    else {
        QList<FileInfo> folders;
        QList<FileInfo> files;
        for (const FileInfo &entry : entries) {
            (entry.isDirectory ? folders : files).append(entry);
        }
        sortFileInfosNaturally(folders);
        sortFileInfosNaturally(files);

        for (const FileInfo &folder : folders) {
            ImageFile *item = new ImageFile(this);
            item->setFolderPath(_root);
            item->setFileName(folder.name);
            item->setIsFolder(true);
            item->setIsImage(false);
            item->setIconPath("qrc:/resources/FolderIcon.svg");
            item->setInfo(ImageInfo{
                .path = item->fullPath(),
                .lastModified = folder.lastModified,
                .fileSize = 0,
            });
            item->setIndex(_items.size());
            _items.append(item);

            QString path = item->fullPath();

            _fileToItem.insert(path, item);
            _folderImagePaths.append(path);

            if (item->fileName() == itemToSelect) {
                indexToSelect = _items.size() - 1;
            }
        }

        for (const FileInfo &file : files) {
            ImageFile *item = createFileItem(_root, file.name, file.lastModified, file.fileSize);
            if (item->isImage()) {
                _imagePaths.append(item->fullPath());
            }
            item->setIndex(_items.size());
            _items.append(item);
        }
    }

    return indexToSelect;
}

bool FileListModel::folderEntries(const QString &path, QList<FileInfo> &entries) {
    if (cacheReadsEnabled(_fileListCacheMode)) {
        FolderInfo cachedFolder;
        if (PersistentFolderCache::retrieveFolder(path, cachedFolder)) {
            entries = cachedFolder.subfiles;
            return true;
        }
    }

    if (!sourceReadsEnabled(_fileListCacheMode)) {
        return false;
    }

    entries = readFolderEntries(path);
    if (cacheWritesEnabled(_fileListCacheMode)) {
        PersistentFolderCache::storeFolder(FolderInfo{path, entries});
    }
    return true;
}

QList<FileInfo> FileListModel::readFolderEntries(const QString &path) const {
    QList<FileInfo> entries;
    if (path == "Computer") {
        const auto drives = QDir::drives();
        entries.reserve(drives.size());
        for (const QFileInfo &drive : drives) {
            QString drivePath = drive.path();
            if (drivePath.endsWith("/") && !drivePath.startsWith("/")) {
                drivePath.chop(1);
            }
            entries.append(FileInfo{
                .name = drivePath,
                .lastModified = drive.lastModified(),
                .fileSize = 0,
                .isDirectory = true,
            });
        }
        return entries;
    }

    QDir dir(path);
    const auto sourceEntries = dir.entryInfoList(
        QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden | QDir::System, QDir::NoSort);
    entries.reserve(sourceEntries.size());
    for (const QFileInfo &entry : sourceEntries) {
        entries.append(FileInfo{
            .name = entry.fileName(),
            .lastModified = entry.lastModified(),
            .fileSize = entry.isDir() ? 0 : entry.size(),
            .isDirectory = entry.isDir(),
        });
    }
    return entries;
}

void FileListModel::startRegularFolderWork() {
    _decodeManager->readImagesInfo(_imagePaths, false);
    _decodeManager->readFolderList(_folderImagePaths, 16); // TODO: FIX!
}

QString FileListModel::rootPath() const {
    return _root;
}

const ImageFile *FileListModel::itemForImageId(const QString &imageId) {
    auto it = _imageIdToItem.find(imageId);
    if (it != _imageIdToItem.end()) {
        return *it;
    }
    return nullptr;
}

QString FileListModel::generateNewId() {
    QString id = QString::number(_lastId);
    _lastId++;
    return id;
}

void FileListModel::updateImageId(ImageFile *item) {
    const QString imageId = item->imageIdUrl().section('/', -1);
    if (!imageId.isEmpty()) {
        _imageIdToItem.remove(imageId);
        _providerImageStore->remove(imageId);
    }
    const QString newImageId = generateNewId();
    _imageIdToItem.insert(newImageId, item);
    _providerImageStore->publish(newImageId, item->image());
    item->setImageId(newImageId);

    QModelIndex modelIndex = index(item->index(), 0, indexFromItem(item->imageFileParent()));
    emit dataChanged(modelIndex, modelIndex, {ImageIdUrlRole});
}

ImageFile *FileListModel::createFileItem(const QString &folderPath, const QString &fileName,
                                         const QDateTime &lastModified, qint64 fileSize) {
    ImageFile *item = new ImageFile(this);
    item->setFolderPath(folderPath);
    item->setFileName(fileName);
    item->setIsFolder(false);
    ImageInfo info = {
        .path = item->fullPath(),
        .lastModified = lastModified,
        .fileSize = fileSize,
    };
    item->setInfo(info);

    if (isImage(item->fileName())) {
        item->setIsImage(true);
        QString lowerFileName = item->fileName().toLower();
        item->setIconPath("qrc:/resources/ImageIcon.svg");
//                updateImageId(item);
        QString path = item->fullPath();
        _fileToItem.insert(path, item);
    }
    else {
        item->setIsImage(false);
        item->setIconPath("qrc:/resources/FileIcon.svg");
    }
    return item;
}

void FileListModel::cleanupModelBeforeCd() {
    cancelAllRunners();
    clearModelData(true);
}

void FileListModel::clearModelData(bool clearViewerData) {
    if (clearViewerData) {
        // Viewer
        _viewerImageCache.clear();
        emit viewerReset();
    }
    _currentViewIndex = -1;
    _selectionPreviewActive = false;
    _selectionPreviewSnapshot.clear();

    for (auto it = _folderModels.begin(); it != _folderModels.end(); ++it) {
        it.value()->deleteLater();
    }
    _folderModels.clear();

    _fileToItem.clear();
    _providerImageStore->remove(_imageIdToItem.keys());
    _imageIdToItem.clear();
    _folderImagePaths.clear();
    _imagePaths.clear();
    for (int i = 0; i < _items.size(); i++) {
        delete _items[i];
    }
    _items.clear();
}

ImageDecodeRequest FileListModel::imageDecodeRequestFromEmbeddedImageInfo(const ImageInfo &info) const {
    QSize thumbnailSize = _folderViewImageSize;
    QSize resultSize = rotateToOrientation(info.imageSize, info.orientation);
    if (!_folderViewImageSize.width()) { // CalcLayoutSingleRow
        thumbnailSize = QSize(resultSize.width() * (qreal(_folderViewImageSize.height()) / resultSize.height()), _folderViewImageSize.height());
        // qDebug() << "ZZ CalcLayoutSingleRow" << result.path << thumbnailSize;
    }
    else { // CalcLayoutGrid
        thumbnailSize = resultSize.scaled(_folderViewImageSize, Qt::KeepAspectRatio);
        // qDebug() << "ZZ CalcLayoutGrid" << _folderViewImageSize << result.path << thumbnailSize << result.imageSize << _folderViewImageSize << result.orientation;
    }
    return ImageDecodeRequest{
        .info = info,
        .targetSize = thumbnailSize,
        .viewerRequest = false,
        .checkCache = info.isCached
    };
}

ImageFile *FileListModel::itemFromIndex(const QModelIndex &index) {
    return static_cast<ImageFile*>(index.internalPointer());
}

QModelIndex FileListModel::indexFromItem(const ImageFile *item) const {
    if (!item) {
        return QModelIndex();
    }
    return index(item->index(), 0, indexFromItem(item->imageFileParent()));
}

QAbstractItemModel *FileListModel::folderModel(int index_) {
    auto it = _folderModels.find(index_);
    if (it == _folderModels.end()) {
        RootProxyModel *proxy = new RootProxyModel(this);
        proxy->setRoot(_items[index_]);
        proxy->setSourceModel(this);
        _folderModels[index_] = proxy;
        return proxy;
    }
    return *it;
}

struct RecursiveFolderInfo {
    int level;                     // Nesting level of the folder
    QString path;                  // Absolute path of the folder
    QString lastInGroup;           // String where each character represents a level: '1' for last, '0' for not last

    RecursiveFolderInfo(int lvl, QString pth, QString lastGroup)
        : level(lvl), path(pth), lastInGroup(lastGroup) {}
};

QList<RecursiveFolderInfo> getAllSubfoldersWithNestingLevel(const QString &startDir) {
    QList<RecursiveFolderInfo> allFoldersWithLevels;         // List to store folders with their nesting levels and boolean string
    QStack<RecursiveFolderInfo> dirs;                        // Stack to manage directories
    dirs.push(RecursiveFolderInfo(0, startDir, ""));        // Start with the initial directory, marked as last in its (non-existent) group

    while (!dirs.isEmpty()) {
        RecursiveFolderInfo dirInfo = dirs.pop();

        // Add the current directory to the list
        allFoldersWithLevels.append(dirInfo);

        // Get a list of all subdirectories in the current directory
        QStringList subDirs = QDir(dirInfo.path).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::NoSort);
        sortNamesNaturally(subDirs);
        for (int i = subDirs.count() - 1; i >= 0; --i) {
            QString subDir = subDirs.at(i);
            QString newPath = QDir(dirInfo.path).filePath(subDir);

            // Determine if this is the last subdirectory in the list
            QString isLast = (i == subDirs.count() - 1) ? "0" : "1";

            // Create a new boolean string for the next level based on the current dir's string
            QString newLastInGroup = dirInfo.lastInGroup + isLast;

            dirs.push(RecursiveFolderInfo(dirInfo.level + 1, newPath, newLastInGroup));
        }
    }

    return allFoldersWithLevels;
}


void FileListModel::enterRecursiveView() {
    if (!fileListSourceAccessEnabled()) {
        return;
    }

    _directOpen.generation++;
    _directOpen.stage = DirectOpenStage::None;
    _directOpen.pendingNeighborInfoPaths.clear();
    _directOpen.pendingNeighborDecodePaths.clear();

    beginResetModel();
    cleanupModelBeforeCd();

    // if (_root == "Computer") {
    //     for (const auto &drive : QDir::drives()) {
    //         ImageFile *item = new ImageFile();
    //         item->fileName = drive.path();
    //         item->isFolder = true;
    //         item->isImage = false;
    //         item->index = _items.size();
    //         item->iconPath = "qrc:/resources/DriveIcon.svg";
    //         _items.append(item);

    //         if (item->fileName == itemToSelect) {
    //             indexToSelect = _items.size() - 1;
    //         }
    //     }
    // }
    // else
    {
        QList<RecursiveFolderInfo> folders = getAllSubfoldersWithNestingLevel(_root);
        for (const auto &folder : folders) {
            QFileInfo info(folder.path);
            // qDebug() << folder.level << info.filePath() << info.fileName();
            ImageFile *item = new ImageFile(this);
            item->setFolderPath(info.dir().absolutePath());
            item->setFileName(info.fileName()); // QString("%1: %2").arg(folder.first).arg(folder.second);
            item->setIsFolder(true);
            item->setIsImage(false);
            item->setIconPath("qrc:/resources/FolderIcon.svg");
            item->setInfo(ImageInfo{
                .path = item->fullPath(),
                .lastModified = info.lastModified(),
                .fileSize = 0,
            });
            item->setIndex(_items.size());
            item->setNestingInfo(folder.lastInGroup);
            _items.append(item);

            QString path = item->fullPath();

            _fileToItem.insert(path, item);
            _folderImagePaths.append(path);
        }

        /*auto files = QDir(_root).entryInfoList(QDir::NoDotAndDotDot | QDir::Files | QDir::Hidden | QDir::System);
        for (const auto &file : files) {
            ImageFile *item = createFileItem(_root, file.fileName(), file.lastModified());
            if (item->isImage) {
                _imagePaths.append(item->fullPath());
            }
            item->index = _items.size();
            _items.append(item);
        }*/
    }
    loadSelectionStatesForVisibleItems();
    endResetModel();

    _decodeManager->readImagesInfo(_imagePaths, true);
    _decodeManager->readFolderList(_folderImagePaths, 16);
}

bool FileListModel::isImage(const QString &fileName) {
    return ThumbnailLoader::isFormatSupported(fileName);
}

void FileListModel::openImageDirectly(const QString &path, int width, int height) {
    const QString imagePath = imageSourceAccessEnabled()
        ? normalizePathArgument(path)
        : normalizePathArgumentWithoutFileAccess(path);

    QFileInfo fileInfo(imagePath);
    if ((imageSourceAccessEnabled() && !fileInfo.isFile()) || !isImage(fileInfo.fileName())) {
        return;
    }

    const QString folderPath = fileInfo.dir().absolutePath();
    const QString fileName = fileInfo.fileName();
    const QString fullPath = QDir(folderPath).absoluteFilePath(fileName);

    const int generation = _directOpen.generation + 1;
    _directOpen = DirectOpenState();
    _directOpen.generation = generation;
    _directOpen.stage = DirectOpenStage::WaitingInfo;
    _directOpen.path = fullPath;
    _directOpen.folderPath = folderPath;
    _directOpen.fileName = fileName;
    _directOpen.viewerSize = QSize(width, height);
    emit directOpenPathChanged();

    _decodeManager->cancelAllRunners();

    int existingIndex = -1;
    if (!_root.isEmpty() && _root != "Computer" &&
        !QString::compare(QDir(_root).absolutePath(), folderPath, Qt::CaseInsensitive)) {
        existingIndex = fileIndex(fileName);
    }

    _directOpen.sameFolder = existingIndex >= 0;
    if (_directOpen.sameFolder) {
        _directOpen.currentIndex = existingIndex;
        _currentViewIndex = existingIndex;
        emit viewerReset();
    }
    else {
        beginResetModel();
        clearModelData(true);
        _root = folderPath;

        ImageFile *item = createFileItem(folderPath, fileName,
                                         imageSourceAccessEnabled() ? fileInfo.lastModified() : QDateTime(),
                                         imageSourceAccessEnabled() ? fileInfo.size() : -1);
        item->setIndex(0);
        _items.append(item);
        if (item->isImage()) {
            _imagePaths.append(item->fullPath());
        }

        _directOpen.currentIndex = 0;
        _currentViewIndex = 0;
        loadSelectionStatesForVisibleItems();
        endResetModel();
    }

    auto itemIt = _fileToItem.find(fullPath);
    if (itemIt != _fileToItem.end() && itemIt.value()->fullSize().isValid() &&
        itemIt.value()->info().imageSize.isValid()) {
        ImageInfo info = itemIt.value()->info();
        info.directOpenGeneration = generation;
        handleDirectOpenImageInfo(info);
    }
    else {
        _decodeManager->readImagesInfo({fullPath}, false, generation);
    }
}

QString FileListModel::directOpenPath() const {
    return _directOpen.path;
}

bool FileListModel::isActiveDirectOpenInfo(const ImageInfo &info) const {
    return info.directOpenGeneration &&
           info.directOpenGeneration == _directOpen.generation &&
           _directOpen.stage != DirectOpenStage::None;
}

bool FileListModel::isActiveDirectOpenRequest(const ImageDecodeRequest &request) const {
    return request.info.directOpenGeneration &&
           request.info.directOpenGeneration == _directOpen.generation &&
           _directOpen.stage != DirectOpenStage::None;
}

void FileListModel::handleDirectOpenImageInfo(const ImageInfo &result) {
    if (!isActiveDirectOpenInfo(result)) {
        return;
    }

    if (_directOpen.stage == DirectOpenStage::WaitingInfo && result.path == _directOpen.path) {
        if (!result.imageSize.isValid()) {
            qWarning() << "Direct open metadata is invalid" << result.path << result.imageSize;
            _directOpen.stage = DirectOpenStage::None;
            return;
        }

        ImageInfo directOpenInfo = result;
        auto itemIt = _fileToItem.find(result.path);
        if (itemIt != _fileToItem.end()) {
            if (directOpenInfo.fileSize < 0) {
                directOpenInfo.fileSize = itemIt.value()->fileSize();
            }
            if (!directOpenInfo.lastModified.isValid()) {
                directOpenInfo.lastModified = itemIt.value()->lastModified();
            }
        }

        _directOpen.info = directOpenInfo;
        _directOpen.info.directOpenGeneration = _directOpen.generation;
        requestDirectOpenFitDecode();
        return;
    }

    if (_directOpen.stage == DirectOpenStage::WaitingNeighborInfo &&
        _directOpen.pendingNeighborInfoPaths.contains(result.path)) {
        _directOpen.pendingNeighborInfoPaths.remove(result.path);
        if (_directOpen.pendingNeighborInfoPaths.isEmpty()) {
            requestDirectOpenNeighborDecodes();
        }
    }
}

void FileListModel::requestDirectOpenFitDecode() {
    auto itemIt = _fileToItem.find(_directOpen.path);
    if (itemIt == _fileToItem.end()) {
        return;
    }

    ImageFile *item = itemIt.value();
    QSize targetSize = item->fullSize();
    if (!targetSize.isValid()) {
        targetSize = rotateToOrientation(_directOpen.info.imageSize, _directOpen.info.orientation);
    }
    ImageInfo info = _directOpen.info;
    info.directOpenGeneration = _directOpen.generation;
    const ImageDecodeRequest request = ViewerImageCache::makeRequest(
        info, targetSize, _directOpen.viewerSize);
    if (!request.targetSize.isValid()) {
        return;
    }

    _directOpen.stage = DirectOpenStage::WaitingFitDecode;
    _decodeManager->decodeImages({request});
}

void FileListModel::requestDirectOpenFullSizeDecode() {
    auto itemIt = _fileToItem.find(_directOpen.path);
    if (itemIt == _fileToItem.end()) {
        return;
    }

    ImageFile *item = itemIt.value();
    QSize targetSize = item->fullSize();
    if (!targetSize.isValid()) {
        targetSize = rotateToOrientation(_directOpen.info.imageSize, _directOpen.info.orientation);
    }
    if (!targetSize.isValid()) {
        return;
    }

    ImageInfo info = _directOpen.info;
    info.directOpenGeneration = _directOpen.generation;
    const ImageDecodeRequest request =
        ViewerImageCache::makeRequest(info, targetSize);
    _directOpen.stage = DirectOpenStage::WaitingFullDecode;
    _decodeManager->decodeImages({request});
}

bool FileListModel::handleDirectOpenImageReady(const ImageDecodeRequest &request, const QImage &image,
                                               const DecodedImageInfo &decodedInfo) {
    if (!isActiveDirectOpenRequest(request)) {
        return false;
    }

    if (request.info.path == _directOpen.path && _directOpen.stage == DirectOpenStage::WaitingFitDecode) {
        auto itemIt = _fileToItem.find(_directOpen.path);
        if (itemIt != _fileToItem.end() && !image.isNull() && itemIt.value()->imageIdUrl().isEmpty()) {
            itemIt.value()->setImage(image);
            itemIt.value()->setIsCachedThumbnail(decodedInfo.isFromCache);
            updateImageId(itemIt.value());
        }

        if (ViewerImageCache::isFullSizeRequest(request)) {
            _directOpen.stage = DirectOpenStage::WaitingFullDecode;
            populateFolderAfterDirectOpenFullDecode();
            requestDirectOpenNeighbors();
        }
        else {
            if (_directOpen.currentIndex >= 0) {
                emit directOpenReady(_directOpen.currentIndex);
            }
            requestDirectOpenFullSizeDecode();
        }
        return true;
    }

    if (request.info.path == _directOpen.path && _directOpen.stage == DirectOpenStage::WaitingFullDecode &&
        ViewerImageCache::isFullSizeRequest(request)) {
        populateFolderAfterDirectOpenFullDecode();
        requestDirectOpenNeighbors();
        return true;
    }

    if (_directOpen.stage == DirectOpenStage::WaitingNeighborDecode &&
        _directOpen.pendingNeighborDecodePaths.contains(request.info.path)) {
        _directOpen.pendingNeighborDecodePaths.remove(request.info.path);
        if (_directOpen.pendingNeighborDecodePaths.isEmpty()) {
            finishDirectOpenPriorityWork();
        }
        return true;
    }

    return false;
}

void FileListModel::populateFolderAfterDirectOpenFullDecode() {
    if (_directOpen.stage != DirectOpenStage::WaitingFullDecode) {
        return;
    }

    if (!_directOpen.sameFolder) {
        beginResetModel();
        clearModelData(false);
        _root = _directOpen.folderPath;
        populateFolderItems(_root, _directOpen.fileName);
        loadSelectionStatesForVisibleItems();

        auto targetIt = _fileToItem.find(_directOpen.path);
        if (targetIt != _fileToItem.end()) {
            targetIt.value()->setFullSize(rotateToOrientation(_directOpen.info.imageSize, _directOpen.info.orientation));
            targetIt.value()->setInfo(_directOpen.info);
        }
        endResetModel();

        const int targetIndex = fileIndex(_directOpen.fileName);
        _directOpen.currentIndex = targetIndex >= 0 ? targetIndex : 0;
    }

    _currentViewIndex = _directOpen.currentIndex;

    auto itemIt = _fileToItem.find(_directOpen.path);
    if (itemIt != _fileToItem.end()) {
        ImageFile *item = itemIt.value();
        item->setFullSize(rotateToOrientation(_directOpen.info.imageSize, _directOpen.info.orientation));
        item->setInfo(_directOpen.info);

        const ViewerImageCache::Entry viewerEntry =
            _viewerImageCache.entryForPath(_directOpen.path, false);
        if (!viewerEntry.image.isNull() && item->imageIdUrl().isEmpty()) {
            item->setImage(viewerEntry.image);
            item->setIsCachedThumbnail(
                viewerEntry.decodedInfo.isFromCache);
            updateImageId(item);
        }

        QModelIndex modelIndex = index(item->index(), 0, indexFromItem(item->imageFileParent()));
        if (modelIndex.isValid()) {
            emit dataChanged(modelIndex, modelIndex, {ImageFullSizeRole, ImageIdUrlRole});
        }
    }

    if (_directOpen.currentIndex >= 0) {
        emit directOpenReady(_directOpen.currentIndex);
        emitViewerImagesForCurrentIndex();
    }
}

QList<int> FileListModel::directOpenNeighborIndexes() const {
    QList<int> result;
    if (_directOpen.currentIndex < 0 || _directOpen.currentIndex >= _items.size()) {
        return result;
    }

    for (int i = _directOpen.currentIndex - 1; i >= 0; i--) {
        if (_items[i]->isImage()) {
            result.append(i);
            break;
        }
    }
    for (int i = _directOpen.currentIndex + 1; i < _items.size(); i++) {
        if (_items[i]->isImage()) {
            result.append(i);
            break;
        }
    }
    return result;
}

void FileListModel::requestDirectOpenNeighbors() {
    QList<int> neighborIndexes = directOpenNeighborIndexes();
    QStringList pathsNeedingInfo;
    _directOpen.pendingNeighborInfoPaths.clear();

    for (int index : neighborIndexes) {
        ImageFile *item = _items[index];
        if (!item->fullSize().isValid() || !item->info().imageSize.isValid()) {
            pathsNeedingInfo.append(item->fullPath());
            _directOpen.pendingNeighborInfoPaths.insert(item->fullPath());
        }
    }

    if (!_directOpen.pendingNeighborInfoPaths.isEmpty()) {
        _directOpen.stage = DirectOpenStage::WaitingNeighborInfo;
        _decodeManager->readImagesInfo(pathsNeedingInfo, false, _directOpen.generation);
        return;
    }

    requestDirectOpenNeighborDecodes();
}

QList<ImageDecodeRequest> FileListModel::directOpenViewerRequestsForIndexes(const QList<int> &indexes,
                                                                            QSet<QString> *queuedPaths) {
    QList<ImageDecodeRequest> requests;
    for (int index : indexes) {
        if (index < 0 || index >= _items.size() || !_items[index]->isImage()) {
            continue;
        }

        ImageFile *item = _items[index];
        const QString requestedPath = item->fullPath();
        ImageInfo info = item->info();
        info.directOpenGeneration = _directOpen.generation;
        const ImageDecodeRequest request =
            ViewerImageCache::makeRequest(
                info, item->fullSize(), _directOpen.viewerSize);
        if (!request.targetSize.isValid() ||
            !_viewerImageCache.needsDecode(request)) {
            continue;
        }

        requests.append(request);
        if (queuedPaths) {
            queuedPaths->insert(requestedPath);
        }
    }
    return requests;
}

void FileListModel::requestDirectOpenNeighborDecodes() {
    QSet<QString> queuedPaths;
    QList<ImageDecodeRequest> requests = directOpenViewerRequestsForIndexes(directOpenNeighborIndexes(), &queuedPaths);
    if (requests.isEmpty()) {
        finishDirectOpenPriorityWork();
        return;
    }

    _directOpen.pendingNeighborDecodePaths = queuedPaths;
    _directOpen.stage = DirectOpenStage::WaitingNeighborDecode;
    _decodeManager->decodeImages(requests);
}

void FileListModel::finishDirectOpenPriorityWork() {
    if (_directOpen.stage == DirectOpenStage::None) {
        return;
    }

    _directOpen.stage = DirectOpenStage::None;
    _directOpen.pendingNeighborInfoPaths.clear();
    _directOpen.pendingNeighborDecodePaths.clear();
    startRegularFolderWork();
}

void FileListModel::emitViewerImagesForCurrentIndex() {
    if (_currentViewIndex < 0 || _currentViewIndex >= _items.size()) {
        return;
    }

    if (!_items[_currentViewIndex]->imageIdUrl().isEmpty()) {
        emit viewerImageIdUrlChanged(_items[_currentViewIndex]->imageIdUrl(), 0);
    }
    const auto cachedImages = _viewerImageCache.cachedImagesForPath(
        _items[_currentViewIndex]->fullPath(), true);
    for (const auto &[url, level] : cachedImages) {
        emit viewerImageIdUrlChanged(url, level);
    }
}

void FileListModel::requestViewer(int index, int width, int height) {
    if (index < 0 || index >= _items.size()) {
        return;
    }

    _currentViewIndex = index;
    if (!_items[index]->imageIdUrl().isEmpty()) {
        emit viewerImageIdUrlChanged(_items[index]->imageIdUrl(), 0);
    }

    const ViewerImageCache::RequestPlan requestPlan =
        _viewerImageCache.planRequest(_items, index, QSize(width, height));
    for (const auto &[url, level] : requestPlan.cachedImages) {
        emit viewerImageIdUrlChanged(url, level);
    }
    _decodeManager->decodeImages(requestPlan.decodeRequests);
}

QString FileListModel::bestViewerImageUrlForIndex(int index) const {
    if (index < 0 || index >= _items.size()) {
        return QString();
    }
    return _viewerImageCache.bestImageUrl(_items[index]);
}

QImage FileListModel::viewerForImageId(const QString &imageId) {
    return _viewerImageCache.viewerImageForId(imageId);
}

QImage FileListModel::fullSizeViewerForImageId(const QString &imageId) {
    return _viewerImageCache.fullSizeImageForId(imageId);
}

void FileListModel::cancelAllRunners() {
    const bool hadDirectOpenPath = !_directOpen.path.isEmpty();
    _directOpen.generation++;
    _directOpen.stage = DirectOpenStage::None;
    _directOpen.path.clear();
    _directOpen.pendingNeighborInfoPaths.clear();
    _directOpen.pendingNeighborDecodePaths.clear();
    if (hadDirectOpenPath) {
        emit directOpenPathChanged();
    }
    _decodeManager->cancelAllRunners();
}

void FileListModel::cancelAllDecodeRunners() {
    // qDebug() << __FUNCTION__;
    _decodeManager->cancelAllDecodeRunners();
}

void FileListModel::cancelAllDecodeViewerRunners() {
    _decodeManager->cancelAllDecodeViewerRunners();
}

void FileListModel::decodeImages(const QList<ImageDecodeRequest> &requests) {
    _decodeManager->decodeImages(requests);
}

int FileListModel::fileIndex(const QString &fileName) const {
    for (int i = 0; i < _items.size(); i++) {
        if (!_items[i]->fileName().compare(fileName, Qt::CaseInsensitive)) {
            return i;
        }
    }
    return -1;
}

RootProxyModel::RootProxyModel(QObject *parent)
    : QAbstractProxyModel(parent) {
    _sourceRoot = nullptr;
}

void RootProxyModel::setRoot(ImageFile *root) {
    _sourceRoot = root;
}

void RootProxyModel::setSourceModel(QAbstractItemModel *sourceModel) {
    QAbstractProxyModel::setSourceModel(sourceModel);

    connect(sourceModel, &QAbstractItemModel::dataChanged, this,
            [&] (const QModelIndex &topLeft, const QModelIndex &bottomRight, const QList<int> &roles = QList<int>()) {
        if (FileListModel::itemFromIndex(topLeft.parent()) == _sourceRoot) {
            emit dataChanged(mapFromSource(topLeft), mapFromSource(bottomRight), roles);
        }
    });
}

QModelIndex RootProxyModel::index(int row, int column, const QModelIndex &parent) const {
    if (!_sourceRoot || !sourceModel()) {
        return QModelIndex();
    }
    return createIndex(row, column, _sourceRoot->subfiles().at(row));
}

QModelIndex RootProxyModel::parent(const QModelIndex &child) const {
    return QModelIndex();
}

int RootProxyModel::rowCount(const QModelIndex &parent) const {
    if (!_sourceRoot || !sourceModel()) {
        return 0;
    }
    return sourceModel()->rowCount(sourceModel()->indexFromItem(_sourceRoot));
}

int RootProxyModel::columnCount(const QModelIndex &parent) const {
    return 1;
}

QModelIndex RootProxyModel::mapToSource(const QModelIndex &proxyIndex) const {
    if (!_sourceRoot || !sourceModel()) {
        return QModelIndex();
    }

    return sourceModel()->index(proxyIndex.row(), proxyIndex.column(), sourceModel()->indexFromItem(_sourceRoot));
}

QModelIndex RootProxyModel::mapFromSource(const QModelIndex &sourceIndex) const {
    if (!_sourceRoot || !sourceModel()) {
        return QModelIndex();
    }

    if (sourceIndex.parent() != sourceModel()->indexFromItem(_sourceRoot)) {
        return QModelIndex();
    }

    return index(sourceIndex.row());
}

FileListModel *RootProxyModel::sourceModel() const {
    return static_cast<FileListModel *>(QAbstractProxyModel::sourceModel());
}

ImageFile *RootProxyModel::rootItem() const {
    return _sourceRoot;
}

void RootProxyModel::cancelAllRunners() {
    if (!sourceModel()) {
        return;
    }
    dynamic_cast<ThumbnailsRequestInterface *>(sourceModel())->cancelAllRunners();
}

void RootProxyModel::cancelAllDecodeRunners() {
    if (!sourceModel()) {
        return;
    }
    qDebug() << __FUNCTION__;
    dynamic_cast<ThumbnailsRequestInterface *>(sourceModel())->cancelAllDecodeRunners();
}

void RootProxyModel::decodeImages(const QList<ImageDecodeRequest> &requests) {
    if (!sourceModel()) {
        return;
    }
    dynamic_cast<ThumbnailsRequestInterface *>(sourceModel())->decodeImages(requests);
}

void RootProxyModel::resetModel() {
    beginResetModel();
    endResetModel();
}

void FileListModel::setFolderViewImageSize(int width, int height) {
    if (_folderViewImageSize.width() != width || _folderViewImageSize.height() != height) {
        _folderViewImageSize = QSize(width, height);
        qDebug() << "ZZ TARGET SIZE CHANGED" << _folderViewImageSize;
    }
}

void FileListModel::setFolderViewImageCount(int count) {

}

void FileListModel::startScanner() {
    _decodeManager->scan(_root);
}

bool FileListModel::runningTasksDebug() const {
    return _decodeManager->runningTasksDebug();
}

void FileListModel::setRunningTasksDebug(bool isRunningTasksDebug) {
    if (runningTasksDebug() == isRunningTasksDebug) {
        return;
    }
    _decodeManager->setRunningTasksDebug(isRunningTasksDebug);
    emit runningTasksDebugChanged();
}

int FileListModel::imageCacheMode() const {
    return static_cast<int>(_imageCacheMode);
}

void FileListModel::setImageCacheMode(int mode) {
    const CacheUsageMode newMode = cacheUsageModeFromInt(mode);
    if (_imageCacheMode == newMode) {
        return;
    }

    _decodeManager->cancelAllRunners();
    _imageCacheMode = newMode;
    _decodeManager->setImageCacheMode(newMode);
    QSettings().setValue(ImageCacheModeSettingsKey, static_cast<int>(newMode));
    emit imageCacheModeChanged();
    reloadPanelForCacheModeChange();
}

int FileListModel::fileListCacheMode() const {
    return static_cast<int>(_fileListCacheMode);
}

void FileListModel::setFileListCacheMode(int mode) {
    const CacheUsageMode newMode = cacheUsageModeFromInt(mode);
    if (_fileListCacheMode == newMode) {
        return;
    }

    _decodeManager->cancelAllRunners();
    _fileListCacheMode = newMode;
    _decodeManager->setFileListCacheMode(newMode);
    QSettings().setValue(FileListCacheModeSettingsKey, static_cast<int>(newMode));
    emit fileListCacheModeChanged();
    reloadPanelForCacheModeChange();
}

bool FileListModel::imageSourceAccessEnabled() const {
    return sourceReadsEnabled(_imageCacheMode);
}

bool FileListModel::fileListSourceAccessEnabled() const {
    return sourceReadsEnabled(_fileListCacheMode);
}

qint64 FileListModel::imageCacheSize() const {
    return _imageCacheSize;
}

QString FileListModel::imageCacheLocation() const {
    return PersistentImageCache::cacheLocation();
}

qint64 FileListModel::fileListCacheSize() const {
    return _fileListCacheSize;
}

QString FileListModel::fileListCacheLocation() const {
    return PersistentFolderCache::cacheLocation();
}

void FileListModel::clearImageCache() {
    cancelAllRunners();
    PersistentImageCache::clear();
    reloadPanelForCacheModeChange();
    refreshCacheInfo();
}

void FileListModel::clearFileListCache() {
    cancelAllRunners();
    PersistentFolderCache::clear();
    reloadPanelForCacheModeChange();
    refreshCacheInfo();
}

void FileListModel::refreshCacheInfo() {
    const qint64 imageSize = PersistentImageCache::cacheSize();
    const qint64 fileListSize = PersistentFolderCache::cacheSize();
    if (_imageCacheSize == imageSize && _fileListCacheSize == fileListSize) {
        return;
    }
    _imageCacheSize = imageSize;
    _fileListCacheSize = fileListSize;
    emit cacheInfoChanged();
}

QString FileListModel::itemNameToPreserve() const {
    if (_currentViewIndex >= 0 && _currentViewIndex < _items.size()) {
        return _items.at(_currentViewIndex)->fileName();
    }
    return _items.isEmpty() ? QString() : _items.first()->fileName();
}

void FileListModel::reloadPanelForCacheModeChange() {
    if (_root.isEmpty() || _isClosing) {
        return;
    }
    const QString itemToSelect = itemNameToPreserve();
    const int sourceIndex = cd(_root, itemToSelect);
    emit panelReloaded(sourceIndex);
}

void FileListModel::dumpCurrentImage() {
    if (_currentViewIndex < 0 || _currentViewIndex >= _items.size()) {
        qDebug() << "No valid current image to dump";
        return;
    }
    
    ImageFile *currentItem = _items.at(_currentViewIndex);
    QString imagePath = currentItem->fullPath();
    
    // Try to get full size viewer image first, fall back to regular viewer image
    QImage imageToSave;
    imageToSave =
        _viewerImageCache.entryForPath(imagePath, true).image;
    if (imageToSave.isNull()) {
        imageToSave =
            _viewerImageCache.entryForPath(imagePath, false).image;
    }
    
    if (imageToSave.isNull()) {
        qDebug() << "No viewer image available for current index";
        return;
    }
    
    // Get Pictures folder path
    QString picturesPath = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    QDir picturesDir(picturesPath);
    if (!picturesDir.exists()) {
        picturesDir.mkpath(".");
    }
    
    // Create filename with timestamp
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
    QString filename = QFileInfo(imagePath).baseName() + "_" + timestamp + ".png";
    QString savePath = picturesDir.filePath(filename);
    
    // Save the image
    if (imageToSave.save(savePath, "PNG")) {
        qDebug() << "Image saved to" << savePath;
    } else {
        qDebug() << "Failed to save image to" << savePath;
    }
}

int FileListModel::selectedCount() const {
    int count = 0;
    for (const ImageFile *item : _items) {
        if (item->isSelected()) {
            count++;
        }
    }
    return count;
}

bool FileListModel::isIndexSelected(int index_) const {
    if (index_ < 0 || index_ >= _items.size()) {
        return false;
    }
    return _items[index_]->isSelected();
}

QColor FileListModel::selectionGroupColorForIndex(int index_) const {
    if (index_ < 0 || index_ >= _items.size()) {
        return QColor();
    }
    return _items[index_]->selectionGroupColor();
}

void FileListModel::toggleSelection(int index_) {
    if (index_ < 0 || index_ >= _items.size()) {
        return;
    }
    setSelection(index_, !_items[index_]->isSelected());
}

void FileListModel::setSelection(int index_, bool selected) {
    if (index_ < 0 || index_ >= _items.size()) {
        return;
    }

    const QString containerKey = selectionContainerForItem(_items[index_]);
    ensureSelectionStateLoaded(containerKey);
    const QHash<QString, QString> previousSelection =
        _selectionStates[containerKey].selectedGroups;
    if (!setSelectionInState(index_, selected)) {
        return;
    }

    pushSelectionHistory(containerKey, selected ? QString("Select %1").arg(_items[index_]->fileName())
                                               : QString("Deselect %1").arg(_items[index_]->fileName()),
                         previousSelection);
    emitSelectionDataChanged(index_, index_, true);
}

void FileListModel::setPathSelection(const QString &path, bool selected) {
    const QFileInfo fileInfo(path);
    const QString containerKey = PersistentSelectionCache::normalizeContainerKey(fileInfo.absolutePath());
    ensureSelectionStateLoaded(containerKey);
    auto &state = _selectionStates[containerKey];
    const QHash<QString, QString> previousSelection = state.selectedGroups;
    const QString previousGroup = state.selectedGroups.value(fileInfo.fileName());
    const QString nextGroup = selected ? activeSelectionGroupId() : QString();
    if (previousGroup == nextGroup) {
        return;
    }

    if (selected) {
        state.selectedGroups.insert(fileInfo.fileName(), nextGroup);
    }
    else {
        state.selectedGroups.remove(fileInfo.fileName());
    }
    pushSelectionHistory(containerKey,
                         selected ? QString("Select %1").arg(fileInfo.fileName())
                                  : QString("Deselect %1").arg(fileInfo.fileName()),
                         previousSelection);
    syncVisibleItemSelection();
    emitSelectionDataChanged(-1, -1, true);
}

void FileListModel::invertSelection() {
    QHash<QString, QHash<QString, QString>> previousSelections;
    QSet<QString> changedContainers;

    for (int i = 0; i < _items.size(); i++) {
        const QString containerKey = selectionContainerForItem(_items[i]);
        ensureSelectionStateLoaded(containerKey);
        if (!previousSelections.contains(containerKey)) {
            previousSelections.insert(containerKey, _selectionStates[containerKey].selectedGroups);
        }

        if (setSelectionInState(i, !_items[i]->isSelected())) {
            changedContainers.insert(containerKey);
        }
    }

    for (const QString &containerKey : changedContainers) {
        pushSelectionHistory(containerKey, "Invert selection", previousSelections[containerKey]);
    }
    if (!changedContainers.isEmpty()) {
        emitSelectionDataChanged(-1, -1, true);
    }
}

void FileListModel::setAllSelection(bool selected) {
    QHash<QString, QHash<QString, QString>> previousSelections;
    QSet<QString> changedContainers;

    for (int i = 0; i < _items.size(); i++) {
        const QString containerKey = selectionContainerForItem(_items[i]);
        ensureSelectionStateLoaded(containerKey);
        if (!previousSelections.contains(containerKey)) {
            previousSelections.insert(containerKey, _selectionStates[containerKey].selectedGroups);
        }

        if (setSelectionInState(i, selected)) {
            changedContainers.insert(containerKey);
        }
    }

    for (const QString &containerKey : changedContainers) {
        pushSelectionHistory(containerKey, selected ? "Select all" : "Deselect all", previousSelections[containerKey]);
    }
    if (!changedContainers.isEmpty()) {
        emitSelectionDataChanged(-1, -1, true);
    }
}

void FileListModel::setSameKindSelection(int index_, bool selected) {
    if (index_ < 0 || index_ >= _items.size()) {
        return;
    }

    ImageFile *currentItem = _items[index_];
    const QString currentContainer = selectionContainerForItem(currentItem);
    ensureSelectionStateLoaded(currentContainer);
    const QHash<QString, QString> previousSelection =
        _selectionStates[currentContainer].selectedGroups;
    const QString currentSuffix = QFileInfo(currentItem->fileName()).suffix().toLower();
    bool changed = false;

    for (int i = 0; i < _items.size(); i++) {
        ImageFile *item = _items[i];
        if (selectionContainerForItem(item) != currentContainer) {
            continue;
        }

        bool matches = false;
        if (currentContainer == "Computer" && currentItem->folderPath().isEmpty()) {
            matches = item->folderPath().isEmpty();
        }
        else if (currentItem->isFolder()) {
            matches = item->isFolder();
        }
        else if (!item->isFolder()) {
            matches = QFileInfo(item->fileName()).suffix().toLower() == currentSuffix;
        }

        if (matches && setSelectionInState(i, selected)) {
            changed = true;
        }
    }

    if (changed) {
        pushSelectionHistory(currentContainer, sameKindDescription(index_, selected), previousSelection);
        emitSelectionDataChanged(-1, -1, true);
    }
}

QVariantList FileListModel::dragIndexesForIndex(int index_, bool singleItemOnly) const {
    QVariantList result;
    if (index_ < 0 || index_ >= _items.size()) {
        return result;
    }

    if (!singleItemOnly && _items[index_]->isSelected()) {
        for (int i = 0; i < _items.size(); i++) {
            if (_items[i]->isSelected()) {
                result.append(i);
            }
        }
    }
    else {
        result.append(index_);
    }
    return result;
}

QVariantList FileListModel::dragUrlsForIndex(int index_, bool singleItemOnly) const {
    QVariantList result;
    const QVariantList indexes = dragIndexesForIndex(index_, singleItemOnly);
    for (const QVariant &indexValue : indexes) {
        bool ok = false;
        const int itemIndex = indexValue.toInt(&ok);
        if (ok && itemIndex >= 0 && itemIndex < _items.size()) {
            const ImageFile *item = _items[itemIndex];
            QString fullPath = item->fullPath();
            if (_root == "Computer" && item->folderPath().isEmpty() &&
                    !fullPath.endsWith("/") && !fullPath.endsWith("\\")) {
                fullPath += "/";
            }
            result.append(QUrl::fromLocalFile(fullPath));
        }
    }
    return result;
}

QVariantMap FileListModel::dragPreviewItemsForIndex(int index_, int limit, bool singleItemOnly) const {
    QVariantMap result;
    QVariantList items;
    const QVariantList indexes = dragIndexesForIndex(index_, singleItemOnly);
    const int totalCount = indexes.size();
    const int cappedCount = limit < 0 ? totalCount : qMin(limit, totalCount);

    for (int i = 0; i < cappedCount; i++) {
        bool ok = false;
        const int itemIndex = indexes[i].toInt(&ok);
        if (!ok || itemIndex < 0 || itemIndex >= _items.size()) {
            continue;
        }

        const ImageFile *item = _items[itemIndex];
        QVariantMap previewItem;
        previewItem["index"] = itemIndex;
        previewItem["text"] = item->text();
        previewItem["imageIdUrl"] = item->imageIdUrl();
        previewItem["iconPath"] = item->iconPath();
        previewItem["isImage"] = item->isImage();
        previewItem["isFolder"] = item->isFolder();
        previewItem["fullPath"] = item->fullPath();
        items.append(previewItem);
    }

    result["items"] = items;
    result["totalCount"] = totalCount;
    result["remainingCount"] = qMax(0, totalCount - items.size());
    return result;
}

QVariantMap FileListModel::finalizeExternalDrag(
    const QVariantList &urls, int dropAction) {
    const auto action = static_cast<Qt::DropAction>(dropAction);
    if (action != Qt::MoveAction && action != Qt::TargetMoveAction) {
        return {
            {QStringLiteral("success"), true},
            {QStringLiteral("movedCount"), 0},
            {QStringLiteral("failedPaths"), QStringList{}},
        };
    }

    const bool sourceMustDelete = action == Qt::MoveAction;
    QStringList completedPaths;
    QStringList failedPaths;
    QSet<QString> sourceFolders;
    QSet<QString> seenPaths;
    for (const QVariant &urlValue : urls) {
        const QUrl url = urlValue.toUrl();
        if (!url.isLocalFile()) {
            continue;
        }

        const QString path = QFileInfo(url.toLocalFile()).absoluteFilePath();
        if (path.isEmpty() || seenPaths.contains(path)) {
            continue;
        }
        seenPaths.insert(path);
        const QFileInfo sourceInfo(path);
        sourceFolders.insert(sourceInfo.absolutePath());

        bool completed = !sourceInfo.exists();
        if (!completed && sourceMustDelete) {
            // A MoveAction means the target accepted the data and expects the
            // source to remove its original. TargetMoveAction is different:
            // the target took ownership and the source must not delete it.
            if (sourceInfo.isDir() && !sourceInfo.isSymLink()) {
                if (QDir(path).isRoot()) {
                    qWarning() << "Refusing to remove filesystem root after drag"
                               << path;
                }
                else {
                    completed = QDir(path).removeRecursively();
                }
            }
            else {
                completed = QFile::remove(path);
            }
        }

        if (completed) {
            completedPaths.append(path);
        }
        else if (sourceMustDelete) {
            failedPaths.append(path);
            qWarning() << "Could not remove source after external move" << path;
        }
    }

    removeMovedPathsFromSelection(
        completedPaths, QStringLiteral("Move by drag and drop"));

    if (!sourceFolders.isEmpty()) {
        PersistentFolderCache::removeFolders(sourceFolders.values());
    }
    const QString normalizedRoot =
        PersistentSelectionCache::normalizeContainerKey(_root);
    if (!normalizedRoot.isEmpty() && normalizedRoot != QStringLiteral("Computer") &&
        sourceFolders.contains(normalizedRoot)) {
        cd(_root);
    }

    return {
        {QStringLiteral("success"), failedPaths.isEmpty()},
        {QStringLiteral("movedCount"), completedPaths.size()},
        {QStringLiteral("failedPaths"), failedPaths},
    };
}

void FileListModel::configureNativeDragCursors(QObject *dragSource) {
#ifdef Q_OS_WIN
    if (!dragSource) {
        return;
    }
    QDrag *drag = dragSource->findChild<QDrag *>(
        QString(), Qt::FindDirectChildrenOnly);
    if (!drag) {
        qWarning() << "Could not find the active native drag for HiDPI cursors";
        return;
    }

    QScreen *screen = nullptr;
    if (const auto *item = qobject_cast<QQuickItem *>(dragSource)) {
        if (item->window()) {
            screen = item->window()->screen();
        }
    }
    if (!screen) {
        screen = QGuiApplication::screenAt(QCursor::pos());
    }
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }

    const qreal dpr = qBound(1.0,
        screen ? screen->devicePixelRatio() : 1.0, 4.0);
    const QSizeF previewSize = drag->pixmap().deviceIndependentSize();
    const QPointF hotSpot = drag->hotSpot();
    drag->setDragCursor(
        windowsDragCursorPixmap(dpr, previewSize, hotSpot,
                                Qt::CopyAction),
        Qt::CopyAction);
    drag->setDragCursor(
        windowsDragCursorPixmap(dpr, previewSize, hotSpot,
                                Qt::MoveAction),
        Qt::MoveAction);
    drag->setDragCursor(
        windowsDragCursorPixmap(dpr, previewSize, hotSpot,
                                Qt::IgnoreAction),
        Qt::IgnoreAction);
#else
    Q_UNUSED(dragSource)
#endif
}

bool FileListModel::fileDragActive() const {
    return _fileDragActive;
}

void FileListModel::setFileDragActive(bool active) {
    if (_fileDragActive == active) {
        return;
    }
    _fileDragActive = active;
    emit fileDragActiveChanged();
}

bool FileListModel::eventFilter(QObject *watched, QEvent *event) {
    Q_UNUSED(watched)
    switch (event->type()) {
    case QEvent::DragEnter: {
        const auto *dragEvent = static_cast<QDragEnterEvent *>(event);
        setFileDragActive(dragEvent->mimeData() &&
                          dragEvent->mimeData()->hasUrls());
        break;
    }
    case QEvent::DragMove: {
        const auto *dragEvent = static_cast<QDragMoveEvent *>(event);
        if (dragEvent->mimeData() && dragEvent->mimeData()->hasUrls()) {
            setFileDragActive(true);
        }
        break;
    }
    case QEvent::DragLeave:
    case QEvent::Drop:
        setFileDragActive(false);
        break;
    default:
        break;
    }
    return false;
}

void FileListModel::removeMovedPathsFromSelection(
    const QStringList &paths, const QString &description) {
    QHash<QString, QHash<QString, QString>> previousSelections;
    QSet<QString> changedContainers;
    for (const QString &path : paths) {
        const QFileInfo pathInfo(path);
        const QString containerKey =
            PersistentSelectionCache::normalizeContainerKey(pathInfo.absolutePath());
        ensureSelectionStateLoaded(containerKey);
        auto &state = _selectionStates[containerKey];
        if (!state.selectedGroups.contains(pathInfo.fileName())) {
            continue;
        }
        if (!previousSelections.contains(containerKey)) {
            previousSelections.insert(containerKey, state.selectedGroups);
        }
        state.selectedGroups.remove(pathInfo.fileName());
        changedContainers.insert(containerKey);
    }
    for (const QString &containerKey : std::as_const(changedContainers)) {
        pushSelectionHistory(containerKey, description,
                             previousSelections.value(containerKey));
    }
    if (!changedContainers.isEmpty()) {
        syncVisibleItemSelection();
        emitSelectionDataChanged(-1, -1, true);
    }
}

void FileListModel::refreshFoldersAfterFileOperation(
    const QSet<QString> &folders) {
    if (folders.isEmpty()) {
        return;
    }
    PersistentFolderCache::removeFolders(folders.values());
    const QString normalizedRoot =
        PersistentSelectionCache::normalizeContainerKey(_root);
    if (!normalizedRoot.isEmpty() && normalizedRoot != QStringLiteral("Computer") &&
        folders.contains(normalizedRoot)) {
        cd(_root);
    }
}

QVariantMap FileListModel::dropUrlsIntoFolder(
    const QVariantList &urls, const QString &destinationFolder,
    int dropAction) {
    const QFileInfo destinationInfo(destinationFolder);
    if (!destinationInfo.exists() || !destinationInfo.isDir()) {
        return fileOperationResult(
            false, QStringLiteral("Folder is unavailable"),
            QStringLiteral("The destination folder could not be found:\n%1")
                .arg(destinationFolder));
    }

    Qt::DropAction action = static_cast<Qt::DropAction>(dropAction);
    if (action != Qt::CopyAction && action != Qt::MoveAction) {
        action = Qt::CopyAction;
    }

    struct PendingItem { QString source; QString destination; };
    QList<PendingItem> pending;
    QSet<QString> seenSources;
    QSet<QString> seenNames;
    const Qt::CaseSensitivity sensitivity =
#ifdef Q_OS_WIN
        Qt::CaseInsensitive;
#else
        Qt::CaseSensitive;
#endif

    for (const QVariant &value : urls) {
        const QUrl url = value.canConvert<QUrl>() ? value.toUrl()
                                                  : QUrl(value.toString());
        if (!url.isLocalFile()) {
            return fileOperationResult(
                false, QStringLiteral("Unsupported item"),
                QStringLiteral("Only local files and folders can be dropped here."));
        }
        const QString source = QFileInfo(url.toLocalFile()).absoluteFilePath();
        if (seenSources.contains(source)) {
            continue;
        }
        seenSources.insert(source);
        const QFileInfo sourceInfo(source);
        if ((!sourceInfo.exists() && !sourceInfo.isSymLink()) ||
            (sourceInfo.isDir() && !sourceInfo.isSymLink() && QDir(source).isRoot())) {
            return fileOperationResult(
                false, QStringLiteral("Source is unavailable"),
                QStringLiteral("The source item could not be found or cannot be moved:\n%1")
                    .arg(source));
        }
        const QString name = sourceInfo.fileName();
        bool duplicateName = false;
        for (const QString &seenName : std::as_const(seenNames)) {
            if (seenName.compare(name, sensitivity) == 0) {
                duplicateName = true;
                break;
            }
        }
        if (duplicateName) {
            return fileOperationResult(
                false, QStringLiteral("Duplicate names"),
                QStringLiteral("More than one dropped item is named “%1”.").arg(name));
        }
        seenNames.insert(name);
        const QString target = QDir(destinationFolder).filePath(name);
        if (QFileInfo::exists(target) || QFileInfo(target).isSymLink()) {
            return fileOperationResult(
                false, QStringLiteral("Item already exists"),
                QStringLiteral("An item named “%1” already exists in:\n%2")
                    .arg(name, destinationFolder));
        }
        if (sourceInfo.isDir() && isSameOrChildPath(destinationFolder, source)) {
            return fileOperationResult(
                false, QStringLiteral("Invalid destination"),
                QStringLiteral("A folder cannot be placed inside itself:\n%1")
                    .arg(source));
        }
        pending.append({source, target});
    }

    if (pending.isEmpty()) {
        return fileOperationResult(false, QStringLiteral("Nothing to drop"),
                                   QStringLiteral("No local files or folders were provided."));
    }

    QList<PendingItem> completed;
    QString error;
    for (const PendingItem &item : std::as_const(pending)) {
        const bool ok = action == Qt::MoveAction
            ? movePath(item.source, item.destination, &error)
            : copyPath(item.source, item.destination, &error);
        if (ok) {
            completed.append(item);
            continue;
        }

        QStringList rollbackFailures;
        for (auto it = completed.crbegin(); it != completed.crend(); ++it) {
            bool rolledBack = false;
            if (action == Qt::MoveAction) {
                QString rollbackError;
                rolledBack = movePath(it->destination, it->source, &rollbackError);
            }
            else {
                rolledBack = removePath(it->destination);
            }
            if (!rolledBack) {
                rollbackFailures.append(it->destination);
            }
        }
        if (!rollbackFailures.isEmpty()) {
            error += QStringLiteral("\n\nSome rollback operations also failed:\n%1")
                         .arg(rollbackFailures.join(QLatin1Char('\n')));
        }
        return fileOperationResult(false, QStringLiteral("Drop failed"), error);
    }

    QStringList movedSources;
    QSet<QString> invalidatedFolders{destinationInfo.absoluteFilePath()};
    for (const PendingItem &item : std::as_const(completed)) {
        const QFileInfo sourceInfo(item.source);
        invalidatedFolders.insert(sourceInfo.absolutePath());
        if (action == Qt::MoveAction) {
            movedSources.append(item.source);
        }
    }
    if (!movedSources.isEmpty()) {
        removeMovedPathsFromSelection(
            movedSources, QStringLiteral("Move by drag and drop"));
    }
    refreshFoldersAfterFileOperation(invalidatedFolders);
    return fileOperationResult(true, QString(), QString(), action,
                               completed.size(), destinationInfo.absoluteFilePath());
}

QVariantMap FileListModel::createFolder(
    const QString &parentPath, const QString &name) {
    const QString trimmedName = name.trimmed();
    const QFileInfo parentInfo(parentPath);
    if (!parentInfo.exists() || !parentInfo.isDir()) {
        return fileOperationResult(
            false, QStringLiteral("Folder is unavailable"),
            QStringLiteral("The current folder could not be found:\n%1").arg(parentPath));
    }
    if (trimmedName.isEmpty() || trimmedName == QStringLiteral(".") ||
        trimmedName == QStringLiteral("..") || trimmedName.contains('/') ||
        trimmedName.contains('\\')) {
        return fileOperationResult(
            false, QStringLiteral("Invalid folder name"),
            QStringLiteral("Enter a valid folder name without path separators."));
    }
#ifdef Q_OS_WIN
    static const QRegularExpression invalidWindowsName(
        QStringLiteral(R"([<>:"/\\|?*\x00-\x1f])"));
    static const QRegularExpression reservedWindowsName(
        QStringLiteral(R"(^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\..*)?$)"),
        QRegularExpression::CaseInsensitiveOption);
    if (invalidWindowsName.match(trimmedName).hasMatch() ||
        trimmedName.endsWith('.') || trimmedName.endsWith(' ') ||
        reservedWindowsName.match(trimmedName).hasMatch()) {
        return fileOperationResult(
            false, QStringLiteral("Invalid folder name"),
            QStringLiteral("That name is not allowed on Windows."));
    }
#endif
    const QString newFolder = QDir(parentPath).filePath(trimmedName);
    if (QFileInfo::exists(newFolder) || QFileInfo(newFolder).isSymLink()) {
        return fileOperationResult(
            false, QStringLiteral("Folder already exists"),
            QStringLiteral("An item named “%1” already exists in this folder.")
                .arg(trimmedName));
    }
    if (!QDir(parentPath).mkdir(trimmedName)) {
        return fileOperationResult(
            false, QStringLiteral("Could not create folder"),
            QStringLiteral("The folder could not be created:\n%1").arg(newFolder));
    }
    refreshFoldersAfterFileOperation({parentInfo.absoluteFilePath()});
    return fileOperationResult(true, QString(), QString(), Qt::IgnoreAction,
                               0, QFileInfo(newFolder).absoluteFilePath());
}

QVariantMap FileListModel::createFolderAndDropUrls(
    const QVariantList &urls, const QString &parentPath,
    const QString &name, int dropAction) {
    const QVariantMap created = createFolder(parentPath, name);
    if (!created.value(QStringLiteral("success")).toBool()) {
        return created;
    }
    const QString folder = created.value(QStringLiteral("destinationFolder")).toString();
    const QVariantMap dropped = dropUrlsIntoFolder(urls, folder, dropAction);
    if (!dropped.value(QStringLiteral("success")).toBool()) {
        QDir(parentPath).rmdir(QFileInfo(folder).fileName());
        refreshFoldersAfterFileOperation({QFileInfo(parentPath).absoluteFilePath()});
        return dropped;
    }
    return dropped;
}

void FileListModel::beginSelectionPreview() {
    if (_selectionPreviewActive) {
        return;
    }

    _selectionPreviewActive = true;
    _selectionPreviewSnapshot.clear();
    for (ImageFile *item : _items) {
        const QString containerKey = selectionContainerForItem(item);
        ensureSelectionStateLoaded(containerKey);
        if (!_selectionPreviewSnapshot.contains(containerKey)) {
            _selectionPreviewSnapshot.insert(
                containerKey, _selectionStates[containerKey].selectedGroups);
        }
    }
}

void FileListModel::previewSelectionRange(int anchorIndex, int targetIndex, bool selected, bool includeTarget) {
    if (anchorIndex < 0 || anchorIndex >= _items.size() || targetIndex < 0 || targetIndex >= _items.size()) {
        return;
    }
    if (!_selectionPreviewActive) {
        beginSelectionPreview();
    }

    for (auto it = _selectionPreviewSnapshot.constBegin(); it != _selectionPreviewSnapshot.constEnd(); ++it) {
        ensureSelectionStateLoaded(it.key());
        _selectionStates[it.key()].selectedGroups = it.value();
    }
    syncVisibleItemSelection();

    int first = qMin(anchorIndex, targetIndex);
    int last = qMax(anchorIndex, targetIndex);
    if (!includeTarget) {
        if (targetIndex > anchorIndex) {
            last = targetIndex - 1;
        }
        else if (targetIndex < anchorIndex) {
            first = targetIndex + 1;
        }
        else {
            emitSelectionDataChanged();
            return;
        }
    }

    QList<int> indexes;
    for (int i = first; i <= last; i++) {
        indexes.append(i);
    }
    mutateSelectionForIndexes(indexes, selected);
    emitSelectionDataChanged();
}

void FileListModel::previewSelectionIndexes(const QVariantList &indexes, int mode) {
    if (!_selectionPreviewActive) {
        beginSelectionPreview();
    }

    for (auto it = _selectionPreviewSnapshot.constBegin(); it != _selectionPreviewSnapshot.constEnd(); ++it) {
        ensureSelectionStateLoaded(it.key());
        _selectionStates[it.key()].selectedGroups = it.value();
    }
    syncVisibleItemSelection();

    QList<int> intIndexes;
    intIndexes.reserve(indexes.size());
    for (const QVariant &indexValue : indexes) {
        bool ok = false;
        const int index_ = indexValue.toInt(&ok);
        if (ok) {
            intIndexes.append(index_);
        }
    }

    if (mode == SelectionPreviewReplace) {
        for (int i = 0; i < _items.size(); i++) {
            setSelectionInState(i, false);
        }
        mutateSelectionForIndexes(intIndexes, true);
    }
    else if (mode == SelectionPreviewToggle) {
        for (int index_ : intIndexes) {
            if (index_ < 0 || index_ >= _items.size()) {
                continue;
            }

            const ImageFile *item = _items[index_];
            const QString containerKey = selectionContainerForItem(item);
            const QString itemKey = selectionItemKey(item);
            const bool wasSelected = _selectionPreviewSnapshot.value(containerKey).contains(itemKey);
            setSelectionInState(index_, !wasSelected);
        }
    }
    else {
        mutateSelectionForIndexes(intIndexes, mode != SelectionPreviewDeselect);
    }
    emitSelectionDataChanged();
}

void FileListModel::commitSelectionPreview(const QString &description) {
    if (!_selectionPreviewActive) {
        return;
    }

    QSet<QString> changedContainers;
    for (auto it = _selectionPreviewSnapshot.constBegin(); it != _selectionPreviewSnapshot.constEnd(); ++it) {
        ensureSelectionStateLoaded(it.key());
        if (_selectionStates[it.key()].selectedGroups != it.value()) {
            changedContainers.insert(it.key());
        }
    }

    for (const QString &containerKey : changedContainers) {
        pushSelectionHistory(containerKey, description, _selectionPreviewSnapshot[containerKey]);
    }

    _selectionPreviewActive = false;
    _selectionPreviewSnapshot.clear();
    if (!changedContainers.isEmpty()) {
        emitSelectionDataChanged(-1, -1, true);
    }
}

void FileListModel::cancelSelectionPreview() {
    if (!_selectionPreviewActive) {
        return;
    }

    for (auto it = _selectionPreviewSnapshot.constBegin(); it != _selectionPreviewSnapshot.constEnd(); ++it) {
        ensureSelectionStateLoaded(it.key());
        _selectionStates[it.key()].selectedGroups = it.value();
    }
    _selectionPreviewActive = false;
    _selectionPreviewSnapshot.clear();
    syncVisibleItemSelection();
    emitSelectionDataChanged();
}

QVariantList FileListModel::selectionHistoryForIndex(int index_) const {
    QVariantList result;
    const QString containerKey = selectionContainerForIndex(index_);
    const auto stateIt = _selectionStates.constFind(containerKey);
    if (stateIt == _selectionStates.constEnd()) {
        return result;
    }

    const auto &history = stateIt->history;
    for (int i = 0; i < history.size(); i++) {
        QVariantMap row;
        row["index"] = i;
        row["description"] = history[i].description;
        row["timestamp"] = history[i].timestamp.toString("yyyy-MM-dd hh:mm:ss");
        row["selectedCount"] = history[i].selectedCount;
        row["current"] = i == stateIt->historyIndex;
        result.append(row);
    }
    return result;
}

int FileListModel::selectionHistoryIndexForIndex(int index_) const {
    const QString containerKey = selectionContainerForIndex(index_);
    const auto stateIt = _selectionStates.constFind(containerKey);
    return stateIt == _selectionStates.constEnd() ? -1 : stateIt->historyIndex;
}

QString FileListModel::selectionContainerForIndex(int index_) const {
    if (index_ >= 0 && index_ < _items.size()) {
        return selectionContainerForItem(_items[index_]);
    }
    return PersistentSelectionCache::normalizeContainerKey(_root);
}

void FileListModel::selectionHistoryBack(int index_) {
    const QString containerKey = selectionContainerForIndex(index_);
    ensureSelectionStateLoaded(containerKey);
    const int historyIndex = _selectionStates[containerKey].historyIndex;
    if (historyIndex > 0) {
        applySelectionHistoryState(containerKey, historyIndex - 1);
    }
}

void FileListModel::selectionHistoryForward(int index_) {
    const QString containerKey = selectionContainerForIndex(index_);
    ensureSelectionStateLoaded(containerKey);
    const int historyIndex = _selectionStates[containerKey].historyIndex;
    if (historyIndex >= 0 && historyIndex < _selectionStates[containerKey].history.size() - 1) {
        applySelectionHistoryState(containerKey, historyIndex + 1);
    }
}

void FileListModel::jumpSelectionHistory(int index_, int historyIndex) {
    const QString containerKey = selectionContainerForIndex(index_);
    ensureSelectionStateLoaded(containerKey);
    applySelectionHistoryState(containerKey, historyIndex);
}

QVariantList FileListModel::selectionGroups() const {
    QVariantList result;
    const QString activeGroupId = PersistentSelectionCache::activeSelectionGroupId();
    const QList<PersistentSelectionCache::SelectionGroup> groups =
        PersistentSelectionCache::selectionGroups();
    const QHash<QString, int> storedCounts =
        PersistentSelectionCache::selectedCountsByGroup();
    result.reserve(groups.size());
    for (const auto &group : groups) {
        const int storedCount = storedCounts.value(group.id);
        const int availableCount =
            _availableSelectionCounts.value(group.id);
        result.append(QVariantMap{
            {QStringLiteral("id"), group.id},
            {QStringLiteral("name"), group.name},
            {QStringLiteral("color"), QColor(group.color)},
            {QStringLiteral("count"), availableCount},
            {QStringLiteral("storedCount"), storedCount},
            {QStringLiteral("unavailableCount"),
             qMax(0, storedCount - availableCount)},
            {QStringLiteral("active"), group.id == activeGroupId},
            {QStringLiteral("isDefault"), group.isDefault},
        });
    }
    return result;
}

QString FileListModel::activeSelectionGroupId() const {
    return PersistentSelectionCache::activeSelectionGroupId();
}

QString FileListModel::activeSelectionGroupName() const {
    const QString activeGroupId = activeSelectionGroupId();
    for (const auto &group : PersistentSelectionCache::selectionGroups()) {
        if (group.id == activeGroupId) {
            return group.name;
        }
    }
    return QStringLiteral("Yellow");
}

QColor FileListModel::activeSelectionGroupColor() const {
    return QColor(PersistentSelectionCache::colorForGroup(activeSelectionGroupId()));
}

int FileListModel::totalSelectedCount() const {
    int count = 0;
    for (auto it = _availableSelectionCounts.constBegin();
         it != _availableSelectionCounts.constEnd(); ++it) {
        count += it.value();
    }
    return count;
}

bool FileListModel::canAddSelectionGroup() const {
    return PersistentSelectionCache::selectionGroups().size() < 8;
}

QString FileListModel::addSelectionGroup() {
    const QString groupId = PersistentSelectionCache::addSelectionGroup();
    if (!groupId.isEmpty()) {
        emit selectionGroupsChanged();
        emit activeSelectionGroupChanged();
    }
    return groupId;
}

void FileListModel::activateSelectionGroup(const QString &groupId) {
    if (PersistentSelectionCache::setActiveSelectionGroupId(groupId)) {
        emit selectionGroupsChanged();
        emit activeSelectionGroupChanged();
    }
}

bool FileListModel::renameSelectionGroup(const QString &groupId, const QString &name) {
    if (!PersistentSelectionCache::renameSelectionGroup(groupId, name)) {
        return false;
    }
    emit selectionGroupsChanged();
    if (groupId == activeSelectionGroupId()) {
        emit activeSelectionGroupChanged();
    }
    return true;
}

bool FileListModel::removeSelectionGroup(const QString &groupId) {
    const bool wasActive = groupId == activeSelectionGroupId();
    if (!PersistentSelectionCache::removeSelectionGroup(groupId)) {
        return false;
    }

    _selectionPreviewActive = false;
    _selectionPreviewSnapshot.clear();
    _selectionStates.clear();
    loadSelectionStatesForVisibleItems();
    emitSelectionDataChanged(-1, -1, true);
    emit selectionHistoryChanged();
    if (wasActive) {
        emit activeSelectionGroupChanged();
    }
    return true;
}

int FileListModel::copyActiveSelectionGroupPaths() const {
    const QString activeGroupId =
        PersistentSelectionCache::activeSelectionGroupId();
    QStringList paths;
    const auto selectedFiles =
        PersistentSelectionCache::selectedFilesByAdditionDate();
    paths.reserve(selectedFiles.size());
    for (const auto &selectedFile : selectedFiles) {
        if (selectedFile.groupId == activeGroupId) {
            paths.append(selectedFile.path);
        }
    }

    QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard || paths.isEmpty()) {
        return 0;
    }
    clipboard->setText(paths.join(QLatin1Char('\n')));
    return paths.size();
}

bool FileListModel::canUndoSelectionGroupMove() const {
    return _lastSelectionGroupMove.isValid();
}

QVariantMap FileListModel::selectionGroupMoveError(
    const QString &title, const QString &message,
    const QString &skipPath) const {
    return {
        {QStringLiteral("success"), false},
        {QStringLiteral("title"), title},
        {QStringLiteral("message"), message},
        {QStringLiteral("skippable"), !skipPath.isEmpty()},
        {QStringLiteral("skipPath"), skipPath},
    };
}

QVariantMap FileListModel::moveActiveSelectionGroupToCurrentFolder() {
    return moveActiveSelectionGroupToCurrentFolderImpl({}, false);
}

QVariantMap FileListModel::moveActiveSelectionGroupToCurrentFolderSkipping(
    const QStringList &skippedPaths) {
    return moveActiveSelectionGroupToCurrentFolderImpl(skippedPaths, false);
}

QVariantMap FileListModel::moveActiveSelectionGroupToCurrentFolderSkippingAll(
    const QStringList &skippedPaths) {
    return moveActiveSelectionGroupToCurrentFolderImpl(skippedPaths, true);
}

QVariantMap FileListModel::moveActiveSelectionGroupToCurrentFolderImpl(
    const QStringList &skippedPaths, bool skipAllItemErrors) {
    if (_root.isEmpty() || _root == QStringLiteral("Computer")) {
        return selectionGroupMoveError(
            QStringLiteral("Choose a folder"),
            QStringLiteral("The current address is not a filesystem folder. "
                           "Open the destination folder and try again."));
    }

    const QString groupId = activeSelectionGroupId();
    PersistentSelectionCache::SelectionGroup activeGroup;
    bool foundGroup = false;
    for (const auto &group : PersistentSelectionCache::selectionGroups()) {
        if (group.id == groupId) {
            activeGroup = group;
            foundGroup = true;
            break;
        }
    }
    if (!foundGroup) {
        return selectionGroupMoveError(
            QStringLiteral("Selection group not found"),
            QStringLiteral("The active selection group no longer exists."));
    }
    const QString folderName = activeGroup.name.trimmed();
    const bool invalidWindowsName =
        folderName.endsWith(QLatin1Char('.')) ||
        folderName.endsWith(QLatin1Char(' ')) ||
        folderName.contains(QRegularExpression(QStringLiteral(R"([<>:"/\\|?*\x00-\x1f])")));
    if (folderName.isEmpty() || folderName == QStringLiteral(".") ||
        folderName == QStringLiteral("..") || invalidWindowsName) {
        return selectionGroupMoveError(
            QStringLiteral("Invalid folder name"),
            QStringLiteral("“%1” cannot be used as a folder name. Rename the "
                           "selection group and try again.").arg(activeGroup.name));
    }

    QDir currentDirectory(_root);
    const QFileInfo currentDirectoryInfo(currentDirectory.absolutePath());
    if (!currentDirectoryInfo.exists() || !currentDirectoryInfo.isDir()) {
        return selectionGroupMoveError(
            QStringLiteral("Destination unavailable"),
            QStringLiteral("The current folder “%1” cannot be found.")
                .arg(QDir::toNativeSeparators(_root)));
    }

    const QString destinationFolder =
        currentDirectory.absoluteFilePath(folderName);
    if (QFileInfo::exists(destinationFolder)) {
        return selectionGroupMoveError(
            QStringLiteral("Folder already exists"),
            QStringLiteral("“%1” already exists in the current folder. Rename "
                           "the selection group or remove the existing folder, "
                           "then retry.").arg(folderName));
    }

    QList<PersistentSelectionCache::SelectedFile> selectedFiles;
    for (const auto &selectedFile :
         PersistentSelectionCache::selectedFilesByAdditionDate()) {
        if (selectedFile.groupId == groupId) {
            selectedFiles.append(selectedFile);
        }
    }
    if (selectedFiles.isEmpty()) {
        return selectionGroupMoveError(
            QStringLiteral("Selection group is empty"),
            QStringLiteral("There are no images to move."));
    }

    QSet<QString> normalizedSkippedPaths;
    for (const QString &skippedPath : skippedPaths) {
        if (!skippedPath.isEmpty()) {
            normalizedSkippedPaths.insert(
                QFileInfo(skippedPath).absoluteFilePath());
        }
    }
    QList<PersistentSelectionCache::SelectedFile> candidateFiles;
    candidateFiles.reserve(selectedFiles.size());
    for (const auto &selectedFile : std::as_const(selectedFiles)) {
        if (!normalizedSkippedPaths.contains(
                QFileInfo(selectedFile.path).absoluteFilePath())) {
            candidateFiles.append(selectedFile);
        }
    }
    if (candidateFiles.isEmpty()) {
        return selectionGroupMoveError(
            QStringLiteral("No images left to move"),
            QStringLiteral("Every image in this group was skipped. Cancel and "
                           "start the move again to retry skipped images."));
    }

    QList<PersistentSelectionCache::SelectedFile> filesToMove;
    filesToMove.reserve(candidateFiles.size());
    QHash<QString, QString> originalToMovedPath;
    QSet<QString> destinationNames;
    for (const auto &selectedFile : std::as_const(candidateFiles)) {
        const QFileInfo sourceInfo(selectedFile.path);
        if (!sourceInfo.exists() || !sourceInfo.isFile()) {
            if (skipAllItemErrors) {
                continue;
            }
            return selectionGroupMoveError(
                QStringLiteral("Source image not found"),
                QStringLiteral("“%1” cannot be found. Retry after restoring "
                               "it, or skip it and move the remaining images.")
                    .arg(QDir::toNativeSeparators(selectedFile.path)),
                selectedFile.path);
        }
        const QString destinationNameKey =
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
            sourceInfo.fileName().toCaseFolded();
#else
            sourceInfo.fileName();
#endif
        if (destinationNames.contains(destinationNameKey)) {
            if (skipAllItemErrors) {
                continue;
            }
            return selectionGroupMoveError(
                QStringLiteral("Duplicate image names"),
                QStringLiteral("More than one selected image is named “%1”. "
                               "Skip this copy or cancel the move.")
                    .arg(sourceInfo.fileName()),
                selectedFile.path);
        }
        destinationNames.insert(destinationNameKey);
        originalToMovedPath.insert(
            selectedFile.path,
            QDir(destinationFolder).absoluteFilePath(sourceInfo.fileName()));
        filesToMove.append(selectedFile);
    }
    if (filesToMove.isEmpty()) {
        return selectionGroupMoveError(
            QStringLiteral("No images could be moved"),
            QStringLiteral("All remaining images were unavailable or "
                           "conflicted with another selected image. Nothing "
                           "was changed."));
    }

    if (!currentDirectory.mkdir(folderName)) {
        return selectionGroupMoveError(
            QStringLiteral("Could not create folder"),
            QStringLiteral("The folder “%1” could not be created in “%2”. "
                           "Check permissions and available space, then retry.")
                .arg(folderName,
                     QDir::toNativeSeparators(currentDirectory.absolutePath())));
    }

    QStringList movedOriginalPaths;
    auto rollbackMoves = [&]() {
        for (auto it = movedOriginalPaths.crbegin();
             it != movedOriginalPaths.crend(); ++it) {
            QFile::rename(originalToMovedPath.value(*it), *it);
        }
        QDir(currentDirectory.absolutePath()).rmdir(folderName);
    };

    for (const auto &selectedFile : std::as_const(filesToMove)) {
        const QString movedPath = originalToMovedPath.value(selectedFile.path);
        if (QFileInfo::exists(movedPath) ||
            !QFile::rename(selectedFile.path, movedPath)) {
            if (skipAllItemErrors) {
                originalToMovedPath.remove(selectedFile.path);
                continue;
            }
            rollbackMoves();
            return selectionGroupMoveError(
                QStringLiteral("Could not move image"),
                QStringLiteral("“%1” could not be moved. The operation was "
                               "cancelled and previously moved images were "
                               "returned to their original locations. Retry or "
                               "skip this image.")
                    .arg(QDir::toNativeSeparators(selectedFile.path)),
                selectedFile.path);
        }
        movedOriginalPaths.append(selectedFile.path);
    }
    if (movedOriginalPaths.isEmpty()) {
        QDir(currentDirectory.absolutePath()).rmdir(folderName);
        return selectionGroupMoveError(
            QStringLiteral("No images could be moved"),
            QStringLiteral("Every remaining image failed to move. Nothing "
                           "was changed."));
    }

    const bool removedGroup = activeGroup.isDefault
        ? PersistentSelectionCache::removeSelectionGroupForMove(groupId)
        : PersistentSelectionCache::removeSelectionGroup(groupId);
    if (!removedGroup) {
        rollbackMoves();
        return selectionGroupMoveError(
            QStringLiteral("Could not remove selection group"),
            QStringLiteral("The images were returned to their original "
                           "locations. Retry the operation."));
    }
    _selectionPreviewActive = false;
    _selectionPreviewSnapshot.clear();
    _selectionStates.clear();
    loadSelectionStatesForVisibleItems();
    emitSelectionDataChanged(-1, -1, true);
    emit selectionHistoryChanged();
    emit activeSelectionGroupChanged();

    const bool previouslyUndoable = canUndoSelectionGroupMove();
    _lastSelectionGroupMove = {
        .group = activeGroup,
        .selectedFiles = selectedFiles,
        .destinationFolder = destinationFolder,
        .originalToMovedPath = originalToMovedPath,
    };
    if (!previouslyUndoable) {
        emit canUndoSelectionGroupMoveChanged();
    }

    QSet<QString> invalidatedFolders{_root};
    for (const auto &selectedFile : std::as_const(selectedFiles)) {
        invalidatedFolders.insert(QFileInfo(selectedFile.path).absolutePath());
    }
    PersistentFolderCache::removeFolders(invalidatedFolders.values());
    cd(_root);
    const qsizetype movedCount = movedOriginalPaths.size();
    const qsizetype skippedCount = selectedFiles.size() - movedCount;
    return {
        {QStringLiteral("success"), true},
        {QStringLiteral("message"),
         QStringLiteral("Moved %1 image%2 to “%3”.%4")
             .arg(movedCount)
             .arg(movedCount == 1 ? QString() : QStringLiteral("s"))
             .arg(folderName)
             .arg(skippedCount == 0
                      ? QString()
                      : QStringLiteral(" Skipped %1 image%2.")
                            .arg(skippedCount)
                            .arg(skippedCount == 1
                                     ? QString() : QStringLiteral("s")))},
    };
}

QVariantMap FileListModel::undoLastSelectionGroupMove() {
    if (!_lastSelectionGroupMove.isValid()) {
        return selectionGroupMoveError(
            QStringLiteral("Nothing to undo"),
            QStringLiteral("There is no group move to undo."));
    }

    const SelectionGroupMoveAction action = _lastSelectionGroupMove;
    for (auto it = action.originalToMovedPath.constBegin();
         it != action.originalToMovedPath.constEnd(); ++it) {
        if (!QFileInfo::exists(it.value())) {
            return selectionGroupMoveError(
                QStringLiteral("Moved image not found"),
                QStringLiteral("“%1” cannot be found. Nothing was changed.")
                    .arg(QDir::toNativeSeparators(it.value())));
        }
        if (QFileInfo::exists(it.key())) {
            return selectionGroupMoveError(
                QStringLiteral("Original path is occupied"),
                QStringLiteral("“%1” already exists. Remove or rename that "
                               "file, then retry undo.")
                    .arg(QDir::toNativeSeparators(it.key())));
        }
        if (!QFileInfo(it.key()).dir().exists()) {
            return selectionGroupMoveError(
                QStringLiteral("Original folder not found"),
                QStringLiteral("The original folder for “%1” no longer "
                               "exists. Recreate it, then retry undo.")
                    .arg(QDir::toNativeSeparators(it.key())));
        }
    }

    QStringList restoredOriginalPaths;
    auto rollbackUndo = [&]() {
        for (auto it = restoredOriginalPaths.crbegin();
             it != restoredOriginalPaths.crend(); ++it) {
            QFile::rename(*it, action.originalToMovedPath.value(*it));
        }
    };
    for (auto it = action.originalToMovedPath.constBegin();
         it != action.originalToMovedPath.constEnd(); ++it) {
        if (!QFile::rename(it.value(), it.key())) {
            rollbackUndo();
            return selectionGroupMoveError(
                QStringLiteral("Could not restore image"),
                QStringLiteral("“%1” could not be returned to its original "
                               "location. No selection changes were made.")
                    .arg(QDir::toNativeSeparators(it.value())));
        }
        restoredOriginalPaths.append(it.key());
    }

    if (!PersistentSelectionCache::restoreSelectionGroup(
            action.group, action.selectedFiles, true)) {
        rollbackUndo();
        return selectionGroupMoveError(
            QStringLiteral("Could not restore selection group"),
            QStringLiteral("The files were returned to the moved folder. A "
                           "group with the same name or color may already exist."));
    }

    QDir().rmdir(action.destinationFolder);
    _lastSelectionGroupMove.clear();
    emit canUndoSelectionGroupMoveChanged();
    _selectionPreviewActive = false;
    _selectionPreviewSnapshot.clear();
    _selectionStates.clear();
    loadSelectionStatesForVisibleItems();
    emitSelectionDataChanged(-1, -1, true);
    emit selectionHistoryChanged();
    emit activeSelectionGroupChanged();
    QSet<QString> invalidatedFolders{_root, action.destinationFolder};
    for (const auto &selectedFile : action.selectedFiles) {
        invalidatedFolders.insert(QFileInfo(selectedFile.path).absolutePath());
    }
    PersistentFolderCache::removeFolders(invalidatedFolders.values());
    cd(_root);

    return {
        {QStringLiteral("success"), true},
        {QStringLiteral("message"),
         QStringLiteral("Restored “%1” and returned %2 image%3.")
             .arg(action.group.name)
             .arg(action.originalToMovedPath.size())
             .arg(action.originalToMovedPath.size() == 1
                      ? QString() : QStringLiteral("s"))},
    };
}

QString FileListModel::selectionContainerForItem(const ImageFile *item) const {
    if (!item) {
        return PersistentSelectionCache::normalizeContainerKey(_root);
    }
    if (_root == "Computer" && item->folderPath().isEmpty()) {
        return "Computer";
    }
    return PersistentSelectionCache::normalizeContainerKey(item->folderPath());
}

QString FileListModel::selectionItemKey(const ImageFile *item) const {
    return item ? item->fileName() : QString();
}

QString FileListModel::selectionGroupForItem(const ImageFile *item) const {
    if (!item) {
        return QString();
    }
    const QString containerKey = selectionContainerForItem(item);
    const auto stateIt = _selectionStates.constFind(containerKey);
    return stateIt == _selectionStates.constEnd()
        ? QString()
        : stateIt->selectedGroups.value(selectionItemKey(item));
}

void FileListModel::ensureSelectionStateLoaded(const QString &containerKey) {
    const QString normalizedKey = PersistentSelectionCache::normalizeContainerKey(containerKey);
    if (!_selectionStates.contains(normalizedKey)) {
        _selectionStates.insert(normalizedKey, PersistentSelectionCache::retrieveContainer(normalizedKey));
    }
}

void FileListModel::loadSelectionStatesForVisibleItems() {
    for (ImageFile *item : _items) {
        ensureSelectionStateLoaded(selectionContainerForItem(item));
    }
    syncVisibleItemSelection();
}

void FileListModel::syncVisibleItemSelection() {
    for (ImageFile *item : _items) {
        const QString containerKey = selectionContainerForItem(item);
        ensureSelectionStateLoaded(containerKey);
        const QString groupId =
            _selectionStates[containerKey].selectedGroups.value(selectionItemKey(item));
        item->setIsSelected(!groupId.isEmpty());
        item->setSelectionGroupId(groupId);
        item->setSelectionGroupColor(groupId.isEmpty()
            ? QColor()
            : QColor(PersistentSelectionCache::colorForGroup(groupId)));
    }
}

void FileListModel::emitSelectionDataChanged(int firstIndex, int lastIndex,
                                             bool persistentChange) {
    if (!_items.isEmpty()) {
        if (firstIndex < 0 || lastIndex < 0) {
            firstIndex = 0;
            lastIndex = _items.size() - 1;
        }
        firstIndex = qBound(0, firstIndex, _items.size() - 1);
        lastIndex = qBound(0, lastIndex, _items.size() - 1);
        if (firstIndex > lastIndex) {
            std::swap(firstIndex, lastIndex);
        }
        emit dataChanged(index(firstIndex, 0), index(lastIndex, 0),
                         {SelectedRole, SelectionGroupIdRole, SelectionGroupColorRole});
    }
    emit selectionChanged();
    if (persistentChange) {
        const QStringList changedPaths = _pendingSelectionPaths.values();
        _pendingSelectionPaths.clear();
        if (changedPaths.isEmpty() || changedPaths.size() > 32) {
            refreshAvailableSelectionCounts();
        }
        else {
            updateAvailableSelectionCounts(changedPaths);
        }
        emit selectionPathsChanged(changedPaths);
        emit selectionGroupsChanged();
    }
}

void FileListModel::refreshAvailableSelectionCounts() {
    _availableSelectionCounts.clear();
    _availableSelectedPathGroups.clear();
    const auto selectedFiles =
        PersistentSelectionCache::selectedFilesByAdditionDate();
    for (const auto &selectedFile : selectedFiles) {
        const QFileInfo fileInfo(selectedFile.path);
        if (!fileInfo.isFile() ||
            !ThumbnailLoader::isFormatSupported(fileInfo.fileName())) {
            continue;
        }
        const QString path = fileInfo.absoluteFilePath();
        _availableSelectedPathGroups.insert(path, selectedFile.groupId);
        _availableSelectionCounts[selectedFile.groupId]++;
    }
}

void FileListModel::updateAvailableSelectionCounts(
    const QStringList &paths) {
    for (const QString &changedPath : paths) {
        const QFileInfo fileInfo(changedPath);
        const QString path = fileInfo.absoluteFilePath();
        const QString previousGroup =
            _availableSelectedPathGroups.take(path);
        if (!previousGroup.isEmpty()) {
            const int nextCount =
                _availableSelectionCounts.value(previousGroup) - 1;
            if (nextCount > 0) {
                _availableSelectionCounts.insert(previousGroup, nextCount);
            }
            else {
                _availableSelectionCounts.remove(previousGroup);
            }
        }

        PersistentSelectionCache::SelectedFile selectedFile;
        if (!PersistentSelectionCache::selectedFile(path, selectedFile) ||
            !fileInfo.isFile() ||
            !ThumbnailLoader::isFormatSupported(fileInfo.fileName())) {
            continue;
        }
        _availableSelectedPathGroups.insert(path, selectedFile.groupId);
        _availableSelectionCounts[selectedFile.groupId]++;
    }
}

void FileListModel::pushSelectionHistory(const QString &containerKey, const QString &description,
                                         const QHash<QString, QString> &previousSelectedGroups) {
    const QString normalizedKey = PersistentSelectionCache::normalizeContainerKey(containerKey);
    ensureSelectionStateLoaded(normalizedKey);
    auto &state = _selectionStates[normalizedKey];
    QSet<QString> changedNames(previousSelectedGroups.keyBegin(),
                               previousSelectedGroups.keyEnd());
    changedNames.unite(QSet<QString>(state.selectedGroups.keyBegin(),
                                     state.selectedGroups.keyEnd()));
    for (const QString &name : std::as_const(changedNames)) {
        if (previousSelectedGroups.value(name) !=
            state.selectedGroups.value(name)) {
            _pendingSelectionPaths.insert(
                normalizedKey == QStringLiteral("Computer")
                    ? name
                    : QDir(normalizedKey).absoluteFilePath(name));
        }
    }
    PersistentSelectionCache::appendHistoryEntry(
        state, description, previousSelectedGroups);
    PersistentSelectionCache::storeContainer(normalizedKey, state, false);
    _selectionSaveTimer.start();
    emit selectionHistoryChanged();
}

void FileListModel::mutateSelectionForIndexes(const QList<int> &indexes, bool selected) {
    for (int index_ : indexes) {
        setSelectionInState(index_, selected);
    }
}

bool FileListModel::setSelectionInState(int index_, bool selected) {
    if (index_ < 0 || index_ >= _items.size()) {
        return false;
    }

    ImageFile *item = _items[index_];
    const QString containerKey = selectionContainerForItem(item);
    ensureSelectionStateLoaded(containerKey);
    auto &selectedGroups = _selectionStates[containerKey].selectedGroups;
    const QString itemKey = selectionItemKey(item);
    const QString previousGroup = selectedGroups.value(itemKey);
    const QString nextGroup = selected ? activeSelectionGroupId() : QString();
    if (previousGroup == nextGroup) {
        return false;
    }

    if (selected) {
        selectedGroups.insert(itemKey, nextGroup);
    }
    else {
        selectedGroups.remove(itemKey);
    }
    item->setIsSelected(!nextGroup.isEmpty());
    item->setSelectionGroupId(nextGroup);
    item->setSelectionGroupColor(nextGroup.isEmpty()
        ? QColor()
        : QColor(PersistentSelectionCache::colorForGroup(nextGroup)));
    return true;
}

void FileListModel::applySelectionHistoryState(const QString &containerKey, int historyIndex) {
    const QString normalizedKey = PersistentSelectionCache::normalizeContainerKey(containerKey);
    ensureSelectionStateLoaded(normalizedKey);
    auto &state = _selectionStates[normalizedKey];
    if (historyIndex < 0 || historyIndex >= state.history.size()) {
        return;
    }

    const QHash<QString, QString> previousSelection = state.selectedGroups;
    if (!PersistentSelectionCache::applyHistoryIndex(state, historyIndex)) {
        return;
    }
    QSet<QString> changedNames(previousSelection.keyBegin(),
                               previousSelection.keyEnd());
    changedNames.unite(QSet<QString>(state.selectedGroups.keyBegin(),
                                     state.selectedGroups.keyEnd()));
    for (const QString &name : std::as_const(changedNames)) {
        if (previousSelection.value(name) != state.selectedGroups.value(name)) {
            _pendingSelectionPaths.insert(
                normalizedKey == QStringLiteral("Computer")
                    ? name
                    : QDir(normalizedKey).absoluteFilePath(name));
        }
    }
    PersistentSelectionCache::storeContainer(normalizedKey, state, false);
    _selectionSaveTimer.start();
    syncVisibleItemSelection();
    emitSelectionDataChanged(-1, -1, true);
    emit selectionHistoryChanged();
}

QString FileListModel::sameKindDescription(int index_, bool selected) const {
    if (index_ < 0 || index_ >= _items.size()) {
        return selected ? "Select matching items" : "Deselect matching items";
    }

    ImageFile *item = _items[index_];
    QString target;
    if (selectionContainerForItem(item) == "Computer" && item->folderPath().isEmpty()) {
        target = "drives";
    }
    else if (item->isFolder()) {
        target = "folders";
    }
    else {
        const QString suffix = QFileInfo(item->fileName()).suffix();
        target = suffix.isEmpty() ? "extensionless files" : QString(".%1 files").arg(suffix);
    }
    return QString("%1 %2").arg(selected ? "Select" : "Deselect", target);
}
