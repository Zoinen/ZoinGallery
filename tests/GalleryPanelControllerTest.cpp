#include <ZoinGallery/GalleryCatalogModel.h>
#include <ZoinGallery/GalleryPanelBackend.h>
#include <ZoinGallery/GalleryPanelController.h>

#include <QAbstractListModel>
#include <QPointer>
#include <QSignalSpy>
#include <QTest>
#include <QUrl>

namespace {

class CatalogSource final : public QAbstractListModel {
public:
    enum Role {
        EntryIdRole = Qt::UserRole + 17,
        SourceIndexRole,
        NameRole,
        SelectedRole,
    };

    explicit CatalogSource(QObject *parent = nullptr)
        : QAbstractListModel(parent),
          _ids{QStringLiteral("one"), QStringLiteral("two"),
               QStringLiteral("three"), QStringLiteral("four")} {}

    int rowCount(const QModelIndex &parent = QModelIndex()) const override {
        return parent.isValid() ? 0 : _ids.size();
    }

    QVariant data(const QModelIndex &index, int role) const override {
        if (!index.isValid() || index.row() < 0
            || index.row() >= _ids.size()) {
            return {};
        }
        switch (role) {
        case EntryIdRole:
            return _ids[index.row()];
        case SourceIndexRole:
            return 100 + index.row();
        case NameRole:
            return _ids[index.row()] + QStringLiteral(".jpg");
        case SelectedRole:
            return _selected.value(index.row(), false);
        default:
            return {};
        }
    }

    QHash<int, QByteArray> roleNames() const override {
        return {{EntryIdRole, QByteArrayLiteral("entryId")},
                {SourceIndexRole, QByteArrayLiteral("sourceIndex")},
                {NameRole, QByteArrayLiteral("entryName")},
                {SelectedRole, QByteArrayLiteral("selectedRole")}};
    }

    QString idAt(int index) const {
        return index >= 0 && index < _ids.size() ? _ids[index] : QString();
    }

    int indexForId(const QString &id) const {
        return _ids.indexOf(id);
    }

    bool selectedAt(int index) const {
        return _selected.value(index, false);
    }

    void setSelected(const QString &id, bool selected) {
        const int row = indexForId(id);
        if (row < 0 || _selected.value(row, false) == selected) {
            return;
        }
        _selected.insert(row, selected);
        emit dataChanged(index(row, 0), index(row, 0), {SelectedRole});
    }

private:
    QStringList _ids;
    QHash<int, bool> _selected;
};

class TestBackend final : public ZoinGallery::GalleryPanelBackend {
public:
    explicit TestBackend(bool remote, bool capabilities = false,
                         QObject *parent = nullptr)
        : GalleryPanelBackend(parent), _source(this), _catalog(this),
          _remote(remote), _capabilities(capabilities) {
        _catalog.setSourceModel(&_source);
    }

