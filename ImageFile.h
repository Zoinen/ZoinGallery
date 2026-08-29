#ifndef IMAGEFILE_H
#define IMAGEFILE_H

#include <QString>
#include <QSize>
#include <QImage>
#include <QMetaType>
#include <QDir>
#include <QDateTime>
#include <QColor>
#include <QSharedPointer>
#include <QVariantMap>
#include <QSet>

#include <ZoinGallery/ImageSourceProvider.h>

#include <memory>

enum ExifOrientation {
    Horizontal = 1,
    MirrorHorizontal = 2,
    Rotate180 = 3,
    MirrorVertical = 4,
    MirrorHorizontalAndRotate270CW = 5,
    Rotate90CW = 6,
    MirrorHorizontalAndRotate90CW = 7,
    Rotate270CW = 8
};

inline QSize rotateToOrientation(QSize size, ExifOrientation orientation) {
    if (orientation == ExifOrientation::Rotate270CW ||
            orientation == ExifOrientation::Rotate90CW ||
            orientation == ExifOrientation::MirrorHorizontalAndRotate270CW ||
            orientation == ExifOrientation::MirrorHorizontalAndRotate90CW) {
        return QSize(size.height(), size.width());
    }
    return size;
}


struct ImageInfo {
    QString path;
    QDateTime lastModified;
    qint64 fileSize = -1;
    // External sources retain their host identity while path is used only as
    // a short-lived decoder backing path. Standalone/local requests leave
    // source invalid and continue to use path as their identity.
    ZoinGallery::ImageSourceDescriptor source;
    QString sourceVersionToken;

    QString sourceIdentity() const {
        return source.isValid() ? source.runtimeIdentity() : path;
    }

    QString formatHint() const {
        return source.isValid() && !source.displayName.isEmpty()
            ? source.displayName : path;
    }

    QSize imageSize;
    ExifOrientation orientation = ExifOrientation::Horizontal;
    QVariantMap exif;

    bool isLast = false;
    bool isFromEmbeddedView = false;
    bool isCached = false;
    bool isFromScanner = false;
    int directOpenGeneration = 0;
    bool highPriority = false;
    // The byte-source transport could not materialize this revision. This is
    // transient (network/offline/timeout), unlike a successfully opened but
    // corrupt or unsupported image, and must not become a permanent metadata
    // result for the catalog generation.
    bool sourceAccessFailed = false;
    // Optional owner for metadata work running on the shared embedded
    // scheduler. Standalone requests leave this empty.
    QString requestNamespace;
};

struct ImageDecodeRequest {
    ImageInfo info;

    QSize targetSize;
    // Optional owner used by a shared embedded scheduler. Standalone requests
    // leave this empty; external sessions stamp their stable session ID.
    QString requestNamespace;
    bool viewerRequest = false;
    // A catalog-wide Fit preparation is stored in the viewer/derived cache,
    // but stays in the background scheduler band. Interactive viewer work
    // for the same source may overtake it and share materialization.
    bool backgroundViewerRequest = false;
    bool checkCache = false;
    // A cold standalone thumbnail decode intentionally prepares a reusable
    // cache-resolution frame before publishing the requested tile. External
    // catalogs carry an opaque host version which the legacy path/stat cache
    // cannot validate, so they disable this expansion and decode the exact
    // masonry/viewer target instead.
    bool expandToCacheResolution = true;
    // Keep exact external decodes out of the legacy path/stat persistent
    // cache. Otherwise a small tile could poison a later request and the
    // cache still could not prove the host's opaque content version.
    bool storeInPersistentCache = true;
    // Keeps Fit intent when a small image's target is already its native size.
    bool fitToViewerRequest = false;
    // Visible/interactively requested work jumps ahead of background scans.
    bool highPriority = false;
    // Set only when an abstract external byte source could not be acquired.
    // Consumers use it to retry transport failures without retrying corrupt
    // local/decoded data forever.
    bool sourceAccessFailed = false;
    // Assigned by DecodeManager so the latest viewer navigation batch stays
    // ahead of stale queued viewer/prefetch work.
    quint64 viewerGeneration = 0;
    int viewerPriorityOrdinal = -1;
    // Distinguishes decoded pixels produced with different thumbnail fit/crop
    // policies. Empty preserves the historical stretched-thumbnail policy.
    QString thumbnailTransformKey;
};

