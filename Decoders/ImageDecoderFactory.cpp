#include "ImageDecoderFactory.h"
#include "ImageDecoderInterface.h"

#include "DdsDecoder.h"
#include "HeicDecoder.h"
#include "JpegDecoder.h"
#include "PngDecoder.h"
#include "QtDecoder.h"
#include "RawDecoder.h"
#include "TiffDecoder.h"
#include "WebpDecoder.h"

#ifdef __USE_EXIV2
#include "Exiv2Decoder.h"
#endif

#include <mutex>

QList<ImageDecoderFactory::Decoder> ImageDecoderFactory::_decoders;

void ImageDecoderFactory::registerBuiltInDecoders() {
    static std::once_flag once;
    std::call_once(once, [] {
        registerClass(&RawDecoder::create, RawDecoder::_decoderPriority, "LibRaw");
        registerClass(&JpegDecoder::create, JpegDecoder::_decoderPriority, "libjpeg-turbo / TinyEXIF");
        registerClass(&HeicDecoder::create, HeicDecoder::_decoderPriority, "libheif");
        registerClass(&WebpDecoder::create, WebpDecoder::_decoderPriority, "libwebp");
        registerClass(&DdsDecoder::create, DdsDecoder::_decoderPriority, "dds.hpp / ZoinGallery BC decoder");
        registerClass(&TiffDecoder::create, TiffDecoder::_decoderPriority, "libtiff");
        registerClass(&PngDecoder::create, PngDecoder::_decoderPriority, "libpng");
#ifdef __USE_EXIV2
        registerClass([] { return new Exiv2Decoder(); }, -1, "Exiv2");
#endif
        registerClass(&QtDecoder::create, QtDecoder::_decoderPriority, "Qt image plugins");
    });
}

QVariantList ImageDecoderFactory::decoderInventory() {
    registerBuiltInDecoders();
    QVariantList result;
    for (int i = 0; i < _decoders.size(); ++i) {
        auto decoder = createDecoder(i);
        auto formats = decoder->supportedFormats();
        formats.removeDuplicates();
        formats.sort(Qt::CaseInsensitive);
        result.append(QVariantMap{{"name", decoder->decoderName()},
            {"priority", _decoders[i].priority}, {"library", _decoders[i].library},
            {"formats", formats}, {"order", i + 1}});
    }
    return result;
}