    ZoinGallery::GalleryCatalogModel *catalogModel() const override {
        return const_cast<ZoinGallery::GalleryCatalogModel *>(&_catalog);
    }
    int currentIndex() const override { return _current; }
    qulonglong catalogRevision() const override { return _catalogRevision; }
    qulonglong selectionRevision() const override {
        return _selectionRevision;
    }
    QString entryIdAt(int index) const override { return _source.idAt(index); }
    int indexForEntryId(const QString &id) const override {
        return _source.indexForId(id);
    }
    int sourceIndexAt(int index) const override { return 100 + index; }
    bool isSelectedAt(int index) const override {
        return _source.selectedAt(index);
    }
    bool canRemoveEntries() const override { return _capabilities; }
    bool canDragEntries() const override { return _capabilities; }
    bool canDropIntoDirectories() const override { return _capabilities; }
    bool canPreviewDirectories() const override { return _capabilities; }
    ZoinGallery::GalleryDragDescriptor prepareDrag(
        int index, bool singleItemOnly, int previewLimit) const override {
        lastDragIndex = index;
        lastSingleItemOnly = singleItemOnly;
        lastPreviewLimit = previewLimit;
        ZoinGallery::GalleryDragDescriptor descriptor;
        descriptor.urls = {
            QUrl::fromLocalFile(QStringLiteral("C:/one.jpg")),
            QUrl::fromLocalFile(QStringLiteral("C:/two.jpg")),
            QUrl::fromLocalFile(QStringLiteral("C:/three.jpg")),
            QUrl::fromLocalFile(QStringLiteral("C:/four.jpg")),
        };
        // Deliberately return more previews than requested. The controller is
        // the public boundary and must enforce its own bounded-QML contract.
        descriptor.previewItems = {
            {.imageSource = QUrl(QStringLiteral("image://preview/one")),
             .label = QStringLiteral("one")},
            {.iconSource = QUrl(QStringLiteral("qrc:/icons/two.svg")),
             .label = QStringLiteral("two"), .directory = true},
            {.imageSource = QUrl(QStringLiteral("image://preview/three")),
             .label = QStringLiteral("three"), .image = true},
        };
        descriptor.totalCount = 4;
        return descriptor;
    }
    ZoinGallery::GalleryFileOperationResult finalizeExternalDrag(
        const QVariantList &urls, Qt::DropAction action) override {
        finalizedUrls = urls;
        finalizedAction = action;
        if (failFinalize) {
            return {.success = false,
                    .title = QStringLiteral("Finalize failed"),
                    .message = QStringLiteral("finalize detail")};
        }
        return {.success = true, .action = action};
    }
    void configureNativeDragCursors(QObject *dragSource) override {
        configuredDragSource = dragSource;
    }
    ZoinGallery::GalleryFileOperationResult dropUrlsIntoDirectory(
        const QVariantList &urls, int directoryIndex,
        Qt::DropAction action) override {
        droppedUrls = urls;
        droppedDirectoryIndex = directoryIndex;
        droppedAction = action;
        if (failDrop) {
            return {.success = false,
                    .title = QStringLiteral("Drop failed"),
                    .message = QStringLiteral("drop detail")};
        }
        return {.success = true, .action = action};
    }
    void removeEntry(int index) override { removedIndex = index; }
    QAbstractItemModel *directoryPreviewModel(int index) override {
        previewDirectoryIndex = index;
        return &_source;
    }
    bool remoteAuthoritative() const override { return _remote; }
    void activateIndex(int index) override { setAuthoritativeIndex(index); }
    void applySelectionIntent(const QStringList &selected,
                              const QStringList &deselected) override {
        if (_remote) {
            return;
        }
        acknowledgeSelection(selected, deselected);
    }

    void setAuthoritativeIndex(int index) {
        if (_current == index) {
            return;
        }
        _current = index;
        emit currentIndexChanged();
    }

    void acknowledgeSelection(const QStringList &selected,
                              const QStringList &deselected) {
        for (const QString &id : selected) {
            _source.setSelected(id, true);
        }
        for (const QString &id : deselected) {
            _source.setSelected(id, false);
        }
        ++_selectionRevision;
        emit selectionRevisionChanged();
    }

    mutable int lastDragIndex = -1;
    mutable bool lastSingleItemOnly = false;
    mutable int lastPreviewLimit = -1;
    QVariantList finalizedUrls;
    Qt::DropAction finalizedAction = Qt::IgnoreAction;
    QPointer<QObject> configuredDragSource;
    QVariantList droppedUrls;
    int droppedDirectoryIndex = -1;
    Qt::DropAction droppedAction = Qt::IgnoreAction;
    int removedIndex = -1;
    int previewDirectoryIndex = -1;
    bool failFinalize = false;
    bool failDrop = false;

private:
    CatalogSource _source;
    ZoinGallery::GalleryCatalogModel _catalog;
    bool _remote = true;
    bool _capabilities = false;
    int _current = 0;
    qulonglong _catalogRevision = 7;
    qulonglong _selectionRevision = 0;
};

class GalleryPanelControllerTest final : public QObject {
    Q_OBJECT

private slots:
    void catalogFacadePublishesFixedRolesWithoutCopyingRows() {
        TestBackend backend(true);
        const auto *catalog = backend.catalogModel();
        QCOMPARE(catalog->rowCount(), 4);
        QCOMPARE(catalog->roleNames().value(
                     ZoinGallery::GalleryCatalogModel::EntryIdRole),
                 QByteArrayLiteral("entryId"));
        QCOMPARE(catalog->data(catalog->index(2, 0),
                               ZoinGallery::GalleryCatalogModel::NameRole)
                     .toString(),
                 QStringLiteral("three.jpg"));
    }

