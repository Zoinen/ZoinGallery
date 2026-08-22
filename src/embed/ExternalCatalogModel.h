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

class DecodeManager;
class ProviderImageStore;

namespace ZoinGallery {

class ThumbnailMemoryCache;

class ExternalCatalogModel final : public QAbstractListModel,
                                   public ThumbnailsRequestInterface {
    Q_OBJECT

public:
    enum ExternalRole {
        EntryIdRole = FileListModel::FileSizeRole + 1,
        SourceIndexRole,
        LocalPathRole,
        VersionTokenRole,
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

    bool applyCatalog(const QVariantList &entries);
    bool applyAppearance(const QVariantList &entries);
    bool applyState(const QString &cursorEntryId, int cursorIndex,
                    const QStringList &selectedEntryIds,
                    bool updateSelection = true);

    QString entryIdAt(int row) const;
    QString entryNameAt(int row) const;
    QString localPathAt(int row) const;
    bool isImageAt(int row) const;
    bool isDirectoryAt(int row) const;
    int sourceIndexAt(int row) const;
    QSize imageOriginalSizeAt(int row) const;
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
    static constexpr qsizetype metadataRequestLimit() { return 64; }
    static constexpr qsizetype metadataRefillLowWatermark() { return 32; }
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
    struct Entry {
        QString id;
        int sourceIndex = -1;
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
        QString thumbnailProviderId;
        QSize thumbnailRequestedSize;
        QString thumbnailTransformKey;
        ImageFile *item = nullptr;
    };

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

    void handleImageInfo(const ImageInfo &info);
    void handleImageProbe(const ImageProbeResult &result);
    void handleImageReady(const ImageDecodeRequest &request,
                          const QImage &image,
                          const DecodedImageInfo &decodedInfo);
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
                        bool *carriesAppearance) const;
    void clearPublishedImage(Entry &entry);
    void requestImageMetadataForRow(int row, bool highPriority);
    void enqueueProbeRows(const QList<int> &rows, bool highPriority);
    void scheduleProbePump();
    void pumpProbeRequests();
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
    QList<ImageFile *> viewerItems() const;
    void notifyViewerImageUrlChanged();
    QString viewerRequestKey(const ImageDecodeRequest &request) const;
    QString thumbnailRequestKey(const ImageDecodeRequest &request) const;
    bool validRow(int row) const;
    QString nextImageId(const Entry &entry);

    QString _sessionId;
    QString _thumbnailProviderName;
    QString _asyncProviderName;
    QSharedPointer<ProviderImageStore> _store;
    QSharedPointer<ThumbnailMemoryCache> _thumbnailCache;
    DecodeManager *_decodeManager = nullptr; // owned by GalleryRuntime
    ViewerImageCache _viewerImageCache;
    QList<Entry> _entries;
    QHash<QString, int> _idToRow;
    QHash<QString, int> _sourceToRow;
    QMultiHash<QString, QString> _sourceEntryIds;
    QMultiHash<QString, QString> _providerEntryIds;
    QHash<QString, QString> _metadataPendingVersions;
    QSet<QString> _metadataResolvedPaths;
    QList<int> _metadataVisibleRows;
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
