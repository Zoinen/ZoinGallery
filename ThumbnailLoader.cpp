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
#include "Unsharp/Unsharp.h"
#include <turbojpeg.h>

#ifdef _WIN32
#define NULL_DEVICE "NUL:"
#else
#define NULL_DEVICE "/dev/null"
#endif

void ThumbnailLoader::init() {
    Exiv2::XmpParser::initialize();
    ::atexit(Exiv2::XmpParser::terminate);
#ifdef EXV_ENABLE_BMFF
    Exiv2::enableBMFF();
#endif

    freopen(NULL_DEVICE, "w", stderr);
}

void ThumbnailLoader::setPath(const QString &path) {
    _path = path;
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

bool ThumbnailLoader::readExifPreview(const QString &path, QSize preferredSize, ImageReadResult &outResult) {
    _path = path;

    QFile f(path);
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
        outResult.orientation = readOrientationFromExif(image.get());
        if (isJpeg(path)) {
            outResult.fullSize = QSize(image->pixelWidth(), image->pixelHeight());
        }
        else {
            outResult.fullSize = readResolutionFromExif(image.get());
        }
//        qDebug() << "OUT FULL SIZE" << path << outResult.fullSize;

//        qDebug() << outResult.request.sourcePath << outResult.fullSize;

//        qDebug() << "Read exif preview" << path << outResult.request.targetSize << outResult.fullSize;

        QSize preferredSizeRotated = rotateToOrientation(preferredSize, outResult.orientation);

        Exiv2::PreviewProperties previewProp;

        // Blacklisting second thumbnail in cr2 since it's a weird tif with very dark image
        int ignoreThumbnailAt = -1;
        if (path.endsWith(".cr2", Qt::CaseInsensitive)) {
            ignoreThumbnailAt = 2;
        }
//        qDebug() << "ignoreThumbnailAt" << ignoreThumbnailAt;
        Exiv2::DataBuf previewImg = Exiv2Preview::preview(*image.get(), preferredSizeRotated.width(),
                                                          preferredSizeRotated.height(), &previewProp, ignoreThumbnailAt);
        if (previewImg.pData_) {
            outResult.thumbnailData = QByteArray::fromRawData(reinterpret_cast<char *>(previewImg.pData_), previewImg.size_);
            outResult.mimeType = QString::fromStdString(previewProp.mimeType_);
            outResult.thumbnailSize = QSize(previewProp.width_, previewProp.height_);
            previewImg.release();
            f.unmap(mappedFile);
            return true;
        }
        else {
            outResult.mimeType = QString::fromStdString(image->mimeType());
        }
    } catch (Exiv2::AnyError& e) {
        std::cout << "Caught Exiv2 exception '" << e << "'" << std::endl;
        f.unmap(mappedFile);
        return false;
    }
    f.unmap(mappedFile);
    return false;
}

bool ThumbnailLoader::readGenericPreview(const QString &path, QSize preferredSize, ImageReadResult &outResult) {
    QFile f(path);
    if (f.open(QFile::ReadOnly)) {
        outResult.fullImageData = f.readAll();
        f.close();
    }
    else {
        return false;
    }

    QImageReader reader(path);
    outResult.fullSize = reader.size();
    QMimeDatabase mimeDatabase;
    outResult.mimeType = mimeDatabase.mimeTypeForData(outResult.fullImageData).name();
    return reader.canRead();
}

QImage ThumbnailLoader::decodeImage(const QByteArray &data, const QString &mimeType, QSize targetSize, const ImageReadResult &readResult) {
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
        QSize targetSizeWithAspect = targetSize;
        if (readResult.request.viewerRequest) {
            targetSizeWithAspect = readResult.fullSize.scaled(targetSize, Qt::KeepAspectRatio);
        }
        reader.setScaledSize(targetSizeWithAspect);

        QImage img = reader.read();;
        if (img.isNull()) {
            qDebug() << "Could not decode image" << mimeType;
        }
        else {
            qDebug() << "Using Qt decoder for" << mimeType;
        }
        return img;
    }
    return QImage();
}

