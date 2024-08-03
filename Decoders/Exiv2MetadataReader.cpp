#include "Exiv2MetadataReader.h"

#include <exiv2/exiv2.hpp>
#include <exiv2/preview.hpp>
#include "exiv2/Preview.h"

#include <iostream>

static const QStringList TiffRawExtensions = {"tiff", "tif", "cr2", "dng", "crw", "nef", "arw", "arq"};
static const QStringList JpegExtensions = {"jpg", "jpeg", "jpe", "jfif"};
QStringList Exiv2Extensions = {"exv", "mrw", "webp", "pef", "rw2", "sr2", "srw", "orf", "png", "pgf", "raf", "eps", "xmp", "gif", "psd", "tga", "bmp", "jp2"};

#ifdef _WIN32
#define NULL_DEVICE "NUL:"
#else
#define NULL_DEVICE "/dev/null"
#endif

void Exiv2MetadataReader::init() {
    Exiv2::XmpParser::initialize();
    ::atexit(Exiv2::XmpParser::terminate);
#ifdef EXV_ENABLE_BMFF
    Exiv2::enableBMFF();
#endif

#if defined(Q_OS_WIN)
    freopen(NULL_DEVICE, "w", stderr);
#endif
}

bool Exiv2MetadataReader::readMetadata(ImageInfo &result) {
    if (!isFormatSupported(result.path)) {
        return false;
    }

    QFile f(result.path);
    if (!f.open(QFile::ReadOnly)) {
        return false;
    }
    uchar *mappedFile = f.map(0, f.size());
    if (!mappedFile) {
        f.unmap(mappedFile);
        return false;
    }

    try {
        Exiv2::Image::AutoPtr image = Exiv2::ImageFactory::open(mappedFile, f.size());

        assert(image.get() != 0);
        image->readMetadata();
        result.orientation = readOrientationFromExif(image.get());
        if (isExtensionMatch(result.path, JpegExtensions)) {
            result.imageSize = QSize(image->pixelWidth(), image->pixelHeight());
        }
        else {
            result.imageSize = readResolutionFromExif(image.get());
        }
        result.exif = readExifToMap(image.get());
        result.exif["Size"] = f.size();
    } catch (Exiv2::AnyError& e) {
        std::cout << "Caught Exiv2 exception '" << e << "'" << std::endl;
        f.unmap(mappedFile);
        return false;
    }
    f.unmap(mappedFile);
    return true;
}

bool Exiv2MetadataReader::isFormatSupported(const QString &path) {
    return isExtensionMatch(path, Exiv2Extensions) || isExtensionMatch(path, JpegExtensions) || isExtensionMatch(path, TiffRawExtensions);
}

bool Exiv2MetadataReader::readPreviewAndMime(ImageData &result) {
    if (!isFormatSupported(result.request.info.path)) {
        return false;
    }

    if (isExtensionMatch(result.request.info.path, JpegExtensions)) {
        return false;
    }

    QFile f(result.request.info.path);
    if (!f.open(QFile::ReadOnly)) {
        return false;
    }
    uchar *mappedFile = f.map(0, f.size());
    if (!mappedFile) {
        f.unmap(mappedFile);
        return false;
    }

    try {
        Exiv2::Image::AutoPtr image = Exiv2::ImageFactory::open(mappedFile, f.size());

        assert(image.get() != 0);
        image->readMetadata();
        result.mimeType = QString::fromStdString(image->mimeType());
        fixMimeType(result.mimeType, result.request.info.path);

        Exiv2::PreviewProperties previewProp;

        // Blacklisting second thumbnail in cr2 since it's a weird tif with very dark image
        int ignoreThumbnailAt = -1;
        if (result.request.info.path.endsWith(".cr2", Qt::CaseInsensitive)) {
            ignoreThumbnailAt = 2;
        }
        bool largerImageAvailable = false;

        QSize targetSize = result.request.targetSize;
        if (!result.request.checkCache) {
            targetSize = expandToCacheImageResolution(targetSize);
        }
        QSize preferredSizeRotated = rotateToOrientation(targetSize, result.request.info.orientation);
        Exiv2::DataBuf previewImg = Exiv2Preview::preview(*image.get(), preferredSizeRotated.width(),
                                                          preferredSizeRotated.height(), &previewProp, ignoreThumbnailAt,
                                                          &largerImageAvailable);
        if (previewImg.pData_) {
            result.data = QByteArray::fromRawData(reinterpret_cast<char *>(previewImg.pData_), previewImg.size_);
            result.previewData.reset(reinterpret_cast<char *>(previewImg.pData_));
            result.previewDataSize = previewImg.size_;
            result.previewMimeType = QString::fromStdString(previewProp.mimeType_);
            fixMimeType(result.mimeType, result.request.info.path);
            previewImg.release();
            f.unmap(mappedFile);
            return true;
        }
    } catch (Exiv2::AnyError& e) {
        std::cout << "Caught Exiv2 exception '" << e << "'" << std::endl;
        f.unmap(mappedFile);
        return false;
    }
    f.unmap(mappedFile);
    return false;
}

