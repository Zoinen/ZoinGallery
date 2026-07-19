#ifndef IMAGEFILE_H
#define IMAGEFILE_H

#include <QString>
#include <QSize>
#include <QImage>
#include <QMetaType>
#include <QDir>
#include <QDateTime>
#include <QSharedPointer>

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

    QSize imageSize;
    ExifOrientation orientation = ExifOrientation::Horizontal;
    QVariantMap exif;

    bool isLast = false;
    bool isFromEmbeddedView = false;
    bool isCached = false;
    bool isFromScanner = false;
    int directOpenGeneration = 0;
};

struct ImageDecodeRequest {
    ImageInfo info;

    QSize targetSize;
    bool viewerRequest = false;
    bool checkCache = false;
};

struct DecodedImageInfo {
    QString decoderUsed;
    int decodingTookTime = -1;
    QString previewUsed;
    bool isFromCache = false;
};

struct ImageData {
    const ImageDecodeRequest request;

    QByteArray data;
    QString mimeType;

    std::shared_ptr<char> previewData;
    int64_t previewDataSize = 0;
    QString previewMimeType;
    QString previewUsed;

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

public:
    using QObject::QObject;

    QString folderPath() const;
    void setFolderPath(const QString &folderPath);

    QString fileName() const;
    void setFileName(const QString &fileName);

    QImage image() const;
    void setImage(const QImage &image);

    QString imageIdUrl() const;
    void setImageId(const QString &imageId);

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

private:
    QString _folderPath;
    QString _fileName;
    QImage _image;
    QString _imageId;
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

    QList<ImageFile *> _subfiles;
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
