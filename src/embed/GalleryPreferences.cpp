#include <ZoinGallery/GalleryPreferences.h>
#include <ZoinGallery/MediaTimingTrace.h>
#include "DecodeManager.h"
#include "DisplayColorSpace.h"
#include "StorageLocations.h"
#include "PersistentImageCache.h"
#include "PersistentDerivedImageCache.h"
#include "Decoders/ImageDecoderInterface.h"
#include <QDir>
#include <QFutureWatcher>
#include <QSettings>
#include <QWindow>
#include <QtConcurrentRun>

namespace ZoinGallery {
GalleryPreferences::GalleryPreferences(DecodeManager *decoder, bool persistent, QObject *parent)
    : QObject(parent), _decoder(decoder), _decoders(ImageDecoderFactory::decoderInventory()) {
    _cachePool.setMaxThreadCount(1);
    QSettings settings;
    _values = {{"imageMode", int(decoder->imageCacheMode())},
        {"folderMode", int(decoder->fileListCacheMode())},
        {"diskLimitMiB", qBound(64, settings.value("Cache/diskLimitMiB", 512).toInt(), 65536)},
        {"location", settings.value("Cache/location", QString()).toString()},
        {"activeLocation", StorageLocations::cacheRoot()},
        {"convertColors", DisplayColorSpace::conversionEnabled()},
        {"animateResizing", settings.value("Gallery/animateResizing", false).toBool()}};
    if (persistent) refresh();
}
QVariantMap GalleryPreferences::values() const { return _values; }
QVariantList GalleryPreferences::decoders() const { return _decoders; }
QString GalleryPreferences::targetColorSpace() const { return DisplayColorSpace::currentDescription(); }
bool GalleryPreferences::animateResizing() const { return _values.value("animateResizing").toBool(); }

bool GalleryPreferences::apply(const QVariantMap &values) {
    const int limit = values.value("diskLimitMiB").toInt();
    const QString location = values.value("location").toString().trimmed();
    const int imageMode = values.value("imageMode").toInt();
    const int folderMode = values.value("folderMode").toInt();
    if (limit < 64 || limit > 65536 || imageMode < 0 || imageMode > 2
        || folderMode < 0 || folderMode > 2
        || (!location.isEmpty() && !QDir::isAbsolutePath(location))) {
        _error = tr("Use a limit from 64 to 65536 MiB and an absolute cache path.");
        emit changed();
        return false;
    }
    if (_busy) { _error = tr("Wait for the current cache operation to finish."); emit changed(); return false; }
    QSettings settings;
    settings.setValue("Cache/imageUsageMode", imageMode);
    settings.setValue("Cache/fileListUsageMode", folderMode);
    settings.setValue("Cache/diskLimitMiB", limit);
    settings.setValue("Cache/location", location);
    settings.setValue("Gallery/animateResizing", values.value("animateResizing").toBool());
    const bool colorsChanged = DisplayColorSpace::conversionEnabled() != values.value("convertColors").toBool();
    DisplayColorSpace::setConversionEnabled(values.value("convertColors").toBool());
    settings.sync();
    if (settings.status() != QSettings::NoError) { _error = tr("Could not save Gallery settings."); emit changed(); return false; }
    _values = values;
    _values["location"] = location;
    _values["activeLocation"] = StorageLocations::cacheRoot();
    _decoder->setImageCacheMode(cacheUsageModeFromInt(imageMode));
    _decoder->setFileListCacheMode(cacheUsageModeFromInt(folderMode));
    if (folderMode == 0) emit clearSnapshotsRequested();
    if (colorsChanged) emit pixelsInvalidated();
    MediaTimingTrace::event("qt.gallery.preferences", {{"diskLimitMiB", limit}, {"imageMode", imageMode}, {"folderMode", folderMode}});
    _error.clear();
    emit changed();
    refresh();
    return true;
}
void GalleryPreferences::refresh() { runCacheOperation(false); }
void GalleryPreferences::clearCache() {
    if (_busy) return;
    emit clearSnapshotsRequested();
    emit pixelsInvalidated();
    runCacheOperation(true);
}
void GalleryPreferences::runCacheOperation(bool clear) {
    if (_busy) return;
    _busy = true;
    emit changed();
    const qint64 budget = _values.value("diskLimitMiB").toLongLong() * 1024 * 1024;
    auto *watcher = new QFutureWatcher<qint64>(this);
    connect(watcher, &QFutureWatcher<qint64>::finished, this, [this, watcher] {
        _diskBytes = watcher->result();
        watcher->deleteLater();
        _busy = false;
        emit changed();
    });
    watcher->setFuture(QtConcurrent::run(&_cachePool, [budget, clear] {
        if (clear) PersistentImageCache::clear();
        PersistentDerivedImageCache::setByteBudget(budget);
        return PersistentImageCache::cacheSize();
    }));
}
void GalleryPreferences::updateDisplay(QObject *object) {
    auto *window = qobject_cast<QWindow *>(object);
    if (!window) return;
    const auto previous = DisplayColorSpace::current();
    DisplayColorSpace::setCurrent(DisplayColorSpace::colorSpaceForScreen(window->screen()));
    if (previous != DisplayColorSpace::current()) emit pixelsInvalidated();
    emit changed();
}
}
