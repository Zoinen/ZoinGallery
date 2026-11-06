#include "VectorSource.h"
#include "VectorSourceStorage_p.h"
#include <QFileInfo>
#include <QSvgRenderer>
#include <QUrlQuery>
#include <QMutex>
#include <QMutexLocker>
#include <QHash>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <QQmlEngine>
#include <QCoreApplication>
#include <QWaitCondition>
#include <QGlobalStatic>
#include <set>
#include <QtQuickVectorImageGenerator/private/qquickqmlgenerator_p.h>
#include <QtQuick/private/qquicktext_p.h>
#include <QtQuickShapes/private/qquickshape_p.h>

bool VectorSourcePin::sceneReady(QQuickItem *item) const {
    if (!item) return false;
    const auto *shape = qobject_cast<QQuickShape *>(item);
    bool ready = !shape || shape->status() == QQuickShape::Ready;
    if (auto *text = qobject_cast<QQuickText *>(item);
        text && text->renderType() != QQuickText::CurveRendering) {
        text->setRenderType(QQuickText::CurveRendering);
    }
    // Keep traversal native: exposing each children list to JS creates wrappers
    // for the generated SVG scene on every asynchronous readiness poll.
    const auto children = item->childItems();
    for (auto *child : children) {
        if (!sceneReady(child)) ready = false;
    }
    return ready;
}

namespace {
struct DirectoryCleanupExecutor {
    DirectoryCleanupExecutor() { pool.setMaxThreadCount(1); }
    QThreadPool pool;
};
Q_GLOBAL_STATIC(DirectoryCleanupExecutor, directoryCleanupExecutor)

QMutex sourceMutex;
QHash<QUrl, QWeakPointer<VectorSource>> sourceHandles;
// Qt 6.11's generator uses shared mutable indentation storage. All gallery
// generation goes through this gate; presentation never calls VectorImage's
// synchronous generator on the GUI thread.
QMutex generationMutex;
QWaitCondition generationAvailable;
std::set<std::pair<int, quint64>> generationWaiters;
quint64 generationSequence = 0;
bool generationBusy = false;

class GenerationTurn {
public:
    GenerationTurn(int priority,
                   const QSharedPointer<ZoinGallery::ImageSourceCancellation> &cancellation) {
        QMutexLocker lock(&generationMutex);
        const auto ticket = std::make_pair(priority, ++generationSequence);
        generationWaiters.insert(ticket);
        while (true) {
            if (cancellation && cancellation->isCanceled()) {
                generationWaiters.erase(ticket);
                generationAvailable.wakeAll();
                return;
            }
            if (!generationBusy && *generationWaiters.begin() == ticket) break;
            generationAvailable.wait(&generationMutex, 10);
        }
        generationWaiters.erase(ticket);
        generationBusy = true;
        _acquired = true;
    }
    ~GenerationTurn() {
        if (!_acquired) return;
        QMutexLocker lock(&generationMutex);
        generationBusy = false;
        generationAvailable.wakeAll();
    }
    explicit operator bool() const { return _acquired; }
private:
    bool _acquired = false;
};

QThreadPool *rawSourcePool() {
    static auto *pool = [] {
        auto *result = new QThreadPool(QCoreApplication::instance());
        result->setMaxThreadCount(1);
        QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit,
                         result, [result] { result->waitForDone(); });
        return result;
    }();
    return pool;
}
}


bool isVectorSource(const ImageInfo &info) {
    const QString suffix = QFileInfo(info.formatHint()).suffix().toLower();
    return suffix == QStringLiteral("svg") || suffix == QStringLiteral("svgz")
        || info.source.mimeType.section(';', 0, 0).trimmed()
            == QStringLiteral("image/svg+xml");
}

QSharedPointer<QTemporaryDir> ZoinGallery::Internal::createVectorSourceDirectory(
    QThreadPool *executor) {
    // Initialize the shared executor during worker-side source preparation.
    if (!executor) (void)directoryCleanupExecutor();
    return QSharedPointer<QTemporaryDir>(new QTemporaryDir, [executor](QTemporaryDir *directory) {
        QThreadPool *pool = executor;
        if (!pool) {
            if (auto *shared = directoryCleanupExecutor()) pool = &shared->pool;
        }
        if (pool) {
            pool->start([directory] { delete directory; });
        } else {
            // Late static teardown: the shared pool has already drained.
            delete directory;
        }
    });
}

QString vectorRevisionKey(const ImageInfo &info) {
    return info.source.isValid() ? info.source.accessKey()
        : info.path + QChar(0x1f) + info.sourceVersionToken + QChar(0x1f)
          + QString::number(info.lastModified.toMSecsSinceEpoch())
          + QChar(0x1f) + QString::number(info.fileSize);
}