    void pendingRemoteCursorDoesNotRollBackOnStaleState() {
        TestBackend backend(true);
        ZoinGallery::GalleryPanelController controller;
        controller.setBackend(&backend);
        QSignalSpy intents(&controller,
                           &ZoinGallery::GalleryPanelController::cursorIntentRequested);

        QVERIFY(controller.requestCursor(2, true));
        QCOMPARE(controller.visualCursorIndex(), 2);
        QVERIFY(controller.cursorIntentPending());
        QCOMPARE(intents.size(), 1);
        QCOMPARE(intents.constFirst().at(2).toInt(), 102);
        QVERIFY(intents.constFirst().at(5).toBool());

        backend.setAuthoritativeIndex(0);
        QCOMPARE(controller.currentIndex(), 0);
        QCOMPARE(controller.visualCursorIndex(), 2);

        QVERIFY(controller.commitPendingCursor());
        QCOMPARE(intents.size(), 2);
        QVERIFY(!intents.constLast().at(5).toBool());
        const qulonglong revision = controller.localRevision();
        controller.acknowledgeCursor(2, revision);
        QVERIFY(!controller.cursorIntentPending());
        QCOMPARE(controller.visualCursorIndex(), 2);
    }

    void selectionPreviewIsIncrementalAndSurvivesUntilAcknowledged() {
        TestBackend backend(true);
        ZoinGallery::GalleryPanelController controller;
        controller.setBackend(&backend);
        QSignalSpy intents(
            &controller,
            &ZoinGallery::GalleryPanelController::selectionIntentRequested);

        controller.beginSelectionGesture(true);
        controller.previewSelectionRange(1, 3);
        QVERIFY(controller.effectiveSelected(QStringLiteral("two"), false));
        controller.previewSelectionRange(2, 3);
        QVERIFY(!controller.effectiveSelected(QStringLiteral("two"), false));
        QVERIFY(controller.effectiveSelected(QStringLiteral("three"), false));
        QVERIFY(controller.commitSelectionGesture());
        QCOMPARE(intents.size(), 1);

        const QStringList selected = intents.constFirst().at(0).toStringList();
        QCOMPARE(selected.size(), 2);
        QVERIFY(selected.contains(QStringLiteral("three")));
        QVERIFY(selected.contains(QStringLiteral("four")));
        QVERIFY(controller.effectiveSelected(QStringLiteral("three"), false));

        backend.acknowledgeSelection(selected, {});
        QVERIFY(controller.effectiveSelected(QStringLiteral("three"), true));
        QVERIFY(!controller.effectiveSelected(QStringLiteral("two"), false));
    }