QImage ThumbnailLoader::createThumbnail(const QImage &image, QSize dimensions, bool keepAspect) {
    //dimensions = image.size().scaled(dimensions, Qt::KeepAspectRatio);

    return image.scaled(dimensions, keepAspect ? Qt::KeepAspectRatio : Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

QImage ThumbnailLoader::unsharpMask(QImage &image) {
    image.convertTo(QImage::Format_RGB888);
//    image.save(QString("c:\\temp\\%1.png").arg(QFileInfo(_path).baseName()));
    uint8_t *unsharped = unsharp(image.bits(), image.width(), image.height(), 24);
    QImage imageSharp((uchar *)unsharped, image.width(), image.height(), QImage::Format_RGB888,
                          [] (void *ptr) {delete[] (uint8_t *)ptr;}, unsharped);
//    imageSharp.save(QString("c:\\temp\\%1+.png").arg(QFileInfo(_path).baseName()));
    return imageSharp;
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
    return {"*.jpg", "*.jpeg", "*.jpe", "*.jfif",
            "*.tiff", "*.tif", "*.cr2", "*.dng", "*.crw", "*.nef", "*.arw", "*.arq",
            "*.bmp", "*.png", "*.gif", "*.jp2", "*.jpc", "*.tga", "*.ico", "*.cur", "*.ppm", "*.pgm", "*.pbm", "*.svg", "*.wmf", "*.emf", "*.webp",
            "*.svg", "*.wmf", "*.emf"};
}

bool ThumbnailLoader::isJpeg(const QString &path) {
    QStringList extensions = {"jpg", "jpeg", "jpe", "jfif"};
    for (const QString &ext : extensions) {
        if (path.endsWith(QString(".") + ext, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

bool ThumbnailLoader::isRawOrTiff(const QString &path) {
    QStringList extensions = {"tiff", "tif", "cr2", "dng", "crw", "nef", "arw", "arq"};
    for (const QString &ext : extensions) {
        if (path.endsWith(QString(".") + ext, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

bool ThumbnailLoader::isImageOther(const QString &path) {
    QStringList extensions = {"bmp", "png", "gif", "jp2", "jpc", "tga", "ico", "cur", "ppm", "pgm", "pbm", "svg", "wmf", "emf", "webp"};
    for (const QString &ext : extensions) {
        if (path.endsWith(QString(".") + ext, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

bool ThumbnailLoader::isVectorImage(const QString &path) {
    QStringList extensions = {"svg", "wmf", "emf"};
    for (const QString &ext : extensions) {
        if (path.endsWith(QString(".") + ext, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

QImage ThumbnailLoader::loadJpegFromData(const uint8_t *data, uint32_t size, QSize targetSize) {
    const int COLOR_COMPONENTS = 3;

    long unsigned int _jpegSize = size;
    const unsigned char* _compressedImage = data;

    int jpegSubsamp, jpegColorspace, width, height;

    tjhandle _jpegDecompressor = tjInitDecompress();

    int res = tjDecompressHeader3(_jpegDecompressor, _compressedImage, _jpegSize, &width, &height, &jpegSubsamp, &jpegColorspace);
    if (res == -1) {
        if (tjGetErrorCode(_jpegDecompressor) == TJERR_FATAL) {
            qDebug() << "JPEG header decode error:" << tjGetErrorStr2(_jpegDecompressor) << "," << _path << _compressedImage << _jpegSize << width << height;
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
    unsigned char *buffer = new unsigned char[pitch * height * COLOR_COMPONENTS];

    res = tjDecompress2(_jpegDecompressor, _compressedImage, _jpegSize, buffer, width, pitch, height, TJPF_RGB, TJFLAG_FASTDCT);
    if (res == -1) {
        if (tjGetErrorCode(_jpegDecompressor) == TJERR_FATAL) {
            qDebug() << "JPEG decode error:" << tjGetErrorStr2(_jpegDecompressor) << "," << _path;
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

QSize ThumbnailLoader::readResolutionFromExif(Exiv2::Image *image) {
    QSize size;
    const Exiv2::ExifData &exifData = image->exifData();
    if (!exifData.empty()) {
        auto widthIt = exifData.findKey(Exiv2::ExifKey("Exif.Photo.PixelXDimension"));
        auto heightIt = exifData.findKey(Exiv2::ExifKey("Exif.Photo.PixelYDimension"));
        if (widthIt != exifData.end() && heightIt != exifData.end()) {
            size = QSize(widthIt->toLong(), heightIt->toLong());
        }
        else {
            auto sizeIt = exifData.findKey(Exiv2::ExifKey("Exif.SubImage1.DefaultCropSize"));
            if (sizeIt != exifData.end() && sizeIt->count() == 2) {
                size = QSize(sizeIt->toLong(0), sizeIt->toLong(1));
            }
            else {
                auto widthIt = exifData.findKey(Exiv2::ExifKey("Exif.Image.ImageWidth"));
                auto heightIt = exifData.findKey(Exiv2::ExifKey("Exif.Image.ImageLength"));
                if (widthIt != exifData.end() && heightIt != exifData.end()) {
                    size = QSize(widthIt->toLong(), heightIt->toLong());
                }
            }
        }
    }
    if (size.isEmpty()) {
        size = QSize(image->pixelWidth(), image->pixelHeight());
    }
    return size;
}
