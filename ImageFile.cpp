#include "ImageFile.h"

#include <QFileInfo>
#include <QLocale>

#include <algorithm>
#include <utility>

QString fileSizeToHumanReadable(qint64 size) {
    const qint64 KB = 1024;
    const qint64 MB = 1024 * KB;
    const qint64 GB = 1024 * MB;
    const qint64 TB = 1024 * GB;

    QString result;

    if (size < KB) {
        result = QString::number(size) + " Bytes";
    } else if (size < MB) {
        result = QString::number(size / static_cast<double>(KB), 'f', 2) + " KB";
    } else if (size < GB) {
        result = QString::number(size / static_cast<double>(MB), 'f', 2) + " MB";
    } else if (size < TB) {
        result = QString::number(size / static_cast<double>(GB), 'f', 2) + " GB";
    } else {
        result = QString::number(size / static_cast<double>(TB), 'f', 2) + " TB";
    }

    return result;
}

QString calculateDivision(const QString expression) {
    QStringList operands = expression.split("/");
    if (operands.size() == 2) {
        int dividend = operands[0].toInt();
        int divisor = qMax(1, operands[1].toInt());
        return QString("%1").arg(float(dividend) / divisor);
    }
    return expression;
}


bool isExtensionMatch(const QString &path, const QStringList &pattern) {
    for (const QString &ext : pattern) {
        if (path.endsWith(QString(".") + ext, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

QString ImageFile::folderPath() const {
    return _folderPath;
}

void ImageFile::setFolderPath(const QString &folderPath) {
    if (_folderPath != folderPath) {
        _folderPath = folderPath;
        emit fullPathChanged();
    }
}

QString ImageFile::fileName() const {
    return _fileName;
}

void ImageFile::setFileName(const QString &fileName) {
    if (_fileName != fileName) {
        _fileName = fileName;
        emit fullPathChanged();

        if (_searchText.isEmpty()) {
            emit textChanged();
        }
        refreshDefaultDisplayFields();
    }
}

QImage ImageFile::image() const {
    return _image;
}

void ImageFile::setImage(const QImage &image, const ImageInfo &sourceInfo) {
    _image = image;
    _imageSourceInfo = image.isNull() ? ImageInfo() : sourceInfo;
}

bool ImageFile::imageMatchesSource(const ImageInfo &sourceInfo) const {
    // A size-based "already have a better thumbnail" decision is safe only
    // when both images are proven to come from the exact same file version.
    // Watcher reconciliation intentionally keeps the old frame visible while
    // the replacement is decoded, so the mere presence of _image is not
    // enough to establish that equivalence.
    const bool versionTokenMatches =
        (_imageSourceInfo.sourceVersionToken.isEmpty() &&
         sourceInfo.sourceVersionToken.isEmpty()) ||
        (!_imageSourceInfo.sourceVersionToken.isEmpty() &&
         _imageSourceInfo.sourceVersionToken == sourceInfo.sourceVersionToken);
    return !_image.isNull() && versionTokenMatches &&
           !_imageSourceInfo.sourceIdentity().isEmpty() &&
           _imageSourceInfo.sourceIdentity() == sourceInfo.sourceIdentity() &&
           ((!_imageSourceInfo.source.isValid() &&
             _imageSourceInfo.lastModified.isValid() &&
             sourceInfo.lastModified.isValid() &&
             _imageSourceInfo.lastModified == sourceInfo.lastModified) ||
            (_imageSourceInfo.source.isValid() && sourceInfo.source.isValid())) &&
           _imageSourceInfo.fileSize >= 0 && sourceInfo.fileSize >= 0 &&
           _imageSourceInfo.fileSize == sourceInfo.fileSize;
}

QString ImageFile::imageIdUrl() const {
    if (_imageId.isEmpty()) {
        return "";
    }
    return QStringLiteral("image://") + _imageProviderName + QLatin1Char('/') +
           _imageId;
}

void ImageFile::setImageId(const QString &imageId) {
    if (_imageId != imageId) {
        _imageId = imageId;
        emit imageIdUrlChanged();
    }
}

QString ImageFile::imageProviderName() const {
    return _imageProviderName;
}

void ImageFile::setImageProviderName(const QString &providerName) {
    const QString normalized = providerName.trimmed();
    if (normalized.isEmpty() || _imageProviderName == normalized) {
        return;
    }
    _imageProviderName = normalized;
    emit imageIdUrlChanged();
}

QSize ImageFile::fullSize() const {
    return _fullSize;
}

void ImageFile::setFullSize(const QSize &fullSize) {
    if (_fullSize != fullSize) {
        _fullSize = fullSize;
        emit fullSizeChanged();
    }
}

bool ImageFile::isFolder() const {
    return _isFolder;
}

void ImageFile::setIsFolder(bool isFolder) {
    if (_isFolder != isFolder) {
        _isFolder = isFolder;
        emit isFolderChanged();
        refreshDefaultDisplayFields();
    }
}

bool ImageFile::isImage() const {
    return _isImage;
}

void ImageFile::setIsImage(bool isImage) {
    if (_isImage != isImage) {
        _isImage = isImage;
        emit isImageChanged();
    }
}

bool ImageFile::isFolderView() const {
    return _isFolderView;
}

void ImageFile::setIsFolderView(bool isFolderView) {
    if (_isFolderView != isFolderView) {
        bool oldFolderView = folderView();
        _isFolderView = isFolderView;
        if (oldFolderView != folderView()) {
            emit folderViewChanged();
        }
    }
}

bool ImageFile::isCachedThumbnail() const {
    return _isCachedThumbnail;
}

void ImageFile::setIsCachedThumbnail(bool isCachedThumbnail) {
    _isCachedThumbnail = isCachedThumbnail;
}


QString ImageFile::iconPath() const {
    return _iconPath;
}

void ImageFile::setIconPath(const QString &iconPath) {
    if (_iconPath != iconPath) {
        _iconPath = iconPath;
        emit iconPathChanged();
    }
}

QVariantMap ImageFile::highlightStyle() const {
    return _highlightStyle;
}

void ImageFile::setHighlightStyle(const QVariantMap &highlightStyle) {
    if (_highlightStyle != highlightStyle) {
        _highlightStyle = highlightStyle;
        emit highlightStyleChanged();
    }
}

QVariantMap ImageFile::defaultDisplayFields() const {
    const QFileInfo fileInfo(_fileName);
    QString baseName = _isFolder
        ? _fileName : fileInfo.completeBaseName();
    if (baseName.isEmpty()) {
        baseName = _fileName;
    }
    QVariantMap fields{
        {QStringLiteral("displayBaseName"), baseName},
        {QStringLiteral("displayExtension"),
         _isFolder ? QString() : fileInfo.suffix()},
        {QStringLiteral("sizeText"),
         _info.fileSize >= 0
             ? fileSizeToHumanReadable(_info.fileSize) : QString()},
        {QStringLiteral("mtimeText"),
         _info.lastModified.isValid()
             ? QLocale().toString(_info.lastModified,
                                  QLocale::ShortFormat)
             : QString()},
        {QStringLiteral("modeText"), QString()},
    };
    return fields;
}

void ImageFile::refreshDefaultDisplayFields() {
    if (_displayFields.isEmpty() && _explicitDisplayFieldKeys.isEmpty()) {
        // Standalone entries derive their fields lazily. Notify bindings but
        // avoid constructing several temporary maps while a catalog row is
        // still being initialized (file name, kind and metadata arrive as
        // separate setter calls).
        emit displayFieldsChanged();
        return;
    }
    QVariantMap updated = defaultDisplayFields();
    for (const QString &key : std::as_const(_explicitDisplayFieldKeys)) {
        updated.insert(key, _displayFields.value(key));
    }
    if (_displayFields == updated) {
        return;
    }
    _displayFields = std::move(updated);
    emit displayFieldsChanged();
}

QVariantMap ImageFile::displayFields() const {
    return _displayFields.isEmpty()
        ? defaultDisplayFields() : _displayFields;
}

void ImageFile::setDisplayFields(const QVariantMap &displayFields) {
    _explicitDisplayFieldKeys.clear();
    for (auto it = displayFields.cbegin(); it != displayFields.cend(); ++it) {
        _explicitDisplayFieldKeys.insert(it.key());
    }
    static const QStringList defaultKeys{
        QStringLiteral("displayBaseName"),
        QStringLiteral("displayExtension"),
        QStringLiteral("sizeText"),
        QStringLiteral("mtimeText"),
        QStringLiteral("modeText"),
    };
    const bool suppliesEveryDefault = std::all_of(
        defaultKeys.cbegin(), defaultKeys.cend(),
        [&displayFields](const QString &key) {
            return displayFields.contains(key);
        });
    QVariantMap updated = suppliesEveryDefault
        ? displayFields : defaultDisplayFields();
    if (!suppliesEveryDefault) {
        for (auto it = displayFields.cbegin();
             it != displayFields.cend(); ++it) {
            updated.insert(it.key(), it.value());
        }
    }
    if (_displayFields == updated) {
        return;
    }
    _displayFields = std::move(updated);
    emit displayFieldsChanged();
}

int ImageFile::index() const {
    return _index;
}

void ImageFile::setIndex(int index) {
    if (_index != index) {
        _index = index;
        emit indexChanged();
    }
}


QString ImageFile::nestingInfo() const {
    return _nestingInfo;
}

void ImageFile::setNestingInfo(const QString &nestingInfo) {
    if (_nestingInfo != nestingInfo) {
        _nestingInfo = nestingInfo;
        emit nestingInfoChanged();
    }
}

ImageInfo ImageFile::info() const {
    return _info;
}

void ImageFile::setInfo(const ImageInfo &info) {
    bool oldIsPanorama = isPanorama();
    const QDateTime oldLastModified = lastModified();
    const qint64 oldFileSize = fileSize();
    _info = info;
    if (oldIsPanorama != isPanorama()) {
        emit isPanoramaChanged();
    }
    if (oldLastModified != lastModified()) {
        emit lastModifiedChanged();
    }
    if (oldFileSize != fileSize()) {
        emit fileSizeChanged();
    }
    if (oldLastModified != lastModified() || oldFileSize != fileSize()) {
        refreshDefaultDisplayFields();
    }
}

QDateTime ImageFile::lastModified() const {
    return _info.lastModified;
}

qint64 ImageFile::fileSize() const {
    return _info.fileSize;
}

QList<ImageFile *> ImageFile::subfiles() const {
    return _subfiles;
}

void ImageFile::setSubfiles(const QList<ImageFile *> &subfiles) {
    bool oldFolderView = folderView();
    _subfiles = subfiles;
    for (ImageFile *subfile : _subfiles) {
        subfile->setParent(this);
    }
    if (_subfilesModelUpdateDepth == 0 && oldFolderView != folderView()) {
        emit folderViewChanged();
    }
}

void ImageFile::beginSubfilesModelUpdate() {
    if (_subfilesModelUpdateDepth++ == 0) {
        _folderViewBeforeSubfilesModelUpdate = folderView();
    }
}

void ImageFile::endSubfilesModelUpdate() {
    Q_ASSERT(_subfilesModelUpdateDepth > 0);
    if (_subfilesModelUpdateDepth <= 0 || --_subfilesModelUpdateDepth != 0) {
        return;
    }
    if (_folderViewBeforeSubfilesModelUpdate != folderView()) {
        emit folderViewChanged();
    }
}

ImageFile *ImageFile::imageFileParent() const {
    return _imageFileParent;
}

void ImageFile::setImageFileParent(ImageFile *parent) {
    _imageFileParent = parent;
}

bool ImageFile::isShowAsImage() const {
    return _isShowAsImage;
}

void ImageFile::setIsShowAsImage(bool showAsImage) {
    if (_isShowAsImage != showAsImage) {
        _isShowAsImage = showAsImage;
        emit isShowAsImageChanged();
    }
}

QString ImageFile::fullPath() const {
    return _folderPath.isEmpty() ? _fileName : QDir(_folderPath).filePath(_fileName);
}

bool ImageFile::folderView() const {
    return !_subfiles.isEmpty() || _isFolderView;
}

bool ImageFile::isPanorama() const {
    return _info.exif.value("Panorama").toString() == "True";
}

QVariantList ImageFile::exifList() const {
    QVariantList out;

    bool dateTimeOriginal = _info.exif.contains("DateTime");
    if (dateTimeOriginal || _info.exif.contains("DateTimeCreated")) {
        QVariantMap title;
        title["title"] = true;
        title["text"] = dateTimeOriginal ? "Date and Time" : "Date/Time Created";
        title["icon"] = "qrc:/ZoinGallery/resources/ExifDateTime.svg";
        out.append(title);

        QDateTime dateTime;
        if (dateTimeOriginal) {
            dateTime = _info.exif["DateTime"].toDateTime();
        }
        else {
            dateTime = _info.exif["DateTimeCreated"].toDateTime();
        }

        QVariantMap date;
        date["text"] = dateTime.toString("yyyy, MMMM dd");
        out.append(date);

        QVariantMap time;
        time["text"] = dateTime.toString("hh:mm:ss");
        out.append(time);
    }

    if (_info.exif.contains("Size")) {
        QVariantMap title;
        title["title"] = true;
        title["text"] = "Image";
        title["icon"] = "qrc:/ZoinGallery/resources/ExifImage.svg";
        out.append(title);

        QVariantMap resolution;
        float mp = _fullSize.width() * _fullSize.height() / 1000000;
        if (mp > 1) {
            mp = qRound(mp);
        }
        resolution["text"] = QString("%1x%2 (%3 MP) %4").arg(_info.imageSize.width()).arg(_info.imageSize.height()).arg(mp).arg(_info.orientation);
        out.append(resolution);

        QVariantMap size;
        size["text"] = fileSizeToHumanReadable(_info.exif["Size"].toLongLong());
        out.append(size);
    }

    if (_info.exif.contains("ShutterSpeed") || _info.exif.contains("FNumber") || _info.exif.contains("ISO")) {
        QVariantMap title;
        title["title"] = true;
        title["text"] = "Shooting";
        title["icon"] = "qrc:/ZoinGallery/resources/ExifShooting.svg";
        out.append(title);
    }
    if (_info.exif.contains("ShutterSpeed")) {
        QString shutterString = _info.exif["ShutterSpeed"].toString();
        QStringList shutterValues = shutterString.split("/");
        if (shutterValues.size() == 2) {
            int dividend = shutterValues[0].toInt();
            int divisor = qMax(1, shutterValues[1].toInt());

            float shutterSpeed = float(dividend) / divisor;
            if (shutterSpeed < 0.3) {
                shutterString = QString("1/%1").arg(qRound(1 / shutterSpeed));
            }
            else {
                shutterString = QString("%1 s").arg(shutterSpeed);
            }
        }

        QVariantMap shutterSpeed;
        shutterSpeed["text"] = shutterString;
        out.append(shutterSpeed);
    }
    if (_info.exif.contains("FNumber")) {
        QVariantMap fNumber;
        fNumber["text"] = QString("F") + calculateDivision(_info.exif["FNumber"].toString());
        out.append(fNumber);
    }
    if (_info.exif.contains("ISO")) {
        QVariantMap iso;
        iso["text"] = _info.exif["ISO"].toString() + " ISO";
        out.append(iso);
    }

    if (_info.exif.contains("Camera") || _info.exif.contains("FocalLength") || _info.exif.contains("Lens")) {
        QVariantMap title;
        title["title"] = true;
        title["text"] = "Camera";
        title["icon"] = "qrc:/ZoinGallery/resources/ExifCamera.svg";
        out.append(title);
    }
    if (_info.exif.contains("Camera")) {
        QVariantMap camera;
        camera["text"] = _info.exif["Camera"].toString();
        out.append(camera);
    }
    if (_info.exif.contains("FocalLength")) {
        QVariantMap focalLength;
        focalLength["text"] = _info.exif["FocalLength"].toString() + " mm";
        out.append(focalLength);
    }
    if (_info.exif.contains("Lens")) {
        QVariantMap lens;
        lens["text"] = _info.exif["Lens"].toString();
        out.append(lens);
    }

    if (_info.exif.contains("Location")) {
        QVariantMap title;
        title["title"] = true;
        title["text"] = "Location";
        title["icon"] = "qrc:/ZoinGallery/resources/ExifLocation.svg";
        out.append(title);

        QStringList loc = _info.exif["Location"].toString().split(",");
        QVariantMap location;
        location["text"] = _info.exif["Location"].toString();
        // location["text"] = QString("%1 %2").arg(convertEXIFToDMS(loc[0]), convertEXIFToDMS(loc[1]));
        // location["url"] = "https://www.google.com/maps/place/" + location["text"].toString().replace(" ", "+");
        QStringList latLon = location["text"].toString().split(", ");
        location["url"] = QString("https://www.openstreetmap.org/?mlat=%1&mlon=%2").arg(latLon[0], latLon[1]);
        out.append(location);
    }

    if (_info.exif.contains("png_data")) {
        QVariantList pngData = _info.exif["png_data"].toList();
        for (auto data : pngData) {
            QVariantMap map = data.toMap();
            QString key = map.keys().first();
            if (key.startsWith("XML:")) {
                continue;
            }

            QVariantMap title;
            title["title"] = true;
            title["text"] = key;
            out.append(title);

            QVariantMap value;
            value["text"] = map[key];
            value["multiline"] = true;
            out.append(value);
        }
    }
    return out;
}

QString ImageFile::text() const {
    return _searchText.isEmpty() ? _fileName : _searchText;
}

bool ImageFile::isFilteredOut() const {
    return !_searchText.isEmpty() && _searchText == _fileName;
}

void ImageFile::setSearchText(const QString &searchText) {
    if (_searchText != searchText) {
        _searchText = searchText;
        emit textChanged();

        if (_searchText == _fileName) {
            emit isFilteredOutChanged();
        }
    }
}

bool ImageFile::isSelected() const {
    return _isSelected;
}

void ImageFile::setIsSelected(bool isSelected) {
    if (_isSelected != isSelected) {
        _isSelected = isSelected;
        emit isSelectedChanged();
    }
}

QString ImageFile::selectionGroupId() const {
    return _selectionGroupId;
}

void ImageFile::setSelectionGroupId(const QString &selectionGroupId) {
    if (_selectionGroupId != selectionGroupId) {
        _selectionGroupId = selectionGroupId;
        emit selectionGroupChanged();
    }
}

QColor ImageFile::selectionGroupColor() const {
    return _selectionGroupColor;
}

void ImageFile::setSelectionGroupColor(const QColor &selectionGroupColor) {
    if (_selectionGroupColor != selectionGroupColor) {
        _selectionGroupColor = selectionGroupColor;
        emit selectionGroupChanged();
    }
}
