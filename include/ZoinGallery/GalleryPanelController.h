#ifndef ZOINGALLERY_GALLERYPANELCONTROLLER_H
#define ZOINGALLERY_GALLERYPANELCONTROLLER_H

#include <ZoinGallery/GalleryCatalogModel.h>
#include <ZoinGallery/GalleryPanelBackend.h>
#include <ZoinGallery/GallerySession.h>

#include <QObject>
#include <QHash>
#include <QPointer>
#include <QVariantList>
#include <QStringList>
#include <memory>

namespace ZoinGallery {

class GalleryQuickSearchIndex;
class GalleryDragPreviewModel;

class GalleryPanelController : public QObject {
    Q_OBJECT
    Q_PROPERTY(GallerySession *session READ session WRITE setSession
               NOTIFY sessionChanged)
    Q_PROPERTY(GalleryPanelBackend *backend READ backend WRITE setBackend
               NOTIFY backendChanged)
    Q_PROPERTY(GalleryCatalogModel *catalogModel READ catalogModel
               NOTIFY backendChanged)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(int visualCursorIndex READ visualCursorIndex
               NOTIFY visualCursorIndexChanged)
    Q_PROPERTY(qulonglong localRevision READ localRevision
               NOTIFY localRevisionChanged)
    Q_PROPERTY(bool cursorIntentPending READ cursorIntentPending
               NOTIFY cursorIntentPendingChanged)
    Q_PROPERTY(int selectionVisualRevision READ selectionVisualRevision
               NOTIFY selectionVisualRevisionChanged)
    Q_PROPERTY(QString cursorEntryId READ cursorEntryId
               NOTIFY currentIndexChanged)
    Q_PROPERTY(QString currentPath READ currentPath
               NOTIFY currentPathChanged)
    Q_PROPERTY(bool catalogReady READ catalogReady
               NOTIFY catalogReadyChanged)
    Q_PROPERTY(qulonglong catalogRevision READ catalogRevision
               NOTIFY catalogRevisionChanged)
    Q_PROPERTY(qulonglong selectionRevision READ selectionRevision
               NOTIFY selectionRevisionChanged)
    Q_PROPERTY(qreal panelScrollOffset READ panelScrollOffset
               WRITE setPanelScrollOffset NOTIFY panelViewportChanged)
    Q_PROPERTY(QString panelViewportCursorEntryId
               READ panelViewportCursorEntryId
               WRITE setPanelViewportCursorEntryId
               NOTIFY panelViewportChanged)
    Q_PROPERTY(bool panelViewportStateAvailable
               READ panelViewportStateAvailable
               NOTIFY panelViewportChanged)
    Q_PROPERTY(QString quickSearchQuery READ quickSearchQuery
               NOTIFY quickSearchChanged)
    Q_PROPERTY(bool quickSearchActive READ quickSearchActive
               NOTIFY quickSearchChanged)
    Q_PROPERTY(int quickSearchMatchCount READ quickSearchMatchCount
               NOTIFY quickSearchChanged)
    Q_PROPERTY(int quickSearchRevision READ quickSearchRevision
               NOTIFY quickSearchChanged)
    Q_PROPERTY(bool canRemoveEntries READ canRemoveEntries
               NOTIFY backendChanged)
    Q_PROPERTY(bool dragEnabled READ dragEnabled NOTIFY backendChanged)
    Q_PROPERTY(bool directoryDropEnabled READ directoryDropEnabled
               NOTIFY backendChanged)
    Q_PROPERTY(bool directoryPreviewEnabled READ directoryPreviewEnabled
               NOTIFY backendChanged)
    Q_PROPERTY(QVariantList dragUrls READ dragUrls
               NOTIFY dragPayloadChanged)
    Q_PROPERTY(QAbstractItemModel *dragPreviewModel READ dragPreviewModel
               CONSTANT)
    Q_PROPERTY(int dragPreviewRemainingCount
               READ dragPreviewRemainingCount NOTIFY dragPayloadChanged)

public:
    explicit GalleryPanelController(QObject *parent = nullptr);
    ~GalleryPanelController() override;

    GallerySession *session() const;
    void setSession(GallerySession *session);
    GalleryPanelBackend *backend() const;
    void setBackend(GalleryPanelBackend *backend);
    GalleryCatalogModel *catalogModel() const;
    int currentIndex() const;
    int visualCursorIndex() const;
    qulonglong localRevision() const;
    bool cursorIntentPending() const;
    int selectionVisualRevision() const;
    QString cursorEntryId() const;
    QString currentPath() const;
    bool catalogReady() const;
    qulonglong catalogRevision() const;
    qulonglong selectionRevision() const;
    qreal panelScrollOffset() const;
    void setPanelScrollOffset(qreal offset);
    QString panelViewportCursorEntryId() const;
    void setPanelViewportCursorEntryId(const QString &entryId);
    bool panelViewportStateAvailable() const;
    QString quickSearchQuery() const;
    bool quickSearchActive() const;
    int quickSearchMatchCount() const;
    int quickSearchRevision() const;
    bool canRemoveEntries() const;
    bool dragEnabled() const;
    bool directoryDropEnabled() const;
    bool directoryPreviewEnabled() const;
    QVariantList dragUrls() const;
    QAbstractItemModel *dragPreviewModel() const;
    int dragPreviewRemainingCount() const;

