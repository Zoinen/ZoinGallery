#include "WebpCodec.h"

#include <QColorSpace>
#include <QtGlobal>

#include <webp/decode.h>
#include <webp/demux.h>
#include <webp/encode.h>

#include <cstring>

namespace {

QSize scaledSize(const QSize &sourceSize, const QSize &targetSize) {
    if (!targetSize.isValid()
        || (sourceSize.width() <= targetSize.width() && sourceSize.height() <= targetSize.height())) {
        return sourceSize;
    }
    return sourceSize.scaled(targetSize, Qt::KeepAspectRatio);
}

QImage decodeStaticWebp(const QByteArray &data, const QSize &targetSize) {
    WebPDecoderConfig config;
    if (!WebPInitDecoderConfig(&config)) {
        return {};
    }

    const auto *bytes = reinterpret_cast<const uint8_t *>(data.constData());
    if (WebPGetFeatures(bytes, static_cast<size_t>(data.size()), &config.input) != VP8_STATUS_OK) {
        return {};
    }

    const QSize outputSize = scaledSize(QSize(config.input.width, config.input.height), targetSize);
    const bool hasAlpha = config.input.has_alpha != 0;
    QImage image(outputSize, hasAlpha ? QImage::Format_RGBA8888 : QImage::Format_RGB888);
    if (image.isNull()) {
        return {};
    }

    config.options.use_threads = 1;
    if (outputSize != QSize(config.input.width, config.input.height)) {
        config.options.use_scaling = 1;
        config.options.scaled_width = outputSize.width();
        config.options.scaled_height = outputSize.height();
    }
    config.output.colorspace = hasAlpha ? MODE_RGBA : MODE_RGB;
    config.output.is_external_memory = 1;
    config.output.u.RGBA.rgba = image.bits();
    config.output.u.RGBA.stride = image.bytesPerLine();
    config.output.u.RGBA.size = static_cast<size_t>(image.sizeInBytes());

    const VP8StatusCode status = WebPDecode(bytes, static_cast<size_t>(data.size()), &config);
    WebPFreeDecBuffer(&config.output);
    if (status != VP8_STATUS_OK) {
        return {};
    }
    return image;
}

QImage decodeAnimatedWebp(const QByteArray &data, const QSize &targetSize) {
    const WebPData webpData {
        reinterpret_cast<const uint8_t *>(data.constData()),
        static_cast<size_t>(data.size())
    };
    WebPAnimDecoderOptions options;
    if (!WebPAnimDecoderOptionsInit(&options)) {
        return {};
    }
    options.color_mode = MODE_RGBA;
    options.use_threads = 1;

    WebPAnimDecoder *decoder = WebPAnimDecoderNew(&webpData, &options);
    if (!decoder) {
        return {};
    }

    WebPAnimInfo info;
    uint8_t *frame = nullptr;
    int timestamp = 0;
    if (!WebPAnimDecoderGetInfo(decoder, &info)
        || !WebPAnimDecoderGetNext(decoder, &frame, &timestamp)
        || !frame) {
        WebPAnimDecoderDelete(decoder);
        return {};
    }

    QImage image(static_cast<int>(info.canvas_width), static_cast<int>(info.canvas_height),
                 QImage::Format_RGBA8888);
    if (!image.isNull()) {
        const qsizetype sourceStride = static_cast<qsizetype>(info.canvas_width) * 4;
        for (int y = 0; y < image.height(); ++y) {
            std::memcpy(image.scanLine(y), frame + y * sourceStride,
                        static_cast<size_t>(sourceStride));
        }
    }
    WebPAnimDecoderDelete(decoder);

    const QSize outputSize = scaledSize(image.size(), targetSize);
    if (!image.isNull() && outputSize != image.size()) {
        image = image.scaled(outputSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    return image;
}

int writeWebp(const uint8_t *data, size_t dataSize, const WebPPicture *picture) {
    auto *output = static_cast<QByteArray *>(picture->custom_ptr);
    output->append(reinterpret_cast<const char *>(data), static_cast<qsizetype>(dataSize));
    return 1;
}

}

namespace WebpCodec {

Features readFeatures(const QByteArray &data) {
    WebPBitstreamFeatures features;
    if (data.isEmpty()
        || WebPGetFeatures(reinterpret_cast<const uint8_t *>(data.constData()),
                           static_cast<size_t>(data.size()), &features) != VP8_STATUS_OK) {
        return {};
    }
    return {
        .size = QSize(features.width, features.height),
        .hasAlpha = features.has_alpha != 0,
        .hasAnimation = features.has_animation != 0,
    };
}

QImage decode(const QByteArray &data, const QSize &targetSize) {
    const Features features = readFeatures(data);
    if (!features.isValid()) {
        return {};
    }

    QImage image = features.hasAnimation
        ? decodeAnimatedWebp(data, targetSize)
        : decodeStaticWebp(data, targetSize);
    if (!image.isNull()) {
        image.setColorSpace(QColorSpace(QColorSpace::SRgb));
    }
    return image;
}

QByteArray encode(const QImage &image, float quality) {
    if (image.isNull()) {
        return {};
    }

    WebPConfig config;
    if (!WebPConfigPreset(&config, WEBP_PRESET_PHOTO, qBound(0.0F, quality, 100.0F))) {
        return {};
    }
    config.method = 3;
    config.thread_level = 1;
    config.alpha_quality = 100;
    config.exact = 1;
    if (!WebPValidateConfig(&config)) {
        return {};
    }

    const bool hasAlpha = image.hasAlphaChannel();
    const QImage pixels = image.convertToFormat(
        hasAlpha ? QImage::Format_RGBA8888 : QImage::Format_RGB888);
    if (pixels.isNull()) {
        return {};
    }

    WebPPicture picture;
    if (!WebPPictureInit(&picture)) {
        return {};
    }
    picture.width = pixels.width();
    picture.height = pixels.height();

    QByteArray output;
    picture.writer = writeWebp;
    picture.custom_ptr = &output;
    const int imported = hasAlpha
        ? WebPPictureImportRGBA(&picture, pixels.constBits(), pixels.bytesPerLine())
        : WebPPictureImportRGB(&picture, pixels.constBits(), pixels.bytesPerLine());
    const int encoded = imported ? WebPEncode(&config, &picture) : 0;
    WebPPictureFree(&picture);
    return encoded ? output : QByteArray();
}

}
