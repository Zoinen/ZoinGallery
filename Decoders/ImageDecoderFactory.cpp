#include "ImageDecoderFactory.h"

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
        registerClass(&RawDecoder::create, RawDecoder::_decoderPriority);
        registerClass(&JpegDecoder::create, JpegDecoder::_decoderPriority);
        registerClass(&HeicDecoder::create, HeicDecoder::_decoderPriority);
        registerClass(&WebpDecoder::create, WebpDecoder::_decoderPriority);
        registerClass(&DdsDecoder::create, DdsDecoder::_decoderPriority);
        registerClass(&TiffDecoder::create, TiffDecoder::_decoderPriority);
        registerClass(&PngDecoder::create, PngDecoder::_decoderPriority);
#ifdef __USE_EXIV2
        registerClass([] { return new Exiv2Decoder(); }, -1);
#endif
        registerClass(&QtDecoder::create, QtDecoder::_decoderPriority);
    });
}
