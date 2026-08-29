#include "HeicDecoder.h"
#include "TinyEXIF.h"

#include <libheif/heif.h>

#include <QBuffer>
#include <QColorSpace>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QPointF>

#include <cstring>

REGISTER_DECODER_DEFINITION(HeicDecoder)

static const QStringList HeicExtensions = {"heic", "heif", "avif"};

// --- RAII helpers -----------------------------------------------------------

struct HeicContext {
    heif_context* ctx = heif_context_alloc();
    ~HeicContext() { if (ctx) heif_context_free(ctx); }
    operator heif_context*() const { return ctx; }
};

struct HeicHandle {
    heif_image_handle* h = nullptr;
    ~HeicHandle() { if (h) heif_image_handle_release(h); }
    operator heif_image_handle*() const { return h; }
    heif_image_handle** operator&() { return &h; }
};

struct HeicImage {
    heif_image* img = nullptr;
    ~HeicImage() { if (img) heif_image_release(img); }
    operator heif_image*() const { return img; }
    heif_image** operator&() { return &img; }
};

// ---------------------------------------------------------------------------

QStringList HeicDecoder::supportedFormats() {
    return HeicExtensions;
}

bool HeicDecoder::readMetadata(ImageInfo &result) {
    if (!isFormatSupported(result.formatHint()))
        return false;

    HeicContext ctx;
    heif_error err = heif_context_read_from_file(ctx, result.path.toUtf8().constData(), nullptr);
    if (err.code != heif_error_Ok) {
        qDebug() << "HeicDecoder::readMetadata: failed to open" << result.path << err.message;
        return false;
    }

    HeicHandle handle;
    err = heif_context_get_primary_image_handle(ctx, &handle);
    if (err.code != heif_error_Ok)
        return false;

    result.imageSize = QSize(heif_image_handle_get_width(handle),
                             heif_image_handle_get_height(handle));

    // libheif applies all HEIF transformations during decode, so orientation is
    // always correct after decoding — report Horizontal to avoid double-rotation.
    result.orientation = ExifOrientation::Horizontal;

    // Extract Exif block for camera/exposure metadata.
    // The block layout is: [4-byte big-endian skip] ["Exif\0\0" + TIFF data]
    int exifCount = heif_image_handle_get_number_of_metadata_blocks(handle, "Exif");
    if (exifCount > 0) {
        heif_item_id exifId;
        heif_image_handle_get_list_of_metadata_block_IDs(handle, "Exif", &exifId, 1);
        size_t exifSize = heif_image_handle_get_metadata_size(handle, exifId);
        if (exifSize > 4) {
            QByteArray exifBlock(static_cast<int>(exifSize), '\0');
            err = heif_image_handle_get_metadata(handle, exifId, exifBlock.data());
            if (err.code == heif_error_Ok) {
                // Skip the 4-byte TIFF-header-offset field; remaining starts with "Exif\0\0"
                const auto* exifStart = reinterpret_cast<const uint8_t*>(exifBlock.constData() + 4);
                unsigned exifLen = static_cast<unsigned>(exifBlock.size() - 4);
                TinyEXIF::EXIFInfo exifInfo;
                if (exifInfo.parseFromEXIFSegment(exifStart, exifLen) == TinyEXIF::PARSE_SUCCESS) {
                    result.exif = readExifToMap(exifInfo);
                }
            }
        }
    }
    result.exif["Size"] = QFileInfo(result.path).size();

    return true;
}

static bool nclxTransferToQt(heif_transfer_characteristics transfer,
                             QColorSpace::TransferFunction &qtTransfer) {
    switch (transfer) {
    case heif_transfer_characteristic_linear:
        qtTransfer = QColorSpace::TransferFunction::Linear;
        return true;
    case heif_transfer_characteristic_IEC_61966_2_1:
    case heif_transfer_characteristic_ITU_R_BT_709_5:
        qtTransfer = QColorSpace::TransferFunction::SRgb;
        return true;
    case heif_transfer_characteristic_ITU_R_BT_2020_2_10bit:
    case heif_transfer_characteristic_ITU_R_BT_2020_2_12bit:
        qtTransfer = QColorSpace::TransferFunction::Bt2020;
        return true;
    case heif_transfer_characteristic_ITU_R_BT_2100_0_PQ:
        qtTransfer = QColorSpace::TransferFunction::St2084;
        return true;
    case heif_transfer_characteristic_ITU_R_BT_2100_0_HLG:
        qtTransfer = QColorSpace::TransferFunction::Hlg;
        return true;
    default:
        return false;
    }
}