struct DecodedImageInfo {
    QString decoderUsed;
    int decodingTookTime = -1;
    QString previewUsed;
    bool isFromCache = false;
    // Opaque-versioned derived artifacts exactly match their requested tier
    // and do not need the legacy cache-to-source quality upgrade.
    bool isAuthoritativeDerivedCache = false;
};

struct ImageData {
    const ImageDecodeRequest request;

    QByteArray data;
    QString mimeType;

    std::shared_ptr<char> previewData;
    int64_t previewDataSize = 0;
    QString previewMimeType;
    QString previewUsed;

    // Pins a provider-owned local backing file through preview extraction and
    // decode. It is intentionally not copied into persistent cache metadata.
    QSharedPointer<ZoinGallery::ImageSourceLease> sourceLease;

    ImageData(const ImageDecodeRequest &request_) : request(request_) {}
};


class ImageFile : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString text READ text NOTIFY textChanged)
    Q_PROPERTY(int index READ index NOTIFY indexChanged)
    Q_PROPERTY(QString fullPath READ fullPath NOTIFY fullPathChanged)
    Q_PROPERTY(QString nestingInfo READ nestingInfo NOTIFY nestingInfoChanged)
    Q_PROPERTY(bool folderView READ folderView NOTIFY folderViewChanged)
    Q_PROPERTY(QString imageIdUrl READ imageIdUrl NOTIFY imageIdUrlChanged)
    Q_PROPERTY(bool isImage READ isImage NOTIFY isImageChanged)
    Q_PROPERTY(bool isShowAsImage READ isShowAsImage WRITE setIsShowAsImage NOTIFY isShowAsImageChanged)
    Q_PROPERTY(QString iconPath READ iconPath NOTIFY iconPathChanged)
    Q_PROPERTY(bool isFolder READ isFolder NOTIFY isFolderChanged)
    Q_PROPERTY(bool isPanorama READ isPanorama NOTIFY isPanoramaChanged)
    Q_PROPERTY(bool isFilteredOut READ isFilteredOut NOTIFY isFilteredOutChanged)
    Q_PROPERTY(QSize fullSize READ fullSize NOTIFY fullSizeChanged)
    Q_PROPERTY(QDateTime lastModified READ lastModified NOTIFY lastModifiedChanged)
    Q_PROPERTY(qint64 fileSize READ fileSize NOTIFY fileSizeChanged)
    Q_PROPERTY(bool isSelected READ isSelected WRITE setIsSelected NOTIFY isSelectedChanged)
    Q_PROPERTY(QString selectionGroupId READ selectionGroupId WRITE setSelectionGroupId NOTIFY selectionGroupChanged)
    Q_PROPERTY(QColor selectionGroupColor READ selectionGroupColor WRITE setSelectionGroupColor NOTIFY selectionGroupChanged)
    Q_PROPERTY(QVariantMap highlightStyle READ highlightStyle WRITE setHighlightStyle NOTIFY highlightStyleChanged)
    Q_PROPERTY(QVariantMap displayFields READ displayFields WRITE setDisplayFields NOTIFY displayFieldsChanged)

