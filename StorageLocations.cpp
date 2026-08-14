#include "StorageLocations.h"

#include <QDir>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>

namespace ZoinGallery::StorageLocations {
namespace {

QMutex storageMutex;
QString configuredNamespace;

QString normalizedNamespace(QString value) {
    value = value.trimmed();
    for (QChar &character : value) {
        if (!character.isLetterOrNumber() && character != QLatin1Char('-') &&
            character != QLatin1Char('_')) {
            character = QLatin1Char('-');
        }
    }
    return value.isEmpty() ? QStringLiteral("embedded") : value;
}

QString writableRoot(QStandardPaths::StandardLocation location,
                     const QString &fallback) {
    const QString path = QStandardPaths::writableLocation(location);
    return path.isEmpty() ? fallback : path;
}

QString scopedRoot(const QString &basePath, const QString &storageNamespace) {
    const QString normalized = normalizedNamespace(storageNamespace);
    // Preserve the existing standalone cache/settings locations so upgrading
    // does not strand a user's databases. Every embedded host receives its own
    // explicit child root and therefore cannot overwrite those legacy files.
    if (normalized == QStringLiteral("standalone")) {
        return QDir::cleanPath(basePath);
    }
    return QDir(basePath).filePath(
        QStringLiteral("ZoinGallery/%1").arg(normalized));
}

QString currentNamespaceLocked() {
    if (configuredNamespace.isEmpty()) {
        configuredNamespace = QStringLiteral("standalone");
    }
    return configuredNamespace;
}

} // namespace

bool configure(const QString &storageNamespace) {
    const QString normalized = normalizedNamespace(storageNamespace);
    QMutexLocker locker(&storageMutex);
    if (configuredNamespace.isEmpty()) {
        configuredNamespace = normalized;
        return true;
    }
    return configuredNamespace == normalized;
}

QString storageNamespace() {
    QMutexLocker locker(&storageMutex);
    return currentNamespaceLocked();
}

QString cacheRootForNamespace(const QString &storageNamespace) {
    const QString tempRoot = writableRoot(QStandardPaths::TempLocation,
                                          QDir::tempPath());
    return scopedRoot(tempRoot, storageNamespace);
}

QString dataRootForNamespace(const QString &storageNamespace) {
    const QString appDataRoot = writableRoot(
        QStandardPaths::AppDataLocation,
        QDir(QDir::homePath()).filePath(QStringLiteral(".zoingallery")));
    return scopedRoot(appDataRoot, storageNamespace);
}

QString cacheRoot() {
    return cacheRootForNamespace(storageNamespace());
}

QString dataRoot() {
    return dataRootForNamespace(storageNamespace());
}

} // namespace ZoinGallery::StorageLocations