VectorSourcePtr acquireVectorSource(const ImageInfo &info,
    const QSharedPointer<ZoinGallery::ImageSourceLease> &lease,
    const QSharedPointer<ZoinGallery::ImageSourceCancellation> &cancellation,
    int preparationPriority) {
    const QString path = lease ? lease->localPath() : info.path;
    QUrl url = QUrl::fromLocalFile(path);
    QUrlQuery revision;
    revision.addQueryItem(QStringLiteral("vector"), QStringLiteral("1"));
    revision.addQueryItem(QStringLiteral("revision"), vectorRevisionKey(info));
    url.setQuery(revision);
    GenerationTurn generationTurn(preparationPriority, cancellation);
    if (!generationTurn) return {};
    {
        QMutexLocker lock(&sourceMutex);
        if (auto existing = sourceHandles.value(url).toStrongRef()) return existing;
    }
    // Parsing on the read worker validates XML, compressed SVG and dimensions.
    // SVG geometry is never rasterized; authored image elements remain images.
    QSvgRenderer document(path);
    if (!document.isValid() || document.defaultSize().isEmpty()) return {};
    auto source = VectorSourcePtr::create();
    source->size = document.defaultSize();
    source->lease = lease;
    source->url = url;
    // A distinct immutable directory avoids QQmlEngine's cached directory
    // listings and keeps generated image assets alive with the VFS revision.
    source->preparedFiles = ZoinGallery::Internal::createVectorSourceDirectory();
    if (!source->preparedFiles->isValid()) return {};
    const QString componentPath = source->preparedFiles->filePath(QStringLiteral("Prepared.qml"));
    QQuickQmlGenerator generator(path,
        QQuickVectorImageGenerator::CurveRenderer | QQuickVectorImageGenerator::AsyncShapes,
        componentPath);
    // Copy authored raster elements into this revision's lease too. The SVG
    // geometry stays vector; generated URLs never depend on external paths.
    generator.setRetainFilePaths(false);
    generator.setAssetFileDirectory(source->preparedFiles->path());
    generator.setAssetFilePrefix(QStringLiteral("asset_"));
    generator.setUrlPrefix(QUrl::fromLocalFile(source->preparedFiles->path() + u'/')
                               .toString(QUrl::FullyEncoded));
    if (!generator.generate() || !generator.save()) return {};
    source->componentUrl = QUrl::fromLocalFile(componentPath);
    {
        QMutexLocker lock(&sourceMutex);
        for (auto it = sourceHandles.begin(); it != sourceHandles.end();) {
            if (it.value().isNull()) it = sourceHandles.erase(it);
            else ++it;
        }
        sourceHandles.insert(source->url, source.toWeakRef());
    }
    return source;
}

void VectorSourcePin::setSource(const QUrl &source) {
    if (_source == source) return;
    ++_generation;
    if (_component) _component->deleteLater();
    _component = nullptr;
    {
        QMutexLocker lock(&sourceMutex);
        _pin = sourceHandles.value(source).toStrongRef();
    }
    _source = source;
    _failed = false;
    _loading = !_source.isEmpty() && !_pin;
    emit sourceChanged();
    emit preparedChanged();
    if (_pin) {
        loadComponent();
    } else if (_loading && _prepareComponent) {
        // Direct local QML consumers also use the worker path. Catalog/VFS
        // surfaces already own a prepared revision from the priority scheduler.
        const auto generation = _generation;
        auto *watcher = new QFutureWatcher<VectorSourcePtr>(this);
        connect(watcher, &QFutureWatcher<VectorSourcePtr>::finished, this,
                [this, watcher, generation] {
            const auto prepared = watcher->result();
            watcher->deleteLater();
            if (_generation != generation) return;
            _pin = prepared;
            _loading = false;
            _failed = !_pin;
            emit preparedChanged();
            if (_pin) loadComponent();
        });
        watcher->setFuture(QtConcurrent::run(rawSourcePool(), [source] {
            if (!source.isLocalFile()) return VectorSourcePtr{};
            ImageInfo info;
            info.path = source.toLocalFile();
            const QFileInfo file(info.path);
            info.fileSize = file.size();
            info.lastModified = file.lastModified();
            return acquireVectorSource(info, {});
        }));
    }
}

VectorSourcePin::~VectorSourcePin() {
    if (_component) _component->deleteLater();
}

void VectorSourcePin::setPrepareComponent(bool prepare) {
    if (_prepareComponent == prepare) return;
    _prepareComponent = prepare;
    if (prepare && !_source.isEmpty() && !_pin) {
        const QUrl source = _source;
        _source = QUrl{};
        setSource(source);
    } else {
        loadComponent();
    }
    emit preparedChanged();
}

void VectorSourcePin::loadComponent() {
    if (!_prepareComponent || !_pin || _component) return;
    auto *engine = qmlEngine(this);
    if (!engine) return;
    _loading = true;
    emit preparedChanged();
    auto *component = new QQmlComponent(engine, engine);
    const auto generation = _generation;
    const QPointer<VectorSourcePin> owner(this);
    // The component, not the surface, owns this capture. A cancelled/replaced
    // surface cannot delete files while the QML compiler still reads them.
    const auto pin = _pin;
    connect(component, &QQmlComponent::statusChanged, component,
            [owner, component, generation, pin](QQmlComponent::Status status) {
        if (status != QQmlComponent::Ready && status != QQmlComponent::Error) return;
        if (!owner || owner->_generation != generation) {
            component->deleteLater();
            return;
        }
        owner->_loading = false;
        owner->_failed = status == QQmlComponent::Error;
        if (owner->_failed) {
            qWarning().noquote() << component->errorString();
            component->deleteLater();
        } else {
            owner->_component = component;
        }
        emit owner->preparedChanged();
    });
    component->loadUrl(pin->componentUrl, QQmlComponent::Asynchronous);
}
