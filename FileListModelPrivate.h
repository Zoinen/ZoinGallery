#pragma once

#include "FileListModel.h"
#include "PathArgument.h"
#include "DecodeManager.h"
#include "NaturalSort.h"
#include "PersistentFolderCache.h"
#include "PersistentImageCache.h"
#include "QmlAsyncImageProvider.h"
#include "ThumbnailLoader.h"

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
#include <QFutureWatcher>
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
#include <QtConcurrent/QtConcurrentRun>

#include <chrono>
#include <filesystem>
#include <system_error>
#include <utility>
#ifdef Q_OS_WIN
#include <QtCore/qt_windows.h>
#endif
#ifdef Q_OS_LINUX
#include <QSocketNotifier>
#include <sys/inotify.h>
#include <unistd.h>
#include <cerrno>
#endif
using namespace std::chrono_literals;

namespace {
constexpr const char *ImageCacheModeSettingsKey = "Cache/imageUsageMode";
constexpr const char *FileListCacheModeSettingsKey = "Cache/fileListUsageMode";
constexpr int FolderRefreshDebounceMs = 180;
constexpr int FolderWatchRetryMaxMs = 5000;
constexpr int FolderPreviewRetryMaxAttempts = 4;
constexpr int FailedImageWorkRetryInitialMs = 250;
constexpr int FailedImageWorkRetryMaxMs = 5000;
constexpr int FailedImageWorkMaxAttempts = 8;

enum class FolderScanStatus {
    Success,
    RootUnavailable,
    EnumerationError,
};

struct FolderScanResult {
    QString path;
    QList<FileInfo> entries;
    FolderScanStatus status = FolderScanStatus::EnumerationError;
    QString errorText;
};

std::filesystem::path nativeFileSystemPath(const QString &path) {
#ifdef Q_OS_WIN
    return std::filesystem::path(
        QDir::toNativeSeparators(path).toStdWString());
#else
    return std::filesystem::path(QFile::encodeName(path).toStdString());
#endif
}
QString qStringFromNativeFileSystemPath(
    const std::filesystem::path &path) {
#ifdef Q_OS_WIN
    return QString::fromStdWString(path.native());
#else
    return QFile::decodeName(QByteArray::fromStdString(path.native()));
#endif
}
QString decodeRetryKey(const ImageDecodeRequest &request) {
    return QStringLiteral("%1\x1f%2\x1f%3x%4\x1f%5\x1f%6\x1f%7\x1f%8\x1f%9")
        .arg(request.info.path)
        .arg(request.viewerRequest ? 1 : 0)
        .arg(request.targetSize.width())
        .arg(request.targetSize.height())
        .arg(request.info.directOpenGeneration)
        .arg(request.info.lastModified.isValid()
                 ? request.info.lastModified.toMSecsSinceEpoch() : -1)
        .arg(request.info.fileSize)
        .arg(request.checkCache ? 1 : 0)
        .arg(request.fitToViewerRequest ? 1 : 0);
}

QString infoRetryKey(const ImageInfo &info) {
    return QStringLiteral("%1\x1f%2\x1f%3\x1f%4\x1f%5")
        .arg(info.path)
        .arg(info.isFromEmbeddedView ? 1 : 0)
        .arg(info.directOpenGeneration)
        .arg(info.lastModified.isValid()
                 ? info.lastModified.toMSecsSinceEpoch() : -1)
        .arg(info.fileSize);
}

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

QString fileSystemPathKey(const QString &path) {
    QString key = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
#ifdef Q_OS_WIN
    key = key.toCaseFolded();
#endif
    return key;
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
