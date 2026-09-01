#ifndef ZOINGALLERY_EXTERNALCATALOGMODEL_H
#define ZOINGALLERY_EXTERNALCATALOGMODEL_H

#include "FileListModel.h"
#include "ImageProbe.h"

#include <QAbstractListModel>
#include <QHash>
#include <QSharedPointer>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

class DecodeManager;
class ProviderImageStore;

namespace ZoinGallery {

class ThumbnailMemoryCache;
class ExternalCatalogResetTransaction;
class ExternalCatalogSparseExtension;
class ExternalCatalogMetadataTransaction;
class ExternalCatalogRowsTransaction;
class ExternalCatalogAppendTransaction;
class ExternalCatalogMetadataPlanner;
class ExternalCatalogThumbnailPlanner;

class ExternalCatalogModel final : public QAbstractListModel,
                                   public GalleryCatalogSource {
    Q_OBJECT
    Q_PROPERTY(bool sparseCatalog READ sparseCatalog)
    Q_PROPERTY(QVariantList materializedRows READ materializedRows)

public:
    enum ExternalRole {
        EntryIdRole = FileListModel::FileSizeRole + 1,
        SourceIndexRole,
        LocalPathRole,
        VersionTokenRole,
        EntryNameRole,
        KnownImageSizeRole,
        VisualSnapshotRole,
    };

    explicit ExternalCatalogModel(
        QString sessionId, QString thumbnailProviderName,
        QString asyncProviderName,
        QSharedPointer<ProviderImageStore> store,
        QSharedPointer<ThumbnailMemoryCache> thumbnailCache,
        DecodeManager *decodeManager,
        qint64 viewerFitCacheByteBudget,
        qint64 viewerNativeCacheByteBudget,
        QObject *parent = nullptr);
    ~ExternalCatalogModel() override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    bool sparseCatalog() const;
    QVariantList materializedRows() const;

    bool applyCatalog(const QVariantList &entries,
                      bool metadataDeferred = false,
                      bool checkEquivalentCatalog = true,
                      int totalCount = -1);
    bool applyCatalogRows(const QVariantList &entries,
                          bool metadataDeferred = false);
    bool appendCatalog(const QVariantList &entries,
                       bool metadataDeferred = false);
    bool applyMetadata(const QVariantList &entries);
    bool applyAppearance(const QVariantList &entries);
    bool applyState(const QString &cursorEntryId, int cursorIndex,
                    const QStringList &selectedEntryIds,
                    bool updateSelection = true);
    bool applyStateDelta(const QString &cursorEntryId, int cursorIndex,
                         const QVariantList &selectionChanges);

    QString entryIdAt(int row) const;
    QString entryNameAt(int row) const;
    QString localPathAt(int row) const;
    bool isImageAt(int row) const;
    bool isDirectoryAt(int row) const;
    int sourceIndexAt(int row) const;
    QSize imageOriginalSizeAt(int row) const;
    QVariantMap highlightStyleAt(int row) const;
    int rowForEntryId(const QString &entryId) const;
    int cursorRow() const;
    void ensurePreviews();
    void resetExternalSource();
    QString viewerImageUrlAt(int row) const;
    QString bestViewerImageUrlAt(int row) const;
    QList<QPair<QString, int>> viewerImageSourcesAt(int row) const;
    void requestViewer(int row, const QSize &viewportSize);
    void requestViewerAt(int row, const QSize &viewportSize);
    void setViewerIndex(int row);
    void clearViewer();

    // Runtime diagnostics used by lifecycle/cache tests. These expose only
    // bounded accounting, never decoded image storage.
    qsizetype viewerFitFrameCount() const;
    qsizetype viewerNativeFrameCount() const;
    qint64 viewerFitRetainedBytes() const;
    qint64 viewerNativeRetainedBytes() const;
    qint64 viewerFitByteBudget() const;
    qint64 viewerNativeByteBudget() const;

    // Metadata requests are admitted to DecodeManager through a small
    // session-local window. These diagnostics make the bound observable to
    // deterministic performance/lifecycle tests without exposing images.
    qsizetype metadataPendingRequestCount() const;
    qsizetype metadataPeakPendingRequestCount() const;
    quint64 metadataSubmittedBatchCount() const;
    // One warm catalog window is deliberately large enough to resolve the
    // common 100+ image folder in one metadata-cache lookup and one reflow.
    static constexpr qsizetype metadataRequestLimit() { return 128; }
    static constexpr qsizetype metadataRefillLowWatermark() { return 64; }
    static constexpr qsizetype probeRequestLimit() { return 32; }
    static constexpr qsizetype probeRefillLowWatermark() { return 16; }
    static constexpr qsizetype catalogFitRequestLimit() { return 8; }

    void decodeImages(const QList<ImageDecodeRequest> &requests) override;
    void requestImageMetadata(const QList<int> &rows,
                              bool highPriority,
                              bool catalogWide = false) override;
    void cancelAllRunners() override;
    void cancelAllDecodeRunners() override;
    bool preserveViewStateOnReset() const override;
    void shutdown();

signals:
    void viewerImageUrlChanged();
    void viewerSourceAtChanged(int row);

private:
    friend class ExternalCatalogResetTransaction;
    friend class ExternalCatalogSparseExtension;
    friend class ExternalCatalogMetadataTransaction;
    friend class ExternalCatalogRowsTransaction;
    friend class ExternalCatalogAppendTransaction;
    friend class ExternalCatalogMetadataPlanner;
    friend class ExternalCatalogThumbnailPlanner;

    struct Entry {
        QString id;
        int sourceIndex = -1;
        bool loaded = false;
        QString name;
        ImageSourceDescriptor source;
        QString contentVersion;
        QString localPath;
        bool directory = false;
        bool image = false;
        bool selected = false;
        qint64 mtimeNs = 0;
        qint64 size = -1;
        QString sourceIdentity;
        QVariantMap displayFields;
        QVariantMap highlightStyle;
        QString iconPath;
        QString iconKey;
        bool metadataDeferred = false;
        ImageInfo imageInfo;
        QSize originalSize;
        QString thumbnailProviderId;
        QSize thumbnailRequestedSize;
        QString thumbnailTransformKey;
        ImageFile *item = nullptr;
    };

    bool setEntryHighlightStyle(Entry &entry,
                                const QVariantMap &highlightStyle) const;

    struct ViewerPlan {
        QSize viewportSize;
        int prefetchCount = 1;
    };

    struct PendingThumbnailRequest {
        bool owner = false;
        QSize admittedTargetSize;
        QString admittedTransformKey;
    };

    struct BackgroundMetadataRetry {
        QString sourceIdentity;
        QString contentVersion;
        QString resourceId;
        qint64 notBeforeMs = 0;
    };

    struct BackgroundDecodeRetry {
        ImageDecodeRequest request;
        qint64 notBeforeMs = 0;
    };

    struct ImageInfoBatchState {
        int firstChangedRow = -1;
        int lastChangedRow = -1;
        bool flushRequested = false;
        bool catalogFitChanged = false;
        bool viewerMetadataChanged = false;
        bool acceptedNamespace = false;
        bool allChangedMetadataCached = true;
    };

    struct ProbeBatch {
        qsizetype available = 0;
        QList<ImageProbeRequest> highRequests;
        QList<ImageProbeRequest> backgroundRequests;
    };

    void handleImageInfo(const ImageInfo &info);
    void handleImageInfos(const QList<ImageInfo> &infos);
    void applyImageInfoResult(const ImageInfo &info,
                              ImageInfoBatchState &state);
    bool applyImageInfoToRow(int row, const ImageInfo &info,
                             ImageInfoBatchState &state);
    void publishImageInfoBatch(const ImageInfoBatchState &state);
    void handleImageProbe(const ImageProbeResult &result);
    bool acceptImageProbe(const ImageProbeResult &result,
                          QString &sourceIdentity, QString &version);
    void recordProbeStatus(const ImageProbeResult &result,
                           const QString &sourceIdentity,
                           const QString &version);
    QString storeProbePreview(const ImageProbeResult &result,
                              const QString &sourceIdentity,
                              const QString &version);
    void publishProbeRows(const ImageProbeResult &result,
                          const QString &sourceIdentity,
                          const QString &version,
                          const QString &providerId);
    bool finishCatalogProbePass();
    void replanAfterImageProbe(bool completedCatalogProbePass);
    void handleImageReady(const ImageDecodeRequest &request,
                          const QImage &image,
                          const DecodedImageInfo &decodedInfo);
    QList<int> sourceRows(const QString &sourceIdentity) const;
    QList<int> decodeAuthorityRows(
        const ImageDecodeRequest &request) const;
    void clearCompletedDecodeRequest(const ImageDecodeRequest &request);
    Entry *validatedDecodedEntry(int row,
                                 const ImageDecodeRequest &request);
    QList<int> validatedDecodedRows(
        const QList<int> &rows,
        const ImageDecodeRequest &request);
    void deriveCatalogThumbnail(const ImageDecodeRequest &request,
                                const QImage &image, Entry &entry);
    void publishViewerImage(const QList<int> &rows,
                            const ImageDecodeRequest &request,
                            const QImage &image,
                            const DecodedImageInfo &decodedInfo);
    void publishThumbnailImage(Entry &entry,
                               const ImageDecodeRequest &request,
                               const QImage &image);
    void handleImageReadFailed(const ImageDecodeRequest &request);
    void handleThumbnailFrameAvailable(const QString &sourceIdentity,
                                       const QString &versionToken,
                                       qint64 sourceFileSize,
                                       const QSize &requestedSize,
                                       const QString &transformKey,
                                       const QString &providerId);
    void handleThumbnailFrameEvicted(const QString &providerId);
    void handleThumbnailRequestReleased(const QString &sourceIdentity,
                                        const QString &versionToken,
                                        qint64 sourceFileSize,
                                        const QSize &requestedSize,
                                        const QString &transformKey,
                                        bool retryWaiters);
    void releaseFailedThumbnailRequest(
        const ImageDecodeRequest &request, bool retryWaiters = false);
    void scheduleSourceDecodeRetry(const ImageDecodeRequest &request);
    bool adoptCachedThumbnail(int row);
    void attachThumbnail(int row, const QString &providerId);
    void detachThumbnail(Entry &entry);
    bool catalogMatches(const QVariantList &values,
                        bool *carriesAppearance,
                        bool metadataDeferred) const;
    bool tryExtendSparseCatalog(const QVariantList &entries,
                                bool metadataDeferred, int totalCount);
    bool applySparseCatalog(const QVariantList &entries,
                            bool metadataDeferred, int totalCount);
    bool parseCatalogEntry(const QVariantMap &value, int row,
                           bool metadataDeferred, Entry *entry) const;
    ImageFile *ensureItem(int row) const;
    QVariantMap visualSnapshot(int row) const;
    void retireItemAfterReset(ImageFile *item);
    void deleteRetiredItems();
    void clearPublishedImage(Entry &entry);
    void requestImageMetadataForRow(int row, bool highPriority);
    void enqueueProbeRows(const QList<int> &rows, bool highPriority);
    void scheduleProbePump();
    void pumpProbeRequests();
    void appendProbeRow(ProbeBatch &batch, int row, bool highPriority);
    void drainProbeRows(ProbeBatch &batch, QList<int> &rows,
                        bool highPriority);
    void collectCatalogProbeRows(ProbeBatch &batch);
    void submitProbeBatch(const QList<ImageProbeRequest> &requests,
                          bool highPriority);
    void resetProgressivePipeline();
    bool probeResolvedFor(const Entry &entry) const;
    bool probeBarrierReached() const;
    void beginCatalogFitPass();
    void scheduleCatalogFitPump();
    void pumpCatalogFitRequests();
    ImageDecodeRequest catalogFitRequestForRow(int row) const;
    void stabilizeViewerFitRequest(ImageDecodeRequest &request) const;
    void completeCatalogFitRequest(const ImageDecodeRequest &request);
    bool viewerFitWindowReady(int row, int count,
                              const QSize &viewportSize) const;
    void tryScheduleDeferredNative();
    void finishDeferredNativeDwell();
    void invalidateNativeDwell();
    void scheduleMetadataPump();
    void pumpMetadataRequests();
    void scheduleMetadataRetry(const QString &sourceIdentity,
                               const QString &contentVersion,
                               const QString &resourceId,
                               bool background);
    void scheduleBackgroundRetryWake();
    void processBackgroundRetries();
    void resetMetadataPlanner();
    void scheduleViewerDecode();
    void scheduleViewerDecodeAt(int row, const QSize &viewportSize,
                                int prefetchCount);
    QList<int> viewerCandidateRows(int row, int count) const;
    QList<ImageFile *> viewerItems(
        int centerRow, int prefetchCount, int *windowCenterRow = nullptr) const;
    void notifyViewerImageUrlChanged();
    QString viewerRequestKey(const ImageDecodeRequest &request) const;
    QString thumbnailRequestKey(const ImageDecodeRequest &request) const;
    bool validRow(int row) const;
    int logicalRowCount() const;
    const Entry *entryAt(int row) const;
    Entry *entryAt(int row);
    const Entry &loadedEntry(int row) const;
    Entry &loadedEntry(int row);
    QString nextImageId(const Entry &entry);

    QString _sessionId;
    QString _thumbnailProviderName;
    QString _asyncProviderName;
    QSharedPointer<ProviderImageStore> _store;
    QSharedPointer<ThumbnailMemoryCache> _thumbnailCache;
    DecodeManager *_decodeManager = nullptr; // owned by GalleryRuntime
    ViewerImageCache _viewerImageCache;
    QList<Entry> _entries;
    // In paged mode _entries contains only materialized viewport rows.
    // QAbstractItemModel still exposes _virtualRowCount rows; this compact
    // index avoids constructing tens of thousands of empty Entry objects.
    int _virtualRowCount = -1;
    QHash<int, int> _sparseRowToOffset;
    // Removed visible rows must outlive begin/endResetModel so retained QML
    // delegates can rebind directly from old to new. They are deleted on the
    // next event-loop turn, or synchronously during shutdown/destruction.
    QSet<ImageFile *> _retiredItems;
    QHash<QString, int> _idToRow;
    QHash<QString, int> _pathToRow;
    QHash<QString, int> _sourceToRow;
    QMultiHash<QString, QString> _sourceEntryIds;
    QMultiHash<QString, QString> _providerEntryIds;
    QHash<QString, QString> _metadataPendingVersions;
    QSet<QString> _metadataResolvedPaths;
    QList<int> _metadataVisibleRows;
    QSet<int> _metadataLastVisibleRows;
    QList<int> _metadataOverscanRows;
    QList<int> _metadataUrgentRows;
    QList<int> _metadataAdHocRows;
    int _catalogMetadataCursor = 0;
    qsizetype _metadataPeakPending = 0;
    quint64 _metadataSubmittedBatches = 0;
    bool _catalogMetadataRequested = false;
    bool _metadataPumpScheduled = false;
    QHash<QString, int> _metadataRetryAttempts;
    QSet<QString> _metadataRetryScheduled;
    QHash<QString, BackgroundMetadataRetry> _backgroundMetadataRetries;
    QHash<QString, QString> _probePendingVersions;
    QHash<QString, QString> _probeResolvedVersions;
    QSet<QString> _probeRetryableSources;
    QHash<QString, int> _probeRetryAttempts;
    QHash<QString, qint64> _probeRetryNotBeforeMs;
    QList<int> _probeVisibleRows;
    QList<int> _probeOverscanRows;
    QList<int> _probeUrgentRows;
    int _catalogProbeCursor = 0;
    bool _catalogProbeRequested = false;
    bool _probePumpScheduled = false;
    bool _probePassComplete = false;
    QList<int> _catalogFitRows;
    QSet<QString> _catalogFitPendingKeys;
    QSet<QString> _catalogFitResolvedSources;
    QSet<QString> _catalogFitWaitingMetadata;
    bool _catalogFitStarted = false;
    bool _catalogFitPumpScheduled = false;
    int _cursorRow = -1;
    quint64 _nextImageSerial = 0;
    QString _viewerEntryId;
    QSize _viewerViewportSize;
    QSize _lastViewerFitViewportSize = QSize(1920, 1080);
    QString _deferredNativeEntryId;
    QTimer _nativeDwellTimer;
    quint64 _nativeDwellGeneration = 0;
    quint64 _scheduledNativeDwellGeneration = 0;
    QString _scheduledNativeDwellEntryId;
    QSize _scheduledNativeDwellFitViewportSize;
    QString _lastViewerImageUrl;
    QHash<QString, ViewerPlan> _viewerPlans;
    QSet<QString> _pendingViewerRequests;
    QHash<QString, PendingThumbnailRequest> _pendingThumbnailRequests;
    QHash<QString, int> _sourceDecodeRetryAttempts;
    QSet<QString> _sourceDecodeRetryScheduled;
    QHash<QString, BackgroundDecodeRetry> _backgroundDecodeRetries;
    QTimer _backgroundRetryTimer;
    bool _shutdown = false;
};

} // namespace ZoinGallery

#endif // ZOINGALLERY_EXTERNALCATALOGMODEL_H