    Q_INVOKABLE QString entryIdAt(int index) const;
    Q_INVOKABLE int indexForEntryId(const QString &entryId) const;
    Q_INVOKABLE int sourceIndexAt(int index) const;
    Q_INVOKABLE QString entryNameAt(int index) const;
    Q_INVOKABLE bool isImageAt(int index) const;
    Q_INVOKABLE bool isSelectedAt(int index) const;
    Q_INVOKABLE QVariantMap highlightStyleAt(int index) const;
    Q_INVOKABLE void activateIndex(int index);
    Q_INVOKABLE void ensurePreviews();
    Q_INVOKABLE bool setQuickSearchQuery(const QString &query);
    Q_INVOKABLE void clearQuickSearch();
    Q_INVOKABLE int quickSearchNext(bool forward,
                                    bool forceMove = true,
                                    bool wrap = true);
    Q_INVOKABLE QVariantMap quickSearchMatchAt(int index) const;
    Q_INVOKABLE QVariantMap quickSearchMatchForEntry(
        const QString &entryId) const;
    Q_INVOKABLE int quickSearchIndexedRowCount() const;
    Q_INVOKABLE int quickSearchLastVisitedRowCount() const;
    Q_INVOKABLE bool requestCursor(int index, bool deferCommit = false);
    Q_INVOKABLE bool commitPendingCursor();
    Q_INVOKABLE void cancelPendingCursor();
    Q_INVOKABLE bool effectiveSelected(
        const QString &entryId, bool authoritative) const;
    Q_INVOKABLE void beginSelectionGesture(bool add);
    Q_INVOKABLE void previewSelectionRange(int first, int last);
    Q_INVOKABLE void toggleSelectionAt(int index);
    Q_INVOKABLE bool commitSelectionGesture();
    Q_INVOKABLE void cancelSelectionGesture();
    Q_INVOKABLE void acknowledgeCursor(int index, qulonglong localRevision);
    Q_INVOKABLE bool prepareDrag(int index, bool singleItemOnly,
                                 int previewLimit = 5);
    Q_INVOKABLE void configureNativeDragCursors(QObject *dragSource);
    Q_INVOKABLE void finishExternalDrag(int dropAction);
    Q_INVOKABLE int dropUrlsIntoDirectory(
        const QVariantList &urls, int directoryIndex, int dropAction);
    Q_INVOKABLE void removeEntry(int index);
    Q_INVOKABLE QAbstractItemModel *directoryPreviewModelAt(int index);

signals:
    void sessionChanged();
    void backendChanged();
    void currentIndexChanged();
    void visualCursorIndexChanged();
    void localRevisionChanged();
    void cursorIntentPendingChanged();
    void selectionVisualRevisionChanged();
    void currentPathChanged();
    void catalogReadyChanged();
    void catalogRevisionChanged();
    void selectionRevisionChanged();
    void panelViewportChanged();
    void quickSearchChanged();
    void dragPayloadChanged();
    void fileOperationFailed(const QString &title, const QString &message);
    void cursorIntentRequested(const QString &entryId, int viewIndex,
                               int sourceIndex,
                               qulonglong catalogRevision,
                               qulonglong localRevision,
                               bool deferred);
    void selectionIntentRequested(const QStringList &selectedEntryIds,
                                  const QStringList &deselectedEntryIds,
                                  qulonglong catalogRevision,
                                  qulonglong localRevision);

private:
    void connectBackend();
    void connectBackendCursorSignals();
    void connectCatalogInvalidationSignals();
    void connectBackendSelectionSignal();
    void connectBackendLifecycleSignals();
    void disconnectBackend();
    void reconcileAuthoritativeCursor();
    void setVisualCursorIndex(int index);
    void bumpLocalRevision();
    void invalidateQuickSearch();
    void clearDragPayload();
    void reportFileOperationFailure(
        const GalleryFileOperationResult &result);

    QPointer<GallerySession> _session;
    QPointer<GalleryPanelBackend> _backend;
    GalleryPanelBackend *_ownedBackend = nullptr;
    QList<QMetaObject::Connection> _backendConnections;
    int _visualCursorIndex = -1;
    int _pendingCursorIndex = -1;
    QString _pendingCursorEntryId;
    qulonglong _pendingCursorRevision = 0;
    bool _cursorIntentDeferred = false;
    qulonglong _localRevision = 0;
    bool _selectionGestureActive = false;
    bool _selectionAdds = true;
    QHash<QString, bool> _selectionPreview;
    QHash<QString, bool> _selectionAwaiting;
    int _selectionRangeFirst = -1;
    int _selectionRangeLast = -1;
    int _selectionVisualRevision = 0;
    std::unique_ptr<GalleryQuickSearchIndex> _quickSearch;
    int _quickSearchRevision = 0;
    std::unique_ptr<GalleryDragPreviewModel> _dragPreviewModel;
    QVariantList _dragUrls;
    int _dragPreviewTotalCount = 0;
};

} // namespace ZoinGallery

#endif // ZOINGALLERY_GALLERYPANELCONTROLLER_H