    void standaloneCapabilitiesStayTypedAndBounded() {
        TestBackend backend(false, true);
        ZoinGallery::GalleryPanelController controller;
        controller.setBackend(&backend);

        QVERIFY(controller.canRemoveEntries());
        QVERIFY(controller.dragEnabled());
        QVERIFY(controller.directoryDropEnabled());
        QVERIFY(controller.directoryPreviewEnabled());

        QSignalSpy payloadChanged(
            &controller,
            &ZoinGallery::GalleryPanelController::dragPayloadChanged);
        QVERIFY(controller.prepareDrag(2, true, 2));
        QCOMPARE(backend.lastDragIndex, 2);
        QVERIFY(backend.lastSingleItemOnly);
        QCOMPARE(backend.lastPreviewLimit, 2);
        QCOMPARE(controller.dragUrls().size(), 4);
        QCOMPARE(controller.dragPreviewModel()->rowCount(), 2);
        QCOMPARE(controller.dragPreviewRemainingCount(), 2);
        QCOMPARE(payloadChanged.size(), 1);

        const QHash<int, QByteArray> roles =
            controller.dragPreviewModel()->roleNames();
        const int labelRole = roles.key(QByteArrayLiteral("label"), -1);
        const int directoryRole =
            roles.key(QByteArrayLiteral("isDirectory"), -1);
        QVERIFY(labelRole >= Qt::UserRole);
        QCOMPARE(controller.dragPreviewModel()->data(
                     controller.dragPreviewModel()->index(1, 0), labelRole)
                     .toString(),
                 QStringLiteral("two"));
        QVERIFY(controller.dragPreviewModel()->data(
                    controller.dragPreviewModel()->index(1, 0),
                    directoryRole).toBool());

        QObject dragSource;
        controller.configureNativeDragCursors(&dragSource);
        QCOMPARE(backend.configuredDragSource.data(), &dragSource);

        controller.finishExternalDrag(Qt::MoveAction);
        QCOMPARE(backend.finalizedUrls.size(), 4);
        QCOMPARE(backend.finalizedAction, Qt::MoveAction);
        QCOMPARE(controller.dragUrls().size(), 0);
        QCOMPARE(controller.dragPreviewModel()->rowCount(), 0);
        QCOMPARE(controller.dragPreviewRemainingCount(), 0);

        const QVariantList dropped = {
            QUrl::fromLocalFile(QStringLiteral("C:/incoming.jpg"))};
        QCOMPARE(controller.dropUrlsIntoDirectory(
                     dropped, 3, Qt::CopyAction),
                 static_cast<int>(Qt::CopyAction));
        QCOMPARE(backend.droppedDirectoryIndex, 3);
        QCOMPARE(backend.droppedAction, Qt::CopyAction);
        QCOMPARE(backend.droppedUrls, dropped);

        QSignalSpy failures(
            &controller,
            &ZoinGallery::GalleryPanelController::fileOperationFailed);
        backend.failDrop = true;
        QCOMPARE(controller.dropUrlsIntoDirectory(
                     dropped, 1, Qt::MoveAction),
                 static_cast<int>(Qt::IgnoreAction));
        QCOMPARE(failures.size(), 1);
        QCOMPARE(failures.constFirst().at(0).toString(),
                 QStringLiteral("Drop failed"));
        QCOMPARE(failures.constFirst().at(1).toString(),
                 QStringLiteral("drop detail"));

        controller.removeEntry(1);
        QCOMPARE(backend.removedIndex, 1);
        QCOMPARE(controller.directoryPreviewModelAt(3),
                 static_cast<QAbstractItemModel *>(backend.catalogModel()
                     ->sourceModel()));
        QCOMPARE(backend.previewDirectoryIndex, 3);
    }

    void unavailableCapabilitiesDoNotInvokeBackend() {
        TestBackend backend(false);
        ZoinGallery::GalleryPanelController controller;
        controller.setBackend(&backend);

        QVERIFY(!controller.canRemoveEntries());
        QVERIFY(!controller.dragEnabled());
        QVERIFY(!controller.directoryDropEnabled());
        QVERIFY(!controller.directoryPreviewEnabled());
        QVERIFY(!controller.prepareDrag(2, false, 5));
        controller.removeEntry(2);
        QCOMPARE(backend.removedIndex, -1);
        QCOMPARE(controller.directoryPreviewModelAt(2), nullptr);
    }
};

} // namespace

QTEST_MAIN(GalleryPanelControllerTest)
#include "GalleryPanelControllerTest.moc"