ExifOrientation Exiv2MetadataReader::readOrientationFromExif(Exiv2::Image *image) {
    ExifOrientation orientation = Horizontal;
    const Exiv2::ExifData &exifData = image->exifData();
    if (!exifData.empty()) {
        auto it = exifData.findKey(Exiv2::ExifKey("Exif.Image.Orientation"));
        if (it != exifData.end()) {
            orientation = (ExifOrientation)it->toLong();
        }
    }
    return orientation;
}

QSize Exiv2MetadataReader::readResolutionFromExif(Exiv2::Image *image) {
    QSize size;
    const Exiv2::ExifData &exifData = image->exifData();
    if (!exifData.empty()) {
        if (!size.isValid()) {
            auto widthIt = exifData.findKey(Exiv2::ExifKey("Exif.Photo.PixelXDimension"));
            auto heightIt = exifData.findKey(Exiv2::ExifKey("Exif.Photo.PixelYDimension"));
            if (widthIt != exifData.end() && heightIt != exifData.end()) {
                size = QSize(widthIt->toLong(), heightIt->toLong());
            }
        }

        if (!size.isValid()) {
            auto widthIt = exifData.findKey(Exiv2::ExifKey("Exif.SubImage3.ImageWidth"));
            auto heightIt = exifData.findKey(Exiv2::ExifKey("Exif.SubImage3.ImageLength"));
            if (widthIt != exifData.end() && heightIt != exifData.end()) {
                size = QSize(widthIt->toLong(), heightIt->toLong());
            }
        }

        if (!size.isValid()) {
            auto widthIt = exifData.findKey(Exiv2::ExifKey("Exif.SubImage2.ImageWidth"));
            auto heightIt = exifData.findKey(Exiv2::ExifKey("Exif.SubImage2.ImageLength"));
            if (widthIt != exifData.end() && heightIt != exifData.end()) {
                size = QSize(widthIt->toLong(), heightIt->toLong());
            }
        }

        if (!size.isValid()) {
            auto widthIt = exifData.findKey(Exiv2::ExifKey("Exif.SubImage1.ImageWidth"));
            auto heightIt = exifData.findKey(Exiv2::ExifKey("Exif.SubImage1.ImageLength"));
            if (widthIt != exifData.end() && heightIt != exifData.end()) {
                size = QSize(widthIt->toLong(), heightIt->toLong());
            }
        }

        if (!size.isValid()) {
            auto sizeIt = exifData.findKey(Exiv2::ExifKey("Exif.SubImage1.DefaultCropSize"));
            if (sizeIt != exifData.end() && sizeIt->count() == 2) {
                size = QSize(sizeIt->toLong(0), sizeIt->toLong(1));
            }
        }

        if (!size.isValid()) {
            auto widthIt = exifData.findKey(Exiv2::ExifKey("Exif.Image.ImageWidth"));
            auto heightIt = exifData.findKey(Exiv2::ExifKey("Exif.Image.ImageLength"));
            if (widthIt != exifData.end() && heightIt != exifData.end()) {
                size = QSize(widthIt->toLong(), heightIt->toLong());
            }
        }
    }
    if (!size.isValid()) {
        size = QSize(image->pixelWidth(), image->pixelHeight());
    }
    return size;
}