public:
    using QObject::QObject;

    QString folderPath() const;
    void setFolderPath(const QString &folderPath);

    QString fileName() const;
    void setFileName(const QString &fileName);

    QImage image() const;
    void setImage(const QImage &image, const ImageInfo &sourceInfo);
    bool imageMatchesSource(const ImageInfo &sourceInfo) const;

    QString imageIdUrl() const;
    void setImageId(const QString &imageId);
    QString imageProviderName() const;
    void setImageProviderName(const QString &providerName);

    QSize fullSize() const;
    void setFullSize(const QSize &fullSize);

    bool isFolder() const;
    void setIsFolder(bool isFolder);

    bool isImage() const;
    void setIsImage(bool isImage);

    bool isFolderView() const;
    void setIsFolderView(bool isFolderView);

    bool isCachedThumbnail() const;
    void setIsCachedThumbnail(bool isCachedThumbnail);

    QString iconPath() const;
    void setIconPath(const QString &iconPath);

    int index() const;
    void setIndex(int index);

    QString nestingInfo() const;
    void setNestingInfo(const QString &nestingInfo);

    ImageInfo info() const;
    void setInfo(const ImageInfo &info);
    QDateTime lastModified() const;
    qint64 fileSize() const;

    QList<ImageFile *> subfiles() const;
    void setSubfiles(const QList<ImageFile *> &subfiles);
    void beginSubfilesModelUpdate();
    void endSubfilesModelUpdate();

    ImageFile *imageFileParent() const;
    void setImageFileParent(ImageFile *parent);

    bool isShowAsImage() const;
    void setIsShowAsImage(bool showAsImage);

    QString fullPath() const;
    bool folderView() const;
    bool isPanorama() const;

    QVariantList exifList() const;

    QString text() const;

    bool isFilteredOut() const;

    void setSearchText(const QString &searchText);

    bool isSelected() const;
    void setIsSelected(bool isSelected);
    QString selectionGroupId() const;
    void setSelectionGroupId(const QString &selectionGroupId);
    QColor selectionGroupColor() const;
    void setSelectionGroupColor(const QColor &selectionGroupColor);
    QVariantMap highlightStyle() const;
    void setHighlightStyle(const QVariantMap &highlightStyle);
    QVariantMap displayFields() const;
    void setDisplayFields(const QVariantMap &displayFields);

    // Initializes a newly-created external-catalog row without emitting the
    // dozen property notifications that no observer can consume before the
    // enclosing model reset completes.
    void initializeExternalCatalogRow(
        const QString &folderPath, const QString &fileName, int index,
        bool isFolder, bool isImage, const QString &iconPath, bool isSelected,
        const QString &imageProviderName, const ImageInfo &info,
        const QVariantMap &highlightStyle,
        const QVariantMap &displayFields,
        bool deferDisplayMetadata = false);

signals:
    void fullPathChanged();
    void nestingInfoChanged();
    void folderViewChanged();
    void imageIdUrlChanged();
    void isImageChanged();
    void isShowAsImageChanged();
    void iconPathChanged();
    void isFolderChanged();
    void isPanoramaChanged();

    void textChanged();
    void indexChanged();
    void fullSizeChanged();
    void lastModifiedChanged();
    void fileSizeChanged();

    void isFilteredOutChanged();
    void isSelectedChanged();
    void selectionGroupChanged();
    void highlightStyleChanged();
    void displayFieldsChanged();

private:
    QVariantMap defaultDisplayFields() const;
    void refreshDefaultDisplayFields();

    QString _folderPath;
    QString _fileName;
    QImage _image;
    ImageInfo _imageSourceInfo;
    QString _imageId;
    QString _imageProviderName = QStringLiteral("zoingallery-thumbnails");
    QSize _fullSize;
    bool _isFolder = false;
    bool _isImage = false;
    bool _isFolderView = false;
    bool _isCachedThumbnail = false;
    bool _isShowAsImage = false;
    QString _iconPath;
    int _index = -1;
    QString _nestingInfo;
    ImageInfo _info;
    bool _isSelected = false;
    QString _selectionGroupId;
    QColor _selectionGroupColor;
    QVariantMap _highlightStyle;
    QVariantMap _displayFields;
    QSet<QString> _explicitDisplayFieldKeys;

    QList<ImageFile *> _subfiles;
    int _subfilesModelUpdateDepth = 0;
    bool _folderViewBeforeSubfilesModelUpdate = false;
    ImageFile *_imageFileParent = nullptr;
    QString _searchText;
};
Q_DECLARE_METATYPE(ImageFile *)

using ImageFilePtr = std::shared_ptr<ImageFile>;

struct FileInfo {
    QString name;
    QDateTime lastModified;
    qint64 fileSize = -1;
    bool isDirectory = false;
};

struct FolderInfo {
    QString path;
    QList<FileInfo> subfiles;
};

const QSize CACHE_IMAGE_RESOLUTION(1024, 767);

inline QSize expandToCacheImageResolution(QSize targetSize) {
    if (targetSize.width() < CACHE_IMAGE_RESOLUTION.width() &&
        targetSize.height() < CACHE_IMAGE_RESOLUTION.height()) {
        QSizeF sizeScaled = QSizeF(targetSize).scaled(CACHE_IMAGE_RESOLUTION, Qt::KeepAspectRatio);
        targetSize.setWidth(sizeScaled.width());
        targetSize.setHeight(sizeScaled.height() - 1); // TODO: WHAT
    }
    return targetSize;
}

bool isExtensionMatch(const QString &path, const QStringList &pattern);

#endif // IMAGEFILE_H
