#include "ThumbnailLoader.h"

#include <QDebug>
#include <QImage>
#include <QFile>
#include <QString>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QImageReader>
#include <QBuffer>
#include <QMimeDatabase>

#include <exiv2/exiv2.hpp>
#include <exiv2/preview.hpp>
#include "exiv2/Preview.h"
#include <iostream>
#include <iomanip>
#include <cassert>
#include <string>
#include <streambuf>
#include <istream>
#include <sstream>

#include "tiffio.hxx"
#include <turbojpeg.h>
#include <memory>

#ifdef _WIN32
#define NULL_DEVICE "NUL:"
#else
#define NULL_DEVICE "/dev/null"
#endif


static const QStringList VectorImageExtensions = {"svg", "wmf", "emf"};
//static const QStringList ImageQtExtensions = {"bmp", "png", "gif", "jp2", "jpc", "tga", "ico", "cur", "ppm", "pgm", "pbm", "svg", "wmf", "emf", "webp", "heic"};
static const QStringList TiffRawExtensions = {"tiff", "tif", "cr2", "dng", "crw", "nef", "arw", "arq"};
static const QStringList JpegExtensions = {"jpg", "jpeg", "jpe", "jfif"};
QStringList Exiv2Extensions = {"exv", "mrw", "webp", "pef", "rw2", "sr2", "srw", "orf", "png", "pgf", "raf", "eps", "xmp", "gif", "psd", "tga", "bmp", "jp2"};

QStringList ThumbnailLoader::ImageQtExtensions;

ExifOrientation mapQtTransformationToExifOrientation(QImageIOHandler::Transformations transformation) {
    switch(transformation) {
    case QImageIOHandler::TransformationNone:
        return ExifOrientation::Horizontal;
    case QImageIOHandler::TransformationMirror:
        return ExifOrientation::MirrorHorizontal;
    case QImageIOHandler::TransformationFlip:
        return ExifOrientation::MirrorVertical;
    case QImageIOHandler::TransformationRotate180:
        return ExifOrientation::Rotate180;
    case QImageIOHandler::TransformationRotate90:
        return ExifOrientation::Rotate90CW;
    case QImageIOHandler::TransformationMirrorAndRotate90:
        return ExifOrientation::MirrorHorizontalAndRotate90CW;
    case QImageIOHandler::TransformationFlipAndRotate90:
        return ExifOrientation::MirrorHorizontalAndRotate270CW;
    case QImageIOHandler::TransformationRotate270:
        return ExifOrientation::Rotate270CW;
    default:
        return ExifOrientation::Horizontal;
    }
}

void ThumbnailLoader::init() {
    Exiv2::XmpParser::initialize();
    ::atexit(Exiv2::XmpParser::terminate);
#ifdef EXV_ENABLE_BMFF
    Exiv2::enableBMFF();
#endif

#if defined(Q_OS_WIN)
    freopen(NULL_DEVICE, "w", stderr);
#endif

    if (ImageQtExtensions.isEmpty()) {
        for (QByteArray arr : QImageReader::supportedImageFormats()) {
            QString ext = QString::fromLatin1(arr);
            if (!TiffRawExtensions.contains(ext) && !JpegExtensions.contains(ext)) {
                ImageQtExtensions.append(ext);
            }
        }
        qDebug() << "Qt plugin formats:" << ImageQtExtensions;

        Exiv2Extensions.append(JpegExtensions);
        Exiv2Extensions.append(TiffRawExtensions);

    }

    // Just to fully instantiate it here
    supportedFormats();
}

struct membuf: std::streambuf {
    membuf(uint8_t const* base, size_t size) {
        char* p((char *)base);
        this->setg(p, p, p + size);
    }

    pos_type seekoff(off_type off, std::ios_base::seekdir dir, std::ios_base::openmode which = std::ios_base::in) override {
        if (dir == std::ios_base::cur)
            gbump(off);
        else if (dir == std::ios_base::end)
            setg(eback(), egptr() + off, egptr());
        else if (dir == std::ios_base::beg)
            setg(eback(), eback() + off, egptr());
        return gptr() - eback();
    }

    pos_type seekpos(pos_type sp, std::ios_base::openmode which) override {
        return seekoff(sp - pos_type(off_type(0)), std::ios_base::beg, which);
    }
};
struct imemstream: virtual membuf, std::istream {
    imemstream(uint8_t const* base, size_t size)
        : membuf(base, size)
        , std::istream(static_cast<std::streambuf*>(this)) {
    }
};