QVariantMap Exiv2MetadataReader::readExifToMap(Exiv2::Image *image) {
    QVariantMap out;
    const Exiv2::ExifData &exifData = image->exifData();
    // for (auto it = exifData.begin(); it != exifData.end(); ++it) {
    //     out[it->key().c_str()] = it->value().toString().c_str();
    // }

    if (!exifData.empty()) {
        auto dateTimeOriginalIt = exifData.findKey(Exiv2::ExifKey("Exif.Photo.DateTimeOriginal"));
        if (dateTimeOriginalIt != exifData.end()) {
            out["DateTime"] = QDateTime::fromString(QString::fromStdString(dateTimeOriginalIt->value().toString()),
                                                    "yyyy:MM:dd hh:mm:ss");
        }

        QStringList shooting;
        auto shutterIt = exifData.findKey(Exiv2::ExifKey("Exif.Photo.ExposureTime"));
        if (shutterIt != exifData.end()) {
            out["ShutterSpeed"] = QString::fromStdString(shutterIt->value().toString());
        }
        auto fIt = exifData.findKey(Exiv2::ExifKey("Exif.Photo.FNumber"));
        if (fIt != exifData.end()) {
            out["FNumber"] = QString::fromStdString(fIt->value().toString());
        }
        auto isoIt = exifData.findKey(Exiv2::ExifKey("Exif.Photo.ISOSpeedRatings"));
        if (isoIt != exifData.end()) {
            out["ISO"] = QString::fromStdString(isoIt->value().toString());
        }

        auto uniqueCameraModelIt = exifData.findKey(Exiv2::ExifKey("Exif.Image.UniqueCameraModel"));
        if (uniqueCameraModelIt != exifData.end()) {
            out["Camera"] = QString::fromStdString(uniqueCameraModelIt->value().toString());
        }
        else {
            QString cameraModel;
            auto imageMakeIt = exifData.findKey(Exiv2::ExifKey("Exif.Image.Make"));
            auto imageModelIt = exifData.findKey(Exiv2::ExifKey("Exif.Image.Model"));

            if (imageMakeIt != exifData.end()) {
                cameraModel.append(QString::fromStdString(imageMakeIt->value().toString()) + " ");
            }
            if (imageModelIt != exifData.end()) {
                cameraModel.append(QString::fromStdString(imageModelIt->value().toString()));
            }
            if (!cameraModel.isEmpty()) {
                out["Camera"] = cameraModel;
            }
        }
        auto focalLengthIt = exifData.findKey(Exiv2::ExifKey("Exif.Photo.FocalLengthIn35mmFilm"));
        if (focalLengthIt != exifData.end()) {
            out["FocalLength"] = QString::fromStdString(focalLengthIt->value().toString());
        }
        auto lensIt = exifData.findKey(Exiv2::ExifKey("Exif.Photo.LensModel"));
        if (lensIt != exifData.end()) {
            out["Lens"] = QString::fromStdString(lensIt->value().toString());
        }

        auto latitudeIt = exifData.findKey(Exiv2::ExifKey("Exif.GPSInfo.GPSLatitude"));
        auto latitudeRefIt = exifData.findKey(Exiv2::ExifKey("Exif.GPSInfo.GPSLatitudeRef"));
        auto longitudeIt = exifData.findKey(Exiv2::ExifKey("Exif.GPSInfo.GPSLongitude"));
        auto longitudeRefIt = exifData.findKey(Exiv2::ExifKey("Exif.GPSInfo.GPSLongitudeRef"));
        if (latitudeIt != exifData.end() && latitudeRefIt != exifData.end() &&
            longitudeIt != exifData.end() && longitudeRefIt != exifData.end()) {
            out["Location"] = QString("%1 %2,%3 %4")
                                  .arg(latitudeIt->value().toString().c_str())
                                  .arg(latitudeRefIt->value().toString().c_str())
                                  .arg(longitudeIt->value().toString().c_str())
                                  .arg(longitudeRefIt->value().toString().c_str());
        }
    }

    const Exiv2::XmpData &xmpData = image->xmpData();
    // for (auto it = xmpData.begin(); it != xmpData.end(); ++it) {
    //     qDebug() << "ZZ" << it->key().c_str() << ":" << it->value().toString().c_str();
    //     out[it->key().c_str()] = it->value().toString().c_str();
    // }

    if (!xmpData.empty()) {
        auto panoramaIt = xmpData.findKey(Exiv2::XmpKey("Xmp.GPano.UsePanoramaViewer"));
        if (panoramaIt != xmpData.end()) {
            out["Panorama"] = QString::fromStdString(panoramaIt->value().toString());
        }
    }

    return out;
}

void Exiv2MetadataReader::fixMimeType(QString &mimeToUpdate, const QString &filePath) {
    if (isExtensionMatch(filePath, {"psd", "psb"})) {
        mimeToUpdate = "psd";
    }
}
