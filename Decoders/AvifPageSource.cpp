#include "PagedImageDocument.h"

#include "ThumbnailLoader.h"

#include <QColorSpace>
#include <QFile>
#include <QTransform>
#include <avif/avif.h>

namespace ZoinGallery {
namespace {

QColorSpace avifColorSpace(const avifImage &image)
{
    if (image.icc.size != 0) {
        const auto profile = QByteArray::fromRawData(
            reinterpret_cast<const char *>(image.icc.data), qsizetype(image.icc.size));
        const QColorSpace color = QColorSpace::fromIccProfile(profile);
        if (color.isValid())
            return color;
    }
    QColorSpace::TransferFunction transfer = QColorSpace::TransferFunction::SRgb;
    switch (image.transferCharacteristics) {
    case AVIF_TRANSFER_CHARACTERISTICS_LINEAR:
        transfer = QColorSpace::TransferFunction::Linear;
        break;
    case AVIF_TRANSFER_CHARACTERISTICS_BT2020_10BIT:
    case AVIF_TRANSFER_CHARACTERISTICS_BT2020_12BIT:
        transfer = QColorSpace::TransferFunction::Bt2020;
        break;
    case AVIF_TRANSFER_CHARACTERISTICS_PQ:
        transfer = QColorSpace::TransferFunction::St2084;
        break;
    case AVIF_TRANSFER_CHARACTERISTICS_HLG:
        transfer = QColorSpace::TransferFunction::Hlg;
        break;
    default:
        break;
    }
    float primaries[8];
    avifColorPrimariesGetValues(image.colorPrimaries, primaries);
    const QColorSpace color(QPointF(primaries[6], primaries[7]), QPointF(primaries[0], primaries[1]),
                            QPointF(primaries[2], primaries[3]), QPointF(primaries[4], primaries[5]), transfer);
    return color.isValid() ? color : QColorSpace(QColorSpace::SRgb);
}

QRect displayRect(const avifImage &image)
{
    avifCropRect crop;
    if ((image.transformFlags & AVIF_TRANSFORM_CLAP)
        && avifCropRectFromCleanApertureBox(&crop, &image.clap, image.width, image.height, nullptr)) {
        return QRect(int(crop.x), int(crop.y), int(crop.width), int(crop.height));
    }
    return QRect(0, 0, int(image.width), int(image.height));
}

class AvifPages final : public PagedImageDocument {
public:
    explicit AvifPages(const QString &path) : _file(path)
    {
        if (!_decoder || !_file.open(QIODevice::ReadOnly))
            return;
        _io.sizeHint = uint64_t(_file.size());
        _io.read = read;
        _io.data = this;
        _decoder->maxThreads = 2;
        _decoder->ignoreExif = AVIF_TRUE;
        _decoder->ignoreXMP = AVIF_TRUE;
        avifDecoderSetIO(_decoder.get(), &_io);
        if (avifDecoderParse(_decoder.get()) == AVIF_RESULT_OK)
            _count = _decoder->imageCount;
    }

    int pageCount() const override { return _count; }
    bool animated() const override { return _count > 1; }
    int repeats() const override
    {
        const int count = _decoder->repetitionCount;
        return count == AVIF_REPETITION_COUNT_UNKNOWN ? 0 : count;
    }

    QSizeF pageSize(int) override
    {
        if (_count == 0)
            return {};
        const avifImage &image = *_decoder->image;
        QSize size = displayRect(image).size();
        if ((image.transformFlags & AVIF_TRANSFORM_IROT) && (image.irot.angle & 1))
            size.transpose();
        return size;
    }

    ImagePage render(int index, QSize target, QRect, const Canceled &canceled) override
    {
        if (index < 0 || index >= _count)
            return {{}, {}, 0, PageError::EndOfSequence};
        const QSize declaredSize(int(_decoder->image->width), int(_decoder->image->height));
        if (!validPageRaster(declaredSize) || canceled()
            || avifDecoderNthImage(_decoder.get(), uint32_t(index)) != AVIF_RESULT_OK || canceled())
            return {{}, {}, 0, PageError::CannotRead};
        const avifImage &image = *_decoder->image;
        const QSize native(int(image.width), int(image.height));
        if (!validPageRaster(native))
            return {{}, {}, 0, PageError::CannotRead};
        QImage raster(native, QImage::Format_RGBA8888);
        if (raster.isNull())
            return {{}, {}, 0, PageError::CannotRead};
        avifRGBImage rgb;
        avifRGBImageSetDefaults(&rgb, &image);
        rgb.depth = 8;
        rgb.format = AVIF_RGB_FORMAT_RGBA;
        rgb.pixels = raster.bits();
        rgb.rowBytes = uint32_t(raster.bytesPerLine());
        if (avifImageYUVToRGB(&image, &rgb) != AVIF_RESULT_OK || canceled())
            return {{}, {}, 0, PageError::CannotRead};
        raster.setColorSpace(avifColorSpace(image));
        const QRect crop = displayRect(image);
        if (crop != raster.rect())
            raster = raster.copy(crop);
        if ((image.transformFlags & AVIF_TRANSFORM_IROT) && image.irot.angle != 0)
            raster = raster.transformed(QTransform().rotate(-90 * int(image.irot.angle)));
        if (image.transformFlags & AVIF_TRANSFORM_IMIR)
            raster = raster.flipped(image.imir.axis == 0 ? Qt::Vertical : Qt::Horizontal);
        const QSize size = raster.size();
        const double milliseconds = _decoder->imageTiming.duration * 1000.0;
        return {ThumbnailLoader::createViewerImage(raster, boundedPageRaster(size, target, false)),
                size, qRound(qBound(0.0, milliseconds, double(INT_MAX)))};
    }

private:
    static avifResult read(avifIO *io, uint32_t, uint64_t offset, size_t length, avifROData *out)
    {
        auto *source = static_cast<AvifPages *>(io->data);
        if (offset > io->sizeHint || !source->_file.seek(qint64(offset)))
            return AVIF_RESULT_IO_ERROR;
        const qint64 available = qint64(qMin<uint64_t>(length, io->sizeHint - offset));
        source->_buffer = source->_file.read(available);
        if (source->_buffer.size() != available)
            return AVIF_RESULT_IO_ERROR;
        out->data = reinterpret_cast<const uint8_t *>(source->_buffer.constData());
        out->size = size_t(available);
        return AVIF_RESULT_OK;
    }

    QFile _file;
    QByteArray _buffer;
    avifIO _io{};
    std::unique_ptr<avifDecoder, decltype(&avifDecoderDestroy)> _decoder{
        avifDecoderCreate(), &avifDecoderDestroy};
    int _count = 0;
};

} // namespace

std::unique_ptr<PagedImageDocument> openAvifPages(const QString &path)
{
    return std::make_unique<AvifPages>(path);
}

} // namespace ZoinGallery
