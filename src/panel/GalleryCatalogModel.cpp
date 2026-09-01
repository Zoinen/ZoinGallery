#include <ZoinGallery/GalleryCatalogModel.h>
#include <ZoinGallery/GallerySession.h>

#include "ImageFile.h"

namespace ZoinGallery {
namespace {

const QHash<int, QByteArray> &fixedRoles() {
    static const QHash<int, QByteArray> roles{
        {GalleryCatalogModel::EntryIdRole, QByteArrayLiteral("entryId")},
        {GalleryCatalogModel::SourceIndexRole,
         QByteArrayLiteral("sourceIndex")},
        {GalleryCatalogModel::NameRole, QByteArrayLiteral("name")},
        {GalleryCatalogModel::LocalPathRole, QByteArrayLiteral("localPath")},
        {GalleryCatalogModel::IsDirectoryRole, QByteArrayLiteral("isDir")},
        {GalleryCatalogModel::IsImageRole, QByteArrayLiteral("isImage")},
        {GalleryCatalogModel::IsSelectedRole,
         QByteArrayLiteral("isSelected")},
        {GalleryCatalogModel::KnownImageSizeRole,
         QByteArrayLiteral("knownImageSize")},
        {GalleryCatalogModel::ImageIdUrlRole,
         QByteArrayLiteral("imageIdUrl")},
        {GalleryCatalogModel::VisualSnapshotRole,
         QByteArrayLiteral("visualSnapshot")},
        {GalleryCatalogModel::ImageFileRole, QByteArrayLiteral("imageFile")},
        {GalleryCatalogModel::FolderViewRole,
         QByteArrayLiteral("folderView")},
        {GalleryCatalogModel::ExifRole, QByteArrayLiteral("exif")},
        {GalleryCatalogModel::TimeToFlushRole,
         QByteArrayLiteral("timeToFlush")},
        {GalleryCatalogModel::SelectionGroupIdRole,
         QByteArrayLiteral("selectionGroupId")},
        {GalleryCatalogModel::SelectionGroupColorRole,
         QByteArrayLiteral("selectionGroupColor")},
        {GalleryCatalogModel::LastModifiedRole,
         QByteArrayLiteral("lastModified")},
        {GalleryCatalogModel::FileSizeRole,
         QByteArrayLiteral("fileSize")},
        {GalleryCatalogModel::VersionTokenRole,
         QByteArrayLiteral("versionToken")},
    };
    return roles;
}

} // namespace

GalleryCatalogModel::GalleryCatalogModel(QObject *parent)
    : QIdentityProxyModel(parent) {}

ImageFile *GalleryCatalogModel::imageForIndex(
    const QModelIndex &proxyIndex) const {
    const auto imageRole = _sourceRoles.constFind(ImageFileRole);
    return imageRole == _sourceRoles.cend()
        ? nullptr
        : QIdentityProxyModel::data(proxyIndex, *imageRole)
              .value<ImageFile *>();
}

QVariant GalleryCatalogModel::visualSnapshotData(
    int row, ImageFile *image) const {
    if (!image) {
        return QVariantMap{
            {QStringLiteral("valid"), false},
            {QStringLiteral("sourceIndex"),
             _session ? _session->sourceIndexAt(row) : row},
        };
    }
    const QVariantMap style = image->highlightStyle();
    return QVariantMap{
        {QStringLiteral("valid"), true},
        {QStringLiteral("entryId"), image->fullPath()},
        {QStringLiteral("sourceIndex"), image->index()},
        {QStringLiteral("localPath"), image->fullPath()},
        {QStringLiteral("text"), image->text()},
        {QStringLiteral("isFolder"), image->isFolder()},
        {QStringLiteral("isImage"), image->isImage()},
        {QStringLiteral("isSelected"), image->isSelected()},
        {QStringLiteral("iconPath"), image->iconPath()},
        {QStringLiteral("iconKey"),
         style.value(QStringLiteral("iconKey"))},
        {QStringLiteral("highlightStyle"), style},
        {QStringLiteral("displayFields"), image->displayFields()},
        {QStringLiteral("imageIdUrl"), image->imageIdUrl()},
    };
}

QVariant GalleryCatalogModel::derivedData(
    const QModelIndex &proxyIndex, int role, ImageFile *image) const {
    const int row = proxyIndex.row();
    switch (role) {
    case EntryIdRole:
        return image ? image->fullPath()
                     : _session ? _session->entryIdAt(row) : QVariant();
    case SourceIndexRole:
        return image ? image->index()
                     : _session ? _session->sourceIndexAt(row) : QVariant();
    case NameRole:
        return image ? image->text()
                     : _session ? _session->entryNameAt(row) : QVariant();
    case LocalPathRole:
        return image ? image->fullPath()
                     : _session ? _session->localPathAt(row) : QVariant();
    case IsDirectoryRole:
        return image ? image->isFolder()
                     : _session ? _session->isDirectoryAt(row) : QVariant();
    case IsImageRole:
        return image ? image->isImage()
                     : _session ? _session->isImageAt(row) : QVariant();
    case IsSelectedRole:
        return image ? image->isSelected()
                     : _session ? _session->isSelectedAt(row) : QVariant();
    case KnownImageSizeRole:
        return image ? image->fullSize()
                     : _session ? _session->imageOriginalSizeAt(row)
                                : QVariant();
    case ImageIdUrlRole:
        return image ? QVariant(image->imageIdUrl()) : QVariant();
    case FolderViewRole:
        return image ? QVariant(image->folderView()) : QVariant();
    case ExifRole:
        return image ? QVariant(image->exifList()) : QVariant();
    case SelectionGroupIdRole:
        return image ? QVariant(image->selectionGroupId()) : QVariant();
    case SelectionGroupColorRole:
        return image ? QVariant(image->selectionGroupColor()) : QVariant();
    case LastModifiedRole:
        return image ? QVariant(image->lastModified()) : QVariant();
    case FileSizeRole:
        return image ? QVariant(image->fileSize()) : QVariant();
    case VisualSnapshotRole:
        return visualSnapshotData(row, image);
    default:
        return {};
    }
}

QVariant GalleryCatalogModel::data(const QModelIndex &proxyIndex,
                                   int role) const {
    if (role < Qt::UserRole) {
        return QIdentityProxyModel::data(proxyIndex, role);
    }
    const auto sourceRole = _sourceRoles.constFind(role);
    if (sourceRole != _sourceRoles.cend()) {
        return QIdentityProxyModel::data(proxyIndex, *sourceRole);
    }
    return proxyIndex.isValid()
        ? derivedData(proxyIndex, role, imageForIndex(proxyIndex))
        : QVariant();
}


QHash<int, QByteArray> GalleryCatalogModel::roleNames() const {
    return fixedRoles();
}

void GalleryCatalogModel::setSourceModel(QAbstractItemModel *sourceModel) {
    QIdentityProxyModel::setSourceModel(sourceModel);
    rebuildRoleMap();
}

void GalleryCatalogModel::setSession(GallerySession *session) {
    if (_session == session) {
        return;
    }
    beginResetModel();
    _session = session;
    endResetModel();
}

bool GalleryCatalogModel::sparseCatalog() const {
    return sourceModel()
        && sourceModel()->property("sparseCatalog").toBool();
}

QVariantList GalleryCatalogModel::materializedRows() const {
    return sourceModel()
        ? sourceModel()->property("materializedRows").toList()
        : QVariantList{};
}

GalleryCatalogSource *GalleryCatalogModel::catalogSource() const {
    return dynamic_cast<GalleryCatalogSource *>(sourceModel());
}

void GalleryCatalogModel::decodeImages(
    const QList<ImageDecodeRequest> &requests) {
    if (GalleryCatalogSource *source = catalogSource()) {
        source->decodeImages(requests);
    }
}

void GalleryCatalogModel::requestImageMetadata(
    const QList<int> &rows, bool highPriority, bool catalogWide) {
    if (GalleryCatalogSource *source = catalogSource()) {
        source->requestImageMetadata(rows, highPriority, catalogWide);
    }
}

void GalleryCatalogModel::cancelAllRunners() {
    if (GalleryCatalogSource *source = catalogSource()) {
        source->cancelAllRunners();
    }
}

void GalleryCatalogModel::cancelAllDecodeRunners() {
    if (GalleryCatalogSource *source = catalogSource()) {
        source->cancelAllDecodeRunners();
    }
}

bool GalleryCatalogModel::preserveViewStateOnReset() const {
    const GalleryCatalogSource *source = catalogSource();
    return source && source->preserveViewStateOnReset();
}

::ImageFile *GalleryCatalogModel::rootItem() const {
    const GalleryCatalogSource *source = catalogSource();
    return source ? source->rootItem() : nullptr;
}

void GalleryCatalogModel::rebuildRoleMap() {
    _sourceRoles.clear();
    if (!sourceModel()) {
        return;
    }
    QHash<QByteArray, int> byName;
    const QHash<int, QByteArray> sourceNames = sourceModel()->roleNames();
    for (auto role = sourceNames.cbegin(); role != sourceNames.cend(); ++role) {
        byName.insert(role.value(), role.key());
    }
    // Accept the small naming differences used by the historical local and
    // external implementations while publishing one canonical role name.
    const QHash<QByteArray, QList<QByteArray>> aliases{
        {QByteArrayLiteral("entryId"),
         {QByteArrayLiteral("entryIdRole")}},
        {QByteArrayLiteral("sourceIndex"),
         {QByteArrayLiteral("index")}},
        {QByteArrayLiteral("name"),
         {QByteArrayLiteral("text"), QByteArrayLiteral("entryName"),
          QByteArrayLiteral("displayRole")}},
        {QByteArrayLiteral("isDir"),
         {QByteArrayLiteral("isFolder"), QByteArrayLiteral("directory"),
          QByteArrayLiteral("folderRole")}},
        {QByteArrayLiteral("isImage"),
         {QByteArrayLiteral("isImageRole")}},
        {QByteArrayLiteral("isSelected"),
         {QByteArrayLiteral("selected"), QByteArrayLiteral("selectedRole")}},
        {QByteArrayLiteral("imageIdUrl"),
         {QByteArrayLiteral("imageIdUrlRole")}},
        {QByteArrayLiteral("knownImageSize"),
         {QByteArrayLiteral("originalSize"),
          QByteArrayLiteral("imageFullSizeRole")}},
        {QByteArrayLiteral("imageFile"),
         {QByteArrayLiteral("image"), QByteArrayLiteral("imageFileRole")}},
        {QByteArrayLiteral("folderView"),
         {QByteArrayLiteral("folderViewRole")}},
        {QByteArrayLiteral("exif"),
         {QByteArrayLiteral("exifRole")}},
        {QByteArrayLiteral("timeToFlush"),
         {QByteArrayLiteral("timeToFlushRole")}},
        {QByteArrayLiteral("selectionGroupId"),
         {QByteArrayLiteral("selectionGroupIdRole")}},
        {QByteArrayLiteral("selectionGroupColor"),
         {QByteArrayLiteral("selectionGroupColorRole")}},
        {QByteArrayLiteral("lastModified"),
         {QByteArrayLiteral("lastModifiedRole")}},
        {QByteArrayLiteral("fileSize"),
         {QByteArrayLiteral("fileSizeRole")}},
    };
    for (auto fixed = fixedRoles().cbegin(); fixed != fixedRoles().cend();
         ++fixed) {
        int sourceRole = byName.value(fixed.value(), -1);
        if (sourceRole < 0) {
            const QList<QByteArray> candidates = aliases.value(fixed.value());
            for (const QByteArray &candidate : candidates) {
                sourceRole = byName.value(candidate, -1);
                if (sourceRole >= 0) {
                    break;
                }
            }
        }
        if (sourceRole >= 0) {
            _sourceRoles.insert(fixed.key(), sourceRole);
        }
    }
}

} // namespace ZoinGallery