bool ThumbnailLoader::readExif(ImageInfo &result) {
    if (!isExiv2Compatible(result.path)) {
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
        if (isJpeg(result.path)) {
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

bool ThumbnailLoader::readGenericInfo(ImageInfo &result) {
    QImageReader reader(result.path);
    ExifOrientation orientation = mapQtTransformationToExifOrientation(reader.transformation());
    result.imageSize = rotateToOrientation(reader.size(), orientation);
    return reader.canRead();
}

bool ThumbnailLoader::readImage(ImageData &result) {
    bool previewLoaded = false;
    if (!result.request.info.path.endsWith(".jpg", Qt::CaseInsensitive)) {
        previewLoaded = readPreviewAndMime(result);
    }
    QSize targetSize = result.request.targetSize;
    if (!result.request.checkCache) {
        targetSize = expandToCacheImageResolution(targetSize);
    }
    QSize sizeRotated = rotateToOrientation(result.request.info.imageSize, result.request.info.orientation);
    if (!previewLoaded || sizeRotated.width() < targetSize.width() ||
                          sizeRotated.height() < targetSize.height()) {
        QFile f(result.request.info.path);
        if (!f.open(QFile::ReadOnly)) {
            return false;
        }
        result.data = f.readAll();
        f.close();
    }
    return true;
}

bool ThumbnailLoader::readPreviewAndMime(ImageData &result) {
    if (!isExiv2Compatible(result.request.info.path)) {
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

QImage ThumbnailLoader::decodeImage(const QByteArray &data, const QString &mimeType, QSize targetSize) {
    if (mimeType == "image/tiff") {
        imemstream memStream(reinterpret_cast<const uint8_t *>(data.constData()), data.size());

        std::istringstream input_TIFF_stream;

        TIFF* tif = TIFFStreamOpen("MemTIFF", &memStream);

        if (tif) {
            uint32_t w, h;
            size_t npixels;

            TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &w);
            TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &h);
            npixels = w * h;
            uint32_t *tiffRaster = (uint32_t*) _TIFFmalloc(npixels * sizeof(uint32_t));
            if (tiffRaster != NULL) {
                if (TIFFReadRGBAImageOriented(tif, w, h, tiffRaster, ORIENTATION_TOPLEFT, 0)) {
                    TIFFClose(tif);

                    QImage img((uchar *)tiffRaster, w, h, QImage::Format_RGBA8888,
                               [] (void *ptr) { if (ptr) _TIFFfree(ptr); }, tiffRaster);
                    return img;
                }
                else {
                    _TIFFfree(tiffRaster);
                    tiffRaster = nullptr;
                }
            }
            TIFFClose(tif);
        }
    }
    else if (mimeType == "image/jpeg") {
        QImage img = loadJpegFromData(reinterpret_cast<const uint8_t *>(data.constData()), data.size(), targetSize);
        return img;
    }
    else {
        QBuffer buf(const_cast<QByteArray *>(&data));
        buf.open(QIODevice::ReadOnly);

        QImageReader reader(&buf);
        reader.setScaledSize(targetSize);

        QImage img = reader.read();;
        if (img.isNull()) {
            qDebug() << "Could not decode image" << mimeType;
        }
        else {
//            qDebug() << "Using Qt decoder for" << mimeType;
        }
        return img;
    }
    return QImage();
}

QImage ThumbnailLoader::createThumbnail(const QImage &image, QSize dimensions) {
    if (dimensions.width() < image.width() ||
        dimensions.height() < image.height()) {
        return image.scaled(dimensions, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    return image;
}

QImage ThumbnailLoader::rotateAndFlip(const QImage &image, ExifOrientation orientation) {
    if (orientation && orientation != ExifOrientation::Horizontal) {
        QTransform transform;
        if (orientation == ExifOrientation::Rotate270CW) {
            transform.rotate(270);
        }
        else if (orientation == ExifOrientation::Rotate90CW) {
            transform.rotate(90);
        }
        else if (orientation == ExifOrientation::Rotate180) {
            transform.rotate(180);
        }
        else if (orientation == ExifOrientation::MirrorHorizontal) {
            transform.scale(-1, 1);
        }
        else if (orientation == ExifOrientation::MirrorHorizontalAndRotate270CW) {
            transform.scale(-1, 1);
            transform.rotate(270);
        }
        else if (orientation == ExifOrientation::MirrorHorizontalAndRotate90CW) {
            transform.scale(-1, 1);
            transform.rotate(90);
        }
        else if (orientation == ExifOrientation::MirrorVertical) {
            transform.scale(1, -1);
        }
        return image.transformed(transform);
    }
    return image;
}

QStringList ThumbnailLoader::supportedFormats() {
    static QStringList formats;
    if (formats.isEmpty()) {
        for (const auto &extensions : {JpegExtensions, TiffRawExtensions, ImageQtExtensions, VectorImageExtensions}) {
            for (const QString &extension : extensions) {
                formats.append(QString("*.%1").arg(extension));
            }
        }
    }
    return formats;
}

bool ThumbnailLoader::isJpeg(const QString &path) {
    return isExtensionMatch(path, JpegExtensions);
}

bool ThumbnailLoader::isRawOrTiff(const QString &path) {
    return isExtensionMatch(path, TiffRawExtensions);
}

bool ThumbnailLoader::isImageOther(const QString &path) {
    return isExtensionMatch(path, ImageQtExtensions);
}

bool ThumbnailLoader::isVectorImage(const QString &path) {
    return isExtensionMatch(path, VectorImageExtensions);
}

bool ThumbnailLoader::isExiv2Compatible(const QString &path) {
    return isExtensionMatch(path, Exiv2Extensions);
}

bool ThumbnailLoader::isExtensionMatch(const QString &path, const QStringList &pattern) {
    for (const QString &ext : pattern) {
        if (path.endsWith(QString(".") + ext, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}


QImage ThumbnailLoader::loadJpegFromData(const uint8_t *data, uint32_t size, QSize targetSize) {
    long unsigned int _jpegSize = size;
    const unsigned char* _compressedImage = data;

    int jpegSubsamp, jpegColorspace, width, height;

    tjhandle _jpegDecompressor = tjInitDecompress();

    int res = tjDecompressHeader3(_jpegDecompressor, _compressedImage, _jpegSize, &width, &height, &jpegSubsamp, &jpegColorspace);
    if (res == -1) {
        if (tjGetErrorCode(_jpegDecompressor) == TJERR_FATAL) {
            qDebug() << "JPEG header decode error:" << tjGetErrorStr2(_jpegDecompressor) << "," << _compressedImage << _jpegSize << width << height;
            tjDestroy(_jpegDecompressor);
            return QImage();
        }
    }

    int num = 0;
    tjscalingfactor *factors = tjGetScalingFactors(&num);
    int denomIndex = 0;
    for (; denomIndex < num; denomIndex++) {
        int scaledWidth = TJSCALED(width, factors[denomIndex]);
        int scaledHeight = TJSCALED(height, factors[denomIndex]);
        //qDebug() << denomIndex << scaledWidth << "x" << scaledHeight;
        if (scaledWidth < targetSize.width() || scaledHeight < targetSize.height()) {
            denomIndex--;
            break;
        }
    }
    denomIndex = qBound(0, denomIndex, num - 1);

    // Higher quality due to increased base resolution
    denomIndex--;
    denomIndex = qBound(0, denomIndex, num - 1);

    width = TJSCALED(width, factors[denomIndex]);
    height = TJSCALED(height, factors[denomIndex]);
//    qDebug() << "Chosen" << denomIndex << "of" << num << width << "x" << height << ", req" << targetSize;

    int pitch = TJPAD(width * tjPixelSize[TJPF_RGB]);
    unsigned char *buffer = new unsigned char[pitch * height];

    res = tjDecompress2(_jpegDecompressor, _compressedImage, _jpegSize, buffer, width, pitch, height, TJPF_RGB, TJFLAG_FASTDCT);
    if (res == -1) {
        if (tjGetErrorCode(_jpegDecompressor) == TJERR_FATAL) {
            qDebug() << "JPEG decode error:" << tjGetErrorStr2(_jpegDecompressor) << ",";
            delete[] buffer;
            tjDestroy(_jpegDecompressor);
            return QImage();
        }
    }
    tjDestroy(_jpegDecompressor);

    QImage img(buffer, width, height, QImage::Format_RGB888, [] (void *ptr) {delete[] (uint8_t *)ptr;}, buffer);
    /*int taken3 = t.restart();
    qDebug() << "time:" << path << taken << taken2 << taken3;*/
//    img.save(QString("c:\\temp\\%1").arg(QFileInfo(path).fileName()));
    return img;

}

ExifOrientation ThumbnailLoader::readOrientationFromExif(Exiv2::Image *image) {
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

QVariantMap ThumbnailLoader::readExifToMap(Exiv2::Image *image) {
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

QSize ThumbnailLoader::readResolutionFromExif(Exiv2::Image *image) {
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

void ThumbnailLoader::fixMimeType(QString &mimeToUpdate, const QString &filePath) {
    if (isExtensionMatch(filePath, {"psd", "psb"})) {
        mimeToUpdate = "psd";
    }
}