static QColorSpace colorSpaceFromNclx(const heif_color_profile_nclx *nclx) {
    if (!nclx) {
        return QColorSpace(QColorSpace::SRgb);
    }

    QColorSpace::TransferFunction transferFunction;
    if (!nclxTransferToQt(nclx->transfer_characteristics, transferFunction)) {
        return QColorSpace(QColorSpace::SRgb);
    }

    if (nclx->color_primaries == heif_color_primaries_ITU_R_BT_709_5 &&
        transferFunction == QColorSpace::TransferFunction::SRgb) {
        return QColorSpace(QColorSpace::SRgb);
    }
    if (nclx->color_primaries == heif_color_primaries_SMPTE_EG_432_1 &&
        transferFunction == QColorSpace::TransferFunction::SRgb) {
        return QColorSpace(QColorSpace::DisplayP3);
    }
    if (nclx->color_primaries == heif_color_primaries_ITU_R_BT_2020_2_and_2100_0 &&
        transferFunction == QColorSpace::TransferFunction::Bt2020) {
        return QColorSpace(QColorSpace::Bt2020);
    }

    if (nclx->color_primary_white_x > 0.0f && nclx->color_primary_red_x > 0.0f &&
        nclx->color_primary_green_x > 0.0f && nclx->color_primary_blue_x > 0.0f) {
        QColorSpace colorSpace(
            QPointF(nclx->color_primary_white_x, nclx->color_primary_white_y),
            QPointF(nclx->color_primary_red_x, nclx->color_primary_red_y),
            QPointF(nclx->color_primary_green_x, nclx->color_primary_green_y),
            QPointF(nclx->color_primary_blue_x, nclx->color_primary_blue_y),
            transferFunction);
        if (colorSpace.isValid()) {
            return colorSpace;
        }
    }

    return QColorSpace(QColorSpace::SRgb);
}

static QColorSpace colorSpaceFromHeicHandle(heif_image_handle *handle) {
    const size_t iccProfileSize = heif_image_handle_get_raw_color_profile_size(handle);
    if (iccProfileSize > 0) {
        QByteArray iccProfile(static_cast<qsizetype>(iccProfileSize), Qt::Uninitialized);
        const heif_error err = heif_image_handle_get_raw_color_profile(handle, iccProfile.data());
        if (err.code == heif_error_Ok) {
            const QColorSpace colorSpace = QColorSpace::fromIccProfile(iccProfile);
            if (colorSpace.isValid()) {
                return colorSpace;
            }
        }
    }

    heif_color_profile_nclx *nclx = nullptr;
    const heif_error err = heif_image_handle_get_nclx_color_profile(handle, &nclx);
    if (err.code == heif_error_Ok && nclx) {
        const QColorSpace colorSpace = colorSpaceFromNclx(nclx);
        heif_nclx_color_profile_free(nclx);
        return colorSpace;
    }

    return QColorSpace(QColorSpace::SRgb);
}

// Decode a heif_image_handle to a self-owned QImage, respecting alpha.
// Pixels are copied out of the libheif buffer before it is released.
static QImage decodeHandleToQImage(heif_image_handle* handle) {
    const bool hasAlpha = heif_image_handle_has_alpha_channel(handle);
    const auto chroma = hasAlpha ? heif_chroma_interleaved_RGBA : heif_chroma_interleaved_RGB;

    HeicImage img;
    heif_decoding_options* opts = heif_decoding_options_alloc();
    opts->ignore_transformations = 0; // apply rotation/mirror
    heif_error err = heif_decode_image(handle, &img, heif_colorspace_RGB, chroma, opts);
    heif_decoding_options_free(opts);
    if (err.code != heif_error_Ok)
        return {};

    int stride = 0;
    const uint8_t* pixels = heif_image_get_plane_readonly(img, heif_channel_interleaved, &stride);
    int w = heif_image_get_width(img, heif_channel_interleaved);
    int h = heif_image_get_height(img, heif_channel_interleaved);
    if (!pixels || w <= 0 || h <= 0)
        return {};

    const auto qtFormat = hasAlpha ? QImage::Format_RGBA8888 : QImage::Format_RGB888;
    const size_t bytesPerPixel = hasAlpha ? 4 : 3;

    // Copy pixels: HeicImage RAII will release libheif's buffer on return,
    // so the QImage must own its data before that happens.
    QImage result(w, h, qtFormat);
    for (int y = 0; y < h; ++y)
        std::memcpy(result.scanLine(y), pixels + static_cast<ptrdiff_t>(y) * stride, static_cast<size_t>(w) * bytesPerPixel);
    result.setColorSpace(colorSpaceFromHeicHandle(handle));
    return result;
}

