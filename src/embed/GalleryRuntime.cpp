#include <ZoinGallery/GalleryRuntime.h>

#include <ZoinGallery/GallerySession.h>

#include "CacheViewer.h"
#include "DecodeManager.h"
#include "MasonryLayout.h"
#include "ProviderImageStore.h"
#include "QmlAsyncImageProvider.h"
#include "QmlImageProvider.h"
#include "StorageLocations.h"
#include "SharedImageSourceProvider.h"
#include "ThumbnailLoader.h"
#include "ThumbnailMemoryCache.h"
#include "ViewerWheelArea.h"

#include <QDebug>
#include <QPointer>
#include <QQmlEngine>
#include <QSettings>
#include <QTimer>
#include <qqml.h>

#include <mutex>

namespace ZoinGallery {

namespace {

constexpr auto RuntimePropertyName = "_zoinGalleryRuntime";
constexpr auto NativeModuleUri = "ZoinGallery.Native";

QString normalizedProviderPrefix(QString prefix) {
    prefix = prefix.trimmed();
    for (QChar &character : prefix) {
        if (!character.isLetterOrNumber() && character != QLatin1Char('-') &&
            character != QLatin1Char('_')) {
            character = QLatin1Char('-');
        }
    }
    return prefix.isEmpty() ? QStringLiteral("zoingallery") : prefix;
}

QString normalizedSessionId(QString id) {
    id = id.trimmed();
    for (QChar &character : id) {
        if (!character.isLetterOrNumber() && character != QLatin1Char('-') &&
            character != QLatin1Char('_')) {
            character = QLatin1Char('_');
        }
    }
    return id;
}

} // namespace

class GalleryRuntime::Private {
public:
    QPointer<QQmlEngine> engine;
    RuntimeOptions options;
    QString thumbnailProviderName;
    QString asyncProviderName;
    QSharedPointer<ProviderImageStore> store;
    QSharedPointer<ThumbnailMemoryCache> thumbnailCache;
    QSharedPointer<ImageSourceProvider> imageSourceProvider;
    DecodeManager *decodeManager = nullptr;
    QmlAsyncImageProvider *asyncProvider = nullptr; // owned by QQmlEngine
    QList<QPointer<GallerySession>> sessions;
    bool shutdown = false;
};

GalleryRuntime *GalleryRuntime::install(
    QQmlEngine *engine, const RuntimeOptions &options) {
    if (!engine) {
        return nullptr;
    }
    const QVariant installed = engine->property(RuntimePropertyName);
    if (QObject *object = installed.value<QObject *>()) {
        return qobject_cast<GalleryRuntime *>(object);
    }

    registerTypes();
    // Decoder registration is explicit so embedding never depends on a
    // standalone executable's static initialization order.
    ThumbnailLoader::init();
    auto *runtime = new GalleryRuntime(engine, options);
    engine->setProperty(RuntimePropertyName,
                        QVariant::fromValue(static_cast<QObject *>(runtime)));
    return runtime;
}

void GalleryRuntime::registerTypes() {
    static std::once_flag once;
    std::call_once(once, [] {
        // The public ZoinGallery module is finalized by the generated dynamic
        // QML plugin. Keep the explicitly registered implementation types in a
        // private companion module so GalleryRuntime::install() can register
        // them before any GalleryPanel is loaded without racing (or mutating)
        // the plugin-owned namespace.
        qmlRegisterType<MasonryLayout>(NativeModuleUri, 1, 0,
                                       "MasonryLayout");
        qmlRegisterType<BrickItem>(NativeModuleUri, 1, 0, "BrickItem");
        qmlRegisterType<ViewerWheelArea>(NativeModuleUri, 1, 0,
                                        "ViewerWheelArea");
        // QML Timer shares Qt Quick's animation driver and can be suspended
        // together with a render-loop animation. This private event-loop timer
        // gives visual transitions an independent terminal-state deadline.
        qmlRegisterType<QTimer>(NativeModuleUri, 1, 0, "EventLoopTimer");
        qmlRegisterUncreatableType<GallerySession>(
            NativeModuleUri, 1, 0, "GallerySession",
            QStringLiteral("GallerySession instances are created by GalleryRuntime"));
        qmlRegisterType<ImageInfoModel>(NativeModuleUri, 1, 0,
                                        "ImageInfoModel");
    });
}

GalleryRuntime::GalleryRuntime(
    QQmlEngine *engine, const RuntimeOptions &options)
    : QObject(engine), d(new Private) {
    d->engine = engine;
    d->options = options;
    d->options.providerPrefix = normalizedProviderPrefix(
        d->options.providerPrefix);
    if (!StorageLocations::configure(d->options.storageNamespace)) {
        qWarning() << "ZoinGallery runtime storage namespace"
                   << d->options.storageNamespace
                   << "does not match the process namespace"
                   << StorageLocations::storageNamespace();
        d->options.storageNamespace = StorageLocations::storageNamespace();
    }
    d->thumbnailProviderName = d->options.thumbnailProviderName.trimmed();
    if (d->thumbnailProviderName.isEmpty()) {
        d->thumbnailProviderName =
            d->options.providerPrefix + QStringLiteral("-thumbnails");
    }
    d->asyncProviderName = d->options.asyncProviderName.trimmed();
    if (d->asyncProviderName.isEmpty()) {
        d->asyncProviderName =
            d->options.providerPrefix + QStringLiteral("-async");
    }
    d->store = QSharedPointer<ProviderImageStore>::create();
    d->thumbnailCache = QSharedPointer<ThumbnailMemoryCache>::create(
        d->store, d->options.thumbnailCacheByteBudget);
    QSharedPointer<ImageSourceProvider> configuredProvider =
        d->options.imageSourceProvider;
    if (!configuredProvider) {
        configuredProvider =
            QSharedPointer<LocalImageSourceProvider>::create();
    }
    d->imageSourceProvider =
        QSharedPointer<SharedImageSourceProvider>::create(
            std::move(configuredProvider));

    // DecodeManager treats a non-positive limit as "use the platform's ideal
    // thread count".  Keep that sentinel intact: the standalone shell uses it
    // to retain the pre-module decode parallelism, while embedded hosts can
    // still supply an explicit bounded limit.
    d->decodeManager = new DecodeManager(
        this, d->options.maxDecodeThreads, d->imageSourceProvider);
    if (d->options.persistentCache) {
        QSettings settings;
        d->decodeManager->setImageCacheMode(cacheUsageModeFromInt(
            settings.value(QStringLiteral("Cache/imageUsageMode"),
                           static_cast<int>(CacheUsageMode::On)).toInt()));
        d->decodeManager->setFileListCacheMode(cacheUsageModeFromInt(
            settings.value(QStringLiteral("Cache/fileListUsageMode"),
                           static_cast<int>(CacheUsageMode::On)).toInt()));
    }
    else {
        d->decodeManager->setImageCacheMode(CacheUsageMode::Off);
        d->decodeManager->setFileListCacheMode(CacheUsageMode::Off);
    }

    engine->addImageProvider(
        d->thumbnailProviderName,
        new QmlImageProvider(d->thumbnailProviderName, d->store));
    const QPointer<GalleryRuntime> runtimeGuard(this);
    d->asyncProvider = new QmlAsyncImageProvider(
        d->asyncProviderName, d->store, [runtimeGuard] {
            if (runtimeGuard) {
                runtimeGuard->asyncProviderDestroyed();
            }
        }, d->options.maxImageProviderThreads);
    engine->addImageProvider(d->asyncProviderName, d->asyncProvider);
}

void GalleryRuntime::asyncProviderDestroyed() {
    // QQmlEngine owns its providers and may delete them from the derived
    // engine destructor before QObject deletes the child runtime. Clear the
    // non-owning pointer while the runtime is still alive so its destructor
    // cannot call through a provider that the engine has already destroyed.
    d->asyncProvider = nullptr;
}

GalleryRuntime::~GalleryRuntime() {
    shutdown();
    if (d->engine &&
        d->engine->property(RuntimePropertyName).value<QObject *>() == this) {
        d->engine->setProperty(RuntimePropertyName, {});
    }
    delete d;
}

GallerySession *GalleryRuntime::createExternalSession(
    const QString &sessionId, QObject *parent) {
    const QString canonicalId = normalizedSessionId(sessionId);
    if (d->shutdown || canonicalId.isEmpty()) {
        return nullptr;
    }
    for (const QPointer<GallerySession> &session : std::as_const(d->sessions)) {
        if (session && session->sessionId() == canonicalId) {
            return session->sourceKind() ==
                    GallerySession::ExternalCatalogSource
                ? session.data() : nullptr;
        }
    }
    auto *session = new GallerySession(
        canonicalId, GallerySession::ExternalCatalogSource,
        d->thumbnailProviderName, d->asyncProviderName,
        d->store, d->thumbnailCache, d->decodeManager,
        d->options.viewerFitCacheByteBudget,
        d->options.viewerNativeCacheByteBudget,
        parent ? parent : this);
    d->sessions.append(session);
    connect(session, &QObject::destroyed, this, [this] {
        d->sessions.removeIf([](const QPointer<GallerySession> &candidate) {
            return candidate.isNull();
        });
    });
    return session;
}

GallerySession *GalleryRuntime::createSession(
    const QString &sessionId, QObject *parent) {
    const QString canonicalId = normalizedSessionId(sessionId);
    if (d->shutdown || canonicalId.isEmpty()) {
        return nullptr;
    }
    for (const QPointer<GallerySession> &session : std::as_const(d->sessions)) {
        if (session && session->sessionId() == canonicalId) {
            return session->sourceKind() ==
                    GallerySession::LocalFilesystemSource
                ? session.data() : nullptr;
        }
    }
    auto *session = new GallerySession(
        canonicalId, GallerySession::LocalFilesystemSource,
        d->thumbnailProviderName, d->asyncProviderName,
        d->store, d->thumbnailCache, d->decodeManager,
        d->options.viewerFitCacheByteBudget,
        d->options.viewerNativeCacheByteBudget,
        parent ? parent : this);
    d->sessions.append(session);
    connect(session, &QObject::destroyed, this, [this] {
        d->sessions.removeIf([](const QPointer<GallerySession> &candidate) {
            return candidate.isNull();
        });
    });
    return session;
}

QString GalleryRuntime::thumbnailProviderName() const {
    return d->thumbnailProviderName;
}

QString GalleryRuntime::asyncProviderName() const {
    return d->asyncProviderName;
}

QString GalleryRuntime::storageNamespace() const {
    return d->options.storageNamespace;
}

int GalleryRuntime::decodeWorkerCount() const {
    return d->decodeManager ? d->decodeManager->workerCount() : 0;
}

qint64 GalleryRuntime::thumbnailCacheByteBudget() const {
    return d->thumbnailCache ? d->thumbnailCache->byteBudget() : 0;
}

qint64 GalleryRuntime::thumbnailCacheRetainedBytes() const {
    return d->thumbnailCache ? d->thumbnailCache->retainedBytes() : 0;
}

qsizetype GalleryRuntime::thumbnailCacheFrameCount() const {
    return d->thumbnailCache ? d->thumbnailCache->frameCount() : 0;
}

qsizetype GalleryRuntime::thumbnailCachePendingRequestCount() const {
    return d->thumbnailCache
        ? d->thumbnailCache->pendingRequestCount() : 0;
}

quint64 GalleryRuntime::thumbnailCacheHitCount() const {
    return d->thumbnailCache ? d->thumbnailCache->hitCount() : 0;
}

quint64 GalleryRuntime::thumbnailCacheMissCount() const {
    return d->thumbnailCache ? d->thumbnailCache->missCount() : 0;
}

quint64 GalleryRuntime::thumbnailCacheCoalescedRequestCount() const {
    return d->thumbnailCache
        ? d->thumbnailCache->coalescedRequestCount() : 0;
}

quint64 GalleryRuntime::thumbnailCacheStoreCount() const {
    return d->thumbnailCache ? d->thumbnailCache->storeCount() : 0;
}

quint64 GalleryRuntime::thumbnailCacheEvictionCount() const {
    return d->thumbnailCache ? d->thumbnailCache->evictionCount() : 0;
}

void GalleryRuntime::shutdown() {
    if (d->shutdown) {
        return;
    }
    d->shutdown = true;
    for (const QPointer<GallerySession> &session : std::as_const(d->sessions)) {
        if (session) {
            session->shutdown();
        }
    }
    if (d->asyncProvider) {
        d->asyncProvider->shutdown();
    }
    if (d->decodeManager) {
        d->decodeManager->prepareToClose();
    }
    if (d->thumbnailCache) {
        d->thumbnailCache->clear();
    }
}

} // namespace ZoinGallery
