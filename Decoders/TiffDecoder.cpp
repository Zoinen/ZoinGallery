#include "TiffDecoder.h"

#include <streambuf>
#include <istream>
#include <sstream>

#include "tiffio.hxx"

static const QStringList TiffRawExtensions = {"tiff", "tif", "cr2", "dng", "crw", "nef", "arw", "arq", "cr3"};

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

QStringList TiffDecoder::supportedFormats() {
    return TiffRawExtensions;
}

bool TiffDecoder::canDecode(const QString &mimeType) {
    return mimeType == "image/tiff";
}

QImage TiffDecoder::decode(const QByteArray &data, QSize targetSize) {
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
    return QImage();
}
