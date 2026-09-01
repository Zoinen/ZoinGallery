#ifndef ZOINGALLERY_GALLERYSESSION_H
#define ZOINGALLERY_GALLERYSESSION_H

#include <QObject>
#include <QSharedPointer>
#include <QSize>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

class QAbstractItemModel;
class ProviderImageStore;
class DecodeManager;

namespace ZoinGallery {

class GalleryRuntime;
class ThumbnailMemoryCache;

class GallerySession final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString sessionId READ sessionId CONSTANT)
    Q_PROPERTY(SourceKind sourceKind READ sourceKind CONSTANT)
    Q_PROPERTY(bool localSource READ localSource CONSTANT)
    Q_PROPERTY(QAbstractItemModel *model READ model CONSTANT)
    Q_PROPERTY(QObject *fileListModel READ fileListModel CONSTANT)
    Q_PROPERTY(QObject *galleryViewModel READ galleryViewModel CONSTANT)
    Q_PROPERTY(QObject *selectedImagesModel READ selectedImagesModel CONSTANT)
    Q_PROPERTY(QObject *imageModel READ imageModel CONSTANT)
    Q_PROPERTY(QString currentPath READ currentPath NOTIFY currentPathChanged)
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(QString cursorEntryId READ cursorEntryId NOTIFY currentIndexChanged)
    Q_PROPERTY(qulonglong catalogRevision READ catalogRevision NOTIFY catalogRevisionChanged)
    Q_PROPERTY(bool catalogReady READ catalogReady NOTIFY catalogReadyChanged)
    Q_PROPERTY(qulonglong selectionRevision READ selectionRevision NOTIFY selectionRevisionChanged)
    Q_PROPERTY(qreal panelScrollOffset READ panelScrollOffset WRITE setPanelScrollOffset
               NOTIFY panelScrollOffsetChanged)
    Q_PROPERTY(QString panelViewportCursorEntryId READ panelViewportCursorEntryId
               WRITE setPanelViewportCursorEntryId
               NOTIFY panelViewportCursorEntryIdChanged)
    Q_PROPERTY(bool panelViewportStateAvailable READ panelViewportStateAvailable
               NOTIFY panelViewportStateAvailableChanged)
    Q_PROPERTY(QString thumbnailProviderName READ thumbnailProviderName CONSTANT)
    Q_PROPERTY(bool viewerOpen READ viewerOpen WRITE setViewerOpen NOTIFY viewerOpenChanged)
    Q_PROPERTY(QUrl viewerSource READ viewerSource NOTIFY viewerSourceChanged)
    Q_PROPERTY(int viewerSourceLevel READ viewerSourceLevel NOTIFY viewerSourceChanged)
    Q_PROPERTY(QString viewerPreviousEntryId READ viewerPreviousEntryId
               NOTIFY viewerPreviousStateChanged)
    Q_PROPERTY(QString viewerPreviousReturnEntryId
               READ viewerPreviousReturnEntryId
               NOTIFY viewerPreviousStateChanged)
    Q_PROPERTY(bool viewerPreviousLocked READ viewerPreviousLocked
               NOTIFY viewerPreviousStateChanged)
    Q_PROPERTY(QVariantMap viewerPreviousViewport READ viewerPreviousViewport
               NOTIFY viewerPreviousStateChanged)
    Q_PROPERTY(bool shutdownComplete READ shutdownComplete NOTIFY shutdownCompleteChanged)

