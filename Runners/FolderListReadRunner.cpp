#include "FolderListReadRunner.h"

#include "NaturalSort.h"
#include "PersistentFolderCache.h"
#include "ThumbnailLoader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <filesystem>
#include <system_error>

namespace {
QList<FileInfo> previewImages(const QList<FileInfo> &entries, int totalImages) {
    QList<FileInfo> images;
    for (const FileInfo &entry : entries) {
        if (!entry.isDirectory && ThumbnailLoader::isFormatSupported(entry.name)) {
            images.append(entry);
        }
    }
    sortFileInfosNaturally(images);
    if (totalImages < 0 || images.size() <= totalImages) {
        return images;
    }
    if (totalImages == 0) {
        return {};
    }

    QList<FileInfo> sampled;
    const float step = qMax(1.0f, float(images.size()) / totalImages);
    for (float index = 0; index < images.size() && sampled.size() < totalImages; index += step) {
        sampled.append(images.at(static_cast<int>(index)));
    }
    return sampled;
}

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

bool readCompleteFolderSnapshot(const QString &path, QList<FileInfo> *entries,
                                QString *errorText) {
    const QFileInfo rootInfo(path);
    if (!rootInfo.isDir() || !rootInfo.isReadable()) {
        *errorText = QStringLiteral("Folder is unavailable");
        return false;
    }

    std::error_code scanError;
    std::filesystem::directory_iterator iterator(
        nativeFileSystemPath(path), scanError);
    const std::filesystem::directory_iterator end;
    if (scanError) {
        *errorText = QString::fromStdString(scanError.message());
        return false;
    }

    while (iterator != end) {
        const std::filesystem::directory_entry sourceEntry = *iterator;
        std::error_code metadataError;
        const std::filesystem::file_status linkStatus =
            sourceEntry.symlink_status(metadataError);
        if (metadataError) {
            *errorText = QString::fromStdString(metadataError.message());
            entries->clear();
            return false;
        }
        const bool sourceIsSymlink =
            std::filesystem::is_symlink(linkStatus);
        const bool sourceIsDirectory =
            std::filesystem::is_directory(linkStatus);
        const bool sourceIsRegularFile =
            std::filesystem::is_regular_file(linkStatus);
        std::filesystem::file_time_type sourceWriteTime;
        std::uintmax_t sourceFileSize = 0;
        if (!sourceIsSymlink) {
            sourceWriteTime = sourceEntry.last_write_time(metadataError);
            if (!metadataError && sourceIsRegularFile) {
                sourceFileSize = sourceEntry.file_size(metadataError);
            }
            if (metadataError) {
                *errorText = QString::fromStdString(metadataError.message());
                entries->clear();
                return false;
            }
        }

        const QFileInfo entry(qStringFromNativeFileSystemPath(
            sourceEntry.path()));
        const bool entryExists = entry.exists() || entry.isSymLink();
        const bool entryIsDirectory = entry.isDir();
        const QDateTime entryLastModified = entry.lastModified();
        const qint64 entryFileSize =
            entryIsDirectory ? 0 : entry.size();

        std::error_code verificationError;
        const std::filesystem::file_status verifiedStatus =
            sourceEntry.symlink_status(verificationError);
        bool metadataChangedDuringScan = verificationError ||
            verifiedStatus.type() != linkStatus.type() || !entryExists;
        if (!metadataChangedDuringScan && !sourceIsSymlink) {
            const std::filesystem::file_time_type verifiedWriteTime =
                sourceEntry.last_write_time(verificationError);
            metadataChangedDuringScan = verificationError ||
                verifiedWriteTime != sourceWriteTime ||
                entryIsDirectory != sourceIsDirectory ||
                !entryLastModified.isValid();
            if (!metadataChangedDuringScan && sourceIsRegularFile) {
                const std::uintmax_t verifiedFileSize =
                    sourceEntry.file_size(verificationError);
                metadataChangedDuringScan = verificationError ||
                    verifiedFileSize != sourceFileSize ||
                    entryFileSize < 0 ||
                    static_cast<std::uintmax_t>(entryFileSize) !=
                        sourceFileSize;
            }
        }
        if (metadataChangedDuringScan) {
            *errorText = QStringLiteral(
                "Folder entry changed while reading metadata: %1")
                .arg(entry.absoluteFilePath());
            entries->clear();
            return false;
        }
        entries->append(FileInfo{
            .name = entry.fileName(),
            .lastModified = entryLastModified,
            .fileSize = entryFileSize,
            .isDirectory = entryIsDirectory,
        });

        iterator.increment(scanError);
        if (scanError) {
            *errorText = QString::fromStdString(scanError.message());
            entries->clear();
            return false;
        }
    }

    const QFileInfo rootAfterScan(path);
    if (!rootAfterScan.isDir() || !rootAfterScan.isReadable()) {
        *errorText =
            QStringLiteral("Folder disappeared while reading metadata");
        entries->clear();
        return false;
    }
    return true;
}
}

FolderListReadRunner::FolderListReadRunner(const QString &path, int totalImages,
                                           bool storeInCache,
                                           quint64 requestGeneration)
    : _path(path), _totalImages(totalImages), _storeInCache(storeInCache),
      _cacheGeneration(PersistentFolderCache::generation()),
      _requestGeneration(requestGeneration) {
}

void FolderListReadRunner::run() {
    QList<FileInfo> entries;
    QString errorText;
    if (!readCompleteFolderSnapshot(_path, &entries, &errorText)) {
        if (!isCanceled()) {
            emit folderListFailed(_path, errorText, _requestGeneration);
        }
        emit finished(this);
        return;
    }
    if (_storeInCache && !isCanceled()) {
        PersistentFolderCache::storeFolder(FolderInfo{_path, entries}, _cacheGeneration);
    }
    if (!isCanceled()) {
        emit folderListReady(_path, previewImages(entries, _totalImages),
                             _requestGeneration);
    }
    emit finished(this);
}
