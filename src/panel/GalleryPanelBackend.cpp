#include <ZoinGallery/GalleryPanelBackend.h>

#include <ZoinGallery/GalleryCatalogModel.h>

#include <QAbstractItemModel>

namespace ZoinGallery {

GalleryDragDescriptor galleryDragDescriptorFromVariants(
    const QVariantList &urls, const QVariantMap &preview) {
    GalleryDragDescriptor descriptor;
    descriptor.urls = urls;
    const QVariantList items =
        preview.value(QStringLiteral("items")).toList();
    descriptor.previewItems.reserve(items.size());
    for (const QVariant &value : items) {
        const QVariantMap item = value.toMap();
        descriptor.previewItems.append(GalleryDragPreviewItem{
            .imageSource = item.value(QStringLiteral("imageIdUrl")).toUrl(),
            .iconSource = item.value(QStringLiteral("iconPath")).toUrl(),
            .label = item.value(QStringLiteral("text")).toString(),
            .image = item.value(QStringLiteral("isImage")).toBool(),
            .directory = item.value(QStringLiteral("isFolder")).toBool(),
        });
    }
    descriptor.totalCount = preview.value(
        QStringLiteral("totalCount"), descriptor.previewItems.size()).toInt();
    return descriptor;
}

GalleryFileOperationResult galleryFileOperationResultFromVariant(
    const QVariantMap &result) {
    GalleryFileOperationResult operation;
    operation.success = result.value(QStringLiteral("success")).toBool();
    operation.action = static_cast<Qt::DropAction>(
        result.value(QStringLiteral("action"), Qt::IgnoreAction).toInt());
    operation.title = result.value(QStringLiteral("title")).toString();
    operation.message = result.value(QStringLiteral("message")).toString();
    if (!operation.success && operation.message.isEmpty()) {
        const QStringList failed =
            result.value(QStringLiteral("failedPaths")).toStringList();
        if (!failed.isEmpty()) {
            operation.title = QStringLiteral("File operation incomplete");
            operation.message = failed.join(QLatin1Char('\n'));
        }
    }
    return operation;
}

GalleryPanelBackend::GalleryPanelBackend(QObject *parent)
    : QObject(parent) {}

GalleryPanelBackend::~GalleryPanelBackend() = default;

QString GalleryPanelBackend::entryNameAt(int index) const {
    GalleryCatalogModel *catalog = catalogModel();
    return catalog && index >= 0 && index < catalog->rowCount()
        ? catalog->data(catalog->index(index, 0),
                        GalleryCatalogModel::NameRole).toString()
        : QString();
}

bool GalleryPanelBackend::isImageAt(int index) const {
    GalleryCatalogModel *catalog = catalogModel();
    return catalog && index >= 0 && index < catalog->rowCount()
        && catalog->data(catalog->index(index, 0),
                         GalleryCatalogModel::IsImageRole).toBool();
}

QVariantMap GalleryPanelBackend::highlightStyleAt(int index) const {
    GalleryCatalogModel *catalog = catalogModel();
    if (!catalog || index < 0 || index >= catalog->rowCount()) {
        return {};
    }
    return catalog->data(catalog->index(index, 0),
                         GalleryCatalogModel::VisualSnapshotRole)
        .toMap().value(QStringLiteral("highlightStyle")).toMap();
}

QString GalleryPanelBackend::currentPath() const { return {}; }
bool GalleryPanelBackend::catalogReady() const { return true; }
qreal GalleryPanelBackend::panelScrollOffset() const { return 0; }
void GalleryPanelBackend::setPanelScrollOffset(qreal) {}
QString GalleryPanelBackend::panelViewportCursorEntryId() const { return {}; }
void GalleryPanelBackend::setPanelViewportCursorEntryId(const QString &) {}
bool GalleryPanelBackend::panelViewportStateAvailable() const { return false; }
void GalleryPanelBackend::ensurePreviews() {}
bool GalleryPanelBackend::canRemoveEntries() const { return false; }
bool GalleryPanelBackend::canDragEntries() const { return false; }
bool GalleryPanelBackend::canDropIntoDirectories() const { return false; }
bool GalleryPanelBackend::canPreviewDirectories() const { return false; }
GalleryDragDescriptor GalleryPanelBackend::prepareDrag(
    int, bool, int) const { return {}; }
GalleryFileOperationResult GalleryPanelBackend::finalizeExternalDrag(
    const QVariantList &, Qt::DropAction) { return {.success = true}; }
void GalleryPanelBackend::configureNativeDragCursors(QObject *) {}
GalleryFileOperationResult GalleryPanelBackend::dropUrlsIntoDirectory(
    const QVariantList &, int, Qt::DropAction) { return {}; }
void GalleryPanelBackend::removeEntry(int) {}
QAbstractItemModel *GalleryPanelBackend::directoryPreviewModel(int) {
    return nullptr;
}

} // namespace ZoinGallery
