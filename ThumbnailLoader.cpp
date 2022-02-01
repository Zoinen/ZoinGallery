#include "ThumbnailLoader.h"

#include <QDebug>
#include <QImage>
#include <QFile>

#include <exiv2/exiv2.hpp>
#include <exiv2/preview.hpp>
#include <iostream>
#include <iomanip>
#include <cassert>
#include <string>
#include <streambuf>
#include <istream>
#include <sstream>

#include "tiffio.hxx"

#ifdef _WIN32
#define NULL_DEVICE "NUL:"
#else
#define NULL_DEVICE "/dev/null"
#endif


ThumbnailLoader::ThumbnailLoader() {

}

void ThumbnailLoader::init() {
    Exiv2::XmpParser::initialize();
    ::atexit(Exiv2::XmpParser::terminate);
#ifdef EXV_ENABLE_BMFF
    Exiv2::enableBMFF();
#endif

    freopen(NULL_DEVICE, "w", stderr);
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

QImage ThumbnailLoader::load(const QString &path) {
    const char* key = "CameraModel";
    const char *tag = "Exif.SubImage2";

    try {
        std::string strPath(path.toUtf8());
        Exiv2::Image::AutoPtr image = Exiv2::ImageFactory::open(strPath);
        assert(image.get() != 0);
        image->readMetadata();

        Exiv2::PreviewManager manager(*image);
        Exiv2::PreviewPropertiesList properties = manager.getPreviewProperties();
//        for (const auto &preview : properties) {
//            qDebug() << preview.width_ << "x" << preview.height_ << ":" << preview.extension_.c_str() << preview.mimeType_.c_str();
//        }
        if (!properties.size()) {
            return QImage();
        }

        Exiv2::PreviewImage previewImg = manager.getPreviewImage(properties.size() > 1 ? properties[0] : properties[0]);
        imemstream memStream(previewImg.pData(), previewImg.size());

        std::istringstream input_TIFF_stream;

        TIFF* tif = TIFFStreamOpen("MemTIFF", &memStream);

        if (tif) {
            uint32_t w, h;
            size_t npixels;
            uint32_t *raster;

            TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &w);
            TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &h);
            npixels = w * h;
            raster = (uint32_t*) _TIFFmalloc(npixels * sizeof(uint32_t));
            if (raster != NULL) {
                if (TIFFReadRGBAImageOriented(tif, w, h, raster, ORIENTATION_TOPLEFT, 0)) {
                    QImage img((uchar *)raster, w, h, QImage::Format_RGBA8888);
                    //                _TIFFfree(raster);
                    TIFFClose(tif);
                    return img;
                }
                else {
                    _TIFFfree(raster);
                }
            }
            TIFFClose(tif);
        }
    } catch (Exiv2::AnyError& e) {
//        std::cerr << "Caught Exiv2 exception '" << e << "'" << std::endl;
        return QImage();
    }
    return QImage();

//    QFile f("c:\\Temp\\__2");
//    f.open(QFile::WriteOnly);
//    f.write((char *)previewImg.pData(), previewImg.size());
//    QImage img = QImage::fromData(previewImg.pData(), previewImg.size());
//    qDebug() << img.width() << "x" << img.height();
//    img.save("C:\\temp\\_1.png");



    /*Exiv2::ExifData &exifData = image->exifData();

    if (exifData.empty()) {
        std::cerr << "no metadata found in file " << path.toStdString() << std::endl;
        exit(2);
    }

    try {
//        for (auto it = exifData.begin(); it != exifData.end(); ++it) {
//            qDebug() << it->key().c_str() << it->value().toString().substr(0, 50).c_str();
//        }
//        std::cout << exifData[key] << std::endl;

        auto it = exifData.findKey(Exiv2::ExifKey(tag));
        if (it != exifData.end()) {
            Exiv2::DataBuf buf = it->dataArea();
            QImage img = QImage::fromData(buf.pData_, buf.size_);
            qDebug() << img.width() << "x" << img.height();
            img.save("C:\\temp\\_1.png");
        }
        else {
            qDebug() << tag << "not found";
        }
    } catch (Exiv2::AnyError& e) {
        std::cerr << "Caught Exiv2 exception '" << e << "'" << std::endl;
        exit(3);
    } catch ( ... ) {
        std::cerr << "Caught a cold!" << std::endl;
        exit(4);
    }**/
}
