#ifndef ZOINGALLERY_EXTERNALCATALOGMODEL_H
#define ZOINGALLERY_EXTERNALCATALOGMODEL_H

#include "FileListModel.h"

#include <QAbstractListModel>
#include <QHash>
#include <QSharedPointer>
#include <QSet>
#include <QStringList>
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

    void handleImageInfo(const ImageInfo &info);
    void handleImageReady(const ImageDecodeRequest &request,
                          const QImage &image,
                          const DecodedImageInfo &decodedInfo);
    void handleThumbnailFrameAvailable(const QString &sourceIdentity,
                                       qint64 versionToken,
                                       qint64 sourceFileSize,
                                       const QSize &requestedSize,
                                       const QString &transformKey,
                                       const QString &providerId);
    void handleThumbnailFrameEvicted(const QString &providerId);
    void handleThumbnailRequestReleased(const QString &sourceIdentity,
                                        qint64 versionToken,
                                        qint64 sourceFileSize,
                                        const QSize &requestedSize,
                                        const QString &transformKey,
                                        bool retryWaiters);
    void releaseFailedThumbnailRequest(
        const ImageDecodeRequest &request);
    bool adoptCachedThumbnail(int row);
    void attachThumbnail(int row, const QString &providerId);
    void detachThumbnail(Entry &entry);
    bool catalogMatches(const QVariantList &values) const;
    void clearPublishedImage(Entry &entry);
    void requestImageMetadataForRow(int row, bool highPriority);
    void scheduleMetadataPump();
    void pumpMetadataRequests();
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
    QHash<QString, int> _pathToRow;
    QMultiHash<QString, QString> _sourceEntryIds;
    QMultiHash<QString, QString> _providerEntryIds;
    QHash<QString, qint64> _metadataPendingVersions;
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
    int _cursorRow = -1;
    quint64 _nextImageSerial = 0;
    QString _viewerEntryId;
    QSize _viewerViewportSize;
    QString _lastViewerImageUrl;
    QHash<QString, ViewerPlan> _viewerPlans;
    QSet<QString> _pendingViewerRequests;
    QHash<QString, PendingThumbnailRequest> _pendingThumbnailRequests;
    bool _shutdown = false;
};

} // namespace ZoinGallery

#endif // ZOINGALLERY_EXTERNALCATALOGMODEL_H