public:
    enum SourceKind {
        LocalFilesystemSource,
        ExternalCatalogSource,
    };
    Q_ENUM(SourceKind)

    ~GallerySession() override;

    QString sessionId() const;
    SourceKind sourceKind() const;
    bool localSource() const;
    QAbstractItemModel *model() const;
    QObject *fileListModel() const;
    QObject *galleryViewModel() const;
    QObject *selectedImagesModel() const;
    QObject *imageModel() const;
    QString currentPath() const;
    int currentIndex() const;
    void setCurrentIndex(int index);
    QString cursorEntryId() const;
    qulonglong catalogRevision() const;
    bool catalogReady() const;
    qulonglong selectionRevision() const;
    qreal panelScrollOffset() const;
    void setPanelScrollOffset(qreal offset);
    QString panelViewportCursorEntryId() const;
    void setPanelViewportCursorEntryId(const QString &entryId);
    bool panelViewportStateAvailable() const;
    QString thumbnailProviderName() const;
    bool viewerOpen() const;
    void setViewerOpen(bool open);
    QUrl viewerSource() const;
    int viewerSourceLevel() const;
    QString viewerPreviousEntryId() const;
    QString viewerPreviousReturnEntryId() const;
    bool viewerPreviousLocked() const;
    QVariantMap viewerPreviousViewport() const;
    bool shutdownComplete() const;

    Q_INVOKABLE bool applyExternalCatalog(
        const QVariantList &entries, qulonglong catalogRevision,
        const QVariantMap &options = QVariantMap());
    Q_INVOKABLE bool appendExternalCatalog(
        const QVariantList &entries, qulonglong catalogRevision,
        int offset, bool final);
    Q_INVOKABLE bool applyExternalCatalogRows(
        const QVariantList &entries, qulonglong catalogRevision);
    Q_INVOKABLE void setExternalCatalogReady(bool ready);
    Q_INVOKABLE bool applyExternalAppearance(
        const QVariantList &entries, qulonglong highlightRevision);
    // Applies a bounded metadata chunk to the current external catalog.
    // Both revisions must exactly match the catalog advertised by the host;
    // stale chunks from a directory that has already been replaced are
    // rejected without changing the model.
    Q_INVOKABLE bool applyExternalMetadata(
        const QVariantList &entries, qulonglong catalogRevision,
        qulonglong metadataRevision, bool final = false);
    Q_INVOKABLE bool applyExternalState(
        const QString &cursorEntryId, int cursorIndex,
        const QStringList &selectedEntryIds, qulonglong selectionRevision);
    // Applies cursor state and a sparse selection overlay without scanning the
    // immutable external catalog. baseSelectionRevision must exactly match
    // the currently installed state; the operation is all-or-nothing.
    Q_INVOKABLE bool applyExternalStateDelta(
        const QString &cursorEntryId, int cursorIndex,
        const QVariantList &selectionChanges,
        qulonglong baseSelectionRevision,
        qulonglong selectionRevision);

    // Compatibility convenience for hosts that carry the path separately.
    Q_INVOKABLE bool applySnapshot(
        const QString &currentPath, const QVariantList &entries,
        qulonglong catalogRevision, qulonglong selectionRevision);
    Q_INVOKABLE bool applySelection(
        const QVariantList &selectedEntryIds, const QString &cursorEntryId,
        qulonglong selectionRevision);
    Q_INVOKABLE int cd(const QString &path,
                       const QString &itemToSelect = QString());

    Q_INVOKABLE QString entryIdAt(int index) const;
    Q_INVOKABLE int indexForEntryId(const QString &entryId) const;
    Q_INVOKABLE QString entryNameAt(int index) const;
    Q_INVOKABLE QString localPathAt(int index) const;
    Q_INVOKABLE bool isImageAt(int index) const;
    Q_INVOKABLE bool isDirectoryAt(int index) const;
    Q_INVOKABLE bool isSelectedAt(int index) const;
    Q_INVOKABLE QVariantMap highlightStyleAt(int index) const;
    Q_INVOKABLE int sourceIndexAt(int index) const;
    Q_INVOKABLE QSize imageOriginalSizeAt(int index) const;
    Q_INVOKABLE int adjacentImageIndex(int fromIndex, int direction) const;
    Q_INVOKABLE QUrl viewerSourceAt(int index) const;
    Q_INVOKABLE QVariantList viewerSourcesAt(int index) const;
    Q_INVOKABLE void activateIndex(int index);
    Q_INVOKABLE void ensurePreviews();
    Q_INVOKABLE void requestViewer(int width, int height);
    Q_INVOKABLE void requestViewerAt(int index, int width, int height);

    Q_INVOKABLE void setViewerPreviousState(
        const QString &previousEntryId,
        const QString &returnEntryId,
        bool locked,
        const QVariantMap &pendingViewport = QVariantMap());
    Q_INVOKABLE void clearViewerPreviousState(bool includeLocked = false);

    Q_INVOKABLE void requestCursor(int index);
    Q_INVOKABLE void requestOpen(int index);
    Q_INVOKABLE void requestToggleSelection(int index);
    void applySelectionIntent(const QStringList &selectedEntryIds,
                              const QStringList &deselectedEntryIds);
    Q_INVOKABLE void resetExternalSource();
    Q_INVOKABLE void shutdown();

signals:
    void currentPathChanged();
    void currentIndexChanged();
    void catalogRevisionChanged();
    void catalogReadyChanged();
    void selectionRevisionChanged();
    void panelScrollOffsetChanged();
    void panelViewportCursorEntryIdChanged();
    void panelViewportStateAvailableChanged();
    void viewerOpenChanged();
    void viewerSourceChanged();
    void viewerSourceAtChanged(int index);
    void viewerPreviousStateChanged();
    void shutdownCompleteChanged();
    void actionRequested(const QString &action, const QVariantMap &payload);
    void viewerRequested(int index, const QUrl &source);

private:
    struct ExternalCatalogApplyContext {
        bool sourceIdentityChanged = false;
        bool requestedCatalogReady = false;
        bool metadataDeferred = false;
        bool catalogRowsDeferred = false;
        int totalCount = -1;
        qulonglong metadataRevision = 0;
        QString previousCursorId;
        int previousIndex = -1;
        bool carriesCursor = false;
        int requestedCursorIndex = -1;
        QString requestedCursorId;
        bool carriesPath = false;
        bool pathChanged = false;
    };

    class Private;
    friend class GalleryRuntime;
    GallerySession(const QString &sessionId,
                   SourceKind sourceKind,
                   const QString &thumbnailProviderName,
                   const QString &asyncProviderName,
                   const QSharedPointer<::ProviderImageStore> &store,
                   const QSharedPointer<ThumbnailMemoryCache> &thumbnailCache,
                   ::DecodeManager *decodeManager,
                   qint64 viewerFitCacheByteBudget,
                   qint64 viewerNativeCacheByteBudget,
                   QObject *parent);
    void sanitizeViewerPreviousStateForCatalog();
    void restorePanelViewportStateForPath(const QString &path);
    void rememberPanelViewportStateForCurrentPath();
    ExternalCatalogApplyContext externalCatalogContext(
        const QVariantList &entries, const QVariantMap &options) const;
    void prepareExternalCatalogPath(
        ExternalCatalogApplyContext &context,
        const QVariantList &entries, const QVariantMap &options);
    bool applySameExternalCatalogRevision(
        const ExternalCatalogApplyContext &context,
        qulonglong revision);
    void commitExternalCatalogState(
        const ExternalCatalogApplyContext &context,
        qulonglong revision, const QVariantMap &options);
    void reconcileExternalCatalogCursor(
        const ExternalCatalogApplyContext &context);
    void finishExternalCatalogApply(
        const ExternalCatalogApplyContext &context);
    Private *d;
};

} // namespace ZoinGallery

#endif // ZOINGALLERY_GALLERYSESSION_H