// Decode a heif_image_handle to a PNG-compressed QByteArray.
// PNG is a built-in Qt image handler in the reusable package, whereas the
// optional qjpeg plugin is not deployed by every host (including f4). Using
// qimg.save(..., "JPEG") therefore silently discarded otherwise valid HEIC
// embedded thumbnails in those hosts and forced an expensive full decode.
// Returns an empty array on failure.
static QByteArray decodeHandleToPng(heif_image_handle* handle,
                                    QSize* decodedSize = nullptr) {
    QImage qimg = decodeHandleToQImage(handle);
    if (qimg.isNull())
        return {};

    if (decodedSize) {
        *decodedSize = qimg.size();
    }

    const QColorSpace srgb(QColorSpace::SRgb);
    if (qimg.colorSpace().isValid() && qimg.colorSpace() != srgb) {
        QImage converted = qimg.convertedToColorSpace(srgb, qimg.format());
        if (!converted.isNull()) {
            qimg = converted;
        }
    }
    qimg.setColorSpace(srgb);

    QByteArray pngBytes;
    QBuffer buf(&pngBytes);
    buf.open(QBuffer::WriteOnly);
    if (!qimg.save(&buf, "PNG")) {
        return {};
    }
    return pngBytes;
}

bool HeicDecoder::readPreviewAndMime(ImageData &result) {
    if (!isFormatSupported(result.request.info.formatHint()))
        return false;

    HeicContext ctx;
    heif_error err = heif_context_read_from_file(
        ctx, result.request.info.path.toUtf8().constData(), nullptr);
    if (err.code != heif_error_Ok)
        return false;

    HeicHandle primary;
    err = heif_context_get_primary_image_handle(ctx, &primary);
    if (err.code != heif_error_Ok)
        return false;

    // Always set the MIME type so the full-decode path works correctly.
    result.mimeType = "image/heic";

    // Try embedded thumbnail first (fast path for masonry thumbnails).
    int thumbCount = heif_image_handle_get_number_of_thumbnails(primary);
    if (thumbCount > 0) {
        heif_item_id thumbId;
        heif_image_handle_get_list_of_thumbnail_IDs(primary, &thumbId, 1);

        HeicHandle thumb;
        err = heif_image_handle_get_thumbnail(primary, thumbId, &thumb);
        if (err.code == heif_error_Ok) {
            QSize previewSize;
            QByteArray pngBytes = decodeHandleToPng(thumb, &previewSize);
            if (!pngBytes.isEmpty()) {
                result.previewDataSize = pngBytes.size();
                result.previewData = std::shared_ptr<char>(
                    new char[pngBytes.size()],
                    [](char* p) { delete[] p; });
                std::memcpy(result.previewData.get(), pngBytes.constData(),
                            pngBytes.size());
                result.previewMimeType = "image/png";
                result.previewUsed = "HEIC thumbnail";

                QSize requiredSize = result.request.targetSize;
                if (!result.request.checkCache &&
                    result.request.expandToCacheResolution) {
                    requiredSize = expandToCacheImageResolution(requiredSize);
                }
                requiredSize = rotateToOrientation(
                    requiredSize, result.request.info.orientation);
                const bool previewCoversTarget =
                    requiredSize.width() > 0 && requiredSize.height() > 0 &&
                    previewSize.width() >= requiredSize.width() &&
                    previewSize.height() >= requiredSize.height();
                if (previewCoversTarget) {
                    return true;
                }
                // Keep the embedded frame as a fallback, but also read the
                // source. ThumbnailLoader prefers source data and will only
                // fall back to this preview if the full HEIC decode fails.
            }
        }
    }

    // No preview covering the requested target — read the whole source.
    QFile f(result.request.info.path);
    if (!f.open(QFile::ReadOnly))
        return false;
    result.data = f.readAll();
    return !result.data.isEmpty();
}

QImage HeicDecoder::decode(const QString &mimeType, const QByteArray &data, QSize targetSize) {
    if (mimeType != "image/heic")
        return {};

    HeicContext ctx;
    heif_error err = heif_context_read_from_memory_without_copy(
        ctx, data.constData(), static_cast<size_t>(data.size()), nullptr);
    if (err.code != heif_error_Ok) {
        qDebug() << "HeicDecoder::decode: context read failed:" << err.message;
        return {};
    }

    HeicHandle handle;
    err = heif_context_get_primary_image_handle(ctx, &handle);
    if (err.code != heif_error_Ok)
        return {};

    QImage result = decodeHandleToQImage(handle);
    if (result.isNull()) {
        qDebug() << "HeicDecoder::decode: pixel decode failed";
        return {};
    }

    if (!targetSize.isEmpty() &&
        (result.width() > targetSize.width() || result.height() > targetSize.height())) {
        result = result.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return result;
}
