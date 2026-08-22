#include "WebpDecoder.h"

#include "ImageFile.h"
#include "TinyEXIF.h"
#include "WebpCodec.h"

#include <QFile>

#include <webp/demux.h>

#include <algorithm>
#include <memory>

REGISTER_DECODER_DEFINITION(WebpDecoder)

namespace {

QByteArray readFile(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QByteArray readExifChunk(const QByteArray &data) {
    const WebPData webpData {
        reinterpret_cast<const uint8_t *>(data.constData()),
        static_cast<size_t>(data.size())
    };
    WebPDemuxer *demuxer = WebPDemux(&webpData);
    if (!demuxer) {
        return {};
    }

    WebPChunkIterator chunk;
    QByteArray exif;
    if (WebPDemuxGetChunk(demuxer, "EXIF", 1, &chunk)) {
        exif = QByteArray(reinterpret_cast<const char *>(chunk.chunk.bytes),
                          static_cast<qsizetype>(chunk.chunk.size));
        WebPDemuxReleaseChunkIterator(&chunk);
    }
    WebPDemuxDelete(demuxer);

    const QByteArray exifHeader("Exif\0\0", 6);
    if (!exif.isEmpty() && !exif.startsWith(exifHeader)) {
        exif.prepend(exifHeader);
    }
    return exif;
}

}

QStringList WebpDecoder::supportedFormats() {
    return {"webp"};
}

bool WebpDecoder::readMetadata(ImageInfo &result) {
    if (!isFormatSupported(result.formatHint())) {
        return false;
    }

    const QByteArray data = readFile(result.path);
    const WebpCodec::Features features = WebpCodec::readFeatures(data);
    if (!features.isValid()) {
        return false;
    }

    result.imageSize = features.size;
    result.exif["Format"] = "WebP";
    result.exif["HasAlpha"] = features.hasAlpha;
    result.exif["Animated"] = features.hasAnimation;

    const QByteArray exifData = readExifChunk(data);
    TinyEXIF::EXIFInfo exifInfo;
    if (!exifData.isEmpty()
        && exifInfo.parseFromEXIFSegment(
            reinterpret_cast<const uint8_t *>(exifData.constData()),
            static_cast<unsigned>(exifData.size())) == TinyEXIF::PARSE_SUCCESS) {
        if (exifInfo.Orientation >= ExifOrientation::Horizontal
            && exifInfo.Orientation <= ExifOrientation::Rotate270CW) {
            result.orientation = static_cast<ExifOrientation>(exifInfo.Orientation);
        }
        const QVariantMap parsedExif = readExifToMap(exifInfo);
        for (auto it = parsedExif.cbegin(); it != parsedExif.cend(); ++it) {
            result.exif.insert(it.key(), it.value());
        }
    }
    return true;
}

bool WebpDecoder::readPreviewAndMime(ImageData &result) {
    if (!isFormatSupported(result.request.info.formatHint())) {
        return false;
    }

    const QByteArray data = readFile(result.request.info.path);
    if (data.isEmpty()) {
        return false;
    }
    result.previewData = std::shared_ptr<char>(new char[data.size()], std::default_delete<char[]>());
    std::copy(data.cbegin(), data.cend(), result.previewData.get());
    result.previewDataSize = data.size();
    result.previewMimeType = "image/webp";
    result.previewUsed = "Original WebP file";
    return true;
}

QImage WebpDecoder::decode(const QString &mimeType, const QByteArray &data, QSize targetSize) {
    if (mimeType != "image/webp" && mimeType != "image/x-webp") {
        return {};
    }
    return WebpCodec::decode(data, targetSize);
}
