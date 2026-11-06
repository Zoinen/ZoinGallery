#pragma once

#include <QColorSpace>
#include <QImage>
#include <QTransform>
#include <QVideoFrame>
#include <QVideoFrameFormat>
#include <algorithm>
#include <array>
#include <cmath>

namespace ZoinGallery::VideoColorSpace {

inline bool isHdr(const QVideoFrameFormat &format)
{
    return format.colorTransfer() == QVideoFrameFormat::ColorTransfer_STD_B67
        || format.colorTransfer() == QVideoFrameFormat::ColorTransfer_ST2084;
}

inline float encodedWhite(float linear, bool hlg)
{
    if (hlg)
        return linear < 1.f / 12 ? std::sqrt(3 * linear)
            : .17883277f * std::log(12 * linear - .28466892f) + .55991073f;
    const float power = std::pow(linear / 100, 1305.f / 8192);
    return std::pow((107.f / 128 + 2413.f / 128 * power)
                   / (1 + 2392.f / 128 * power), 2523.f / 32);
}

inline float toneScale(float signal, float white, float output)
{
    const float knee = 1.5f * output - .5f;
    const float relative = signal / white;
    if (signal <= 0 || relative < knee || knee >= 1)
        return 1;
    const float t = (relative - knee) / (1 - knee);
    const float mapped = knee + (1 - knee) * t
        + (3 * output - knee - 2) * t * t
        + (knee + 1 - 2 * output) * t * t * t;
    return mapped / relative;
}

inline QRgb sdrPixel(std::array<float, 3> rgb, float signal, bool hlg,
                      float white, float output)
{
    const float scale = toneScale(signal, white, output);
    for (float &channel : rgb) {
        const float value = std::max(0.f, channel * scale);
        if (hlg) {
            channel = value < .5f ? value * value / 3
                : (std::exp((value - .55991073f) / .17883277f) + .28466892f) / 12;
        } else {
            const float power = std::pow(value, 32.f / 2523);
            channel = 100 * std::pow(std::max(0.f, power - 107.f / 128)
                / std::max(1e-6f, 2413.f / 128 - 2392.f / 128 * power), 8192.f / 1305);
        }
    }
    if (hlg) {
        const float gain = output * std::pow(std::max(0.f,
            .2627f * rgb[0] + .6780f * rgb[1] + .0593f * rgb[2]), .2f);
        for (float &channel : rgb)
            channel *= gain;
    }
    const auto encode = [](float linear) {
        linear = std::clamp(linear, 0.f, 1.f);
        return qRound(255 * (linear <= .0031308f ? 12.92f * linear
            : 1.055f * std::pow(linear, 1.f / 2.4f) - .055f));
    };
    return qRgb(encode(1.6605f * rgb[0] - .5876f * rgb[1] - .0728f * rgb[2]),
                encode(-.1246f * rgb[0] + 1.1329f * rgb[1] - .0083f * rgb[2]),
                encode(-.0182f * rgb[0] - .1006f * rgb[1] + 1.1187f * rgb[2]));
}

inline QImage cpuHdrToSdr(QVideoFrame frame)
{
    // Keep 10/16-bit source precision through tone mapping. Converting to
    // encoded RGB8 first loses shadow detail, particularly for PQ sources.
    const auto format = frame.surfaceFormat();
    if (!isHdr(format)
        || (format.pixelFormat() != QVideoFrameFormat::Format_P010
            && format.pixelFormat() != QVideoFrameFormat::Format_P016)
        || !frame.map(QVideoFrame::ReadOnly))
        return {};
    QImage image(frame.size(), QImage::Format_RGB32);
    if (image.isNull()) {
        frame.unmap();
        return {};
    }
    const bool hlg = format.colorTransfer() == QVideoFrameFormat::ColorTransfer_STD_B67;
    const float white = encodedWhite(format.maxLuminance() / 100, hlg);
    const float output = encodedWhite(1, hlg);
    const bool full = format.colorRange() == QVideoFrameFormat::ColorRange_Full;
    // Normalized YUV matrices match Qt's BT.709/BT.2020 conversion, including
    // its limited-range offsets. HDR gamut conversion happens after the EOTF.
    const std::array<float, 12> matrix = format.colorSpace() == QVideoFrameFormat::ColorSpace_BT2020
        ? (full ? std::array<float, 12>{1, 0, 1.4746f, -.7402f, 1, -.1646f, -.5714f, .3694f,
                                        1, 1.8814f, 0, -.9445f}
                : std::array<float, 12>{1.1644f, 0, 1.6787f, -.9157f,
                    1.1644f, -.1874f, -.6504f, .3475f, 1.1644f, 2.1418f, 0, -1.1483f})
        : (full ? std::array<float, 12>{1, 0, 1.5748f, -.790488f,
                    1, -.187324f, -.468124f, .329010f, 1, 1.8556f, 0, -.931439f}
                : std::array<float, 12>{1.1644f, 0, 1.7927f, -.9729f,
                    1.1644f, -.2132f, -.5329f, .3015f, 1.1644f, 2.1124f, 0, -1.1334f});
    const int chromaWidth = (frame.width() + 1) / 2;
    const int chromaHeight = (frame.height() + 1) / 2;
    const auto chromaAt = [&](int x, int y, int component) {
        const auto *row = reinterpret_cast<const quint16 *>(frame.bits(1)
            + std::clamp(y, 0, chromaHeight - 1) * frame.bytesPerLine(1));
        return row[2 * std::clamp(x, 0, chromaWidth - 1) + component] / 65535.f;
    };
    for (int y = 0; y < image.height(); ++y) {
        const auto *luma = reinterpret_cast<const quint16 *>(frame.bits(0) + y * frame.bytesPerLine(0));
        auto *pixels = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const int cx = x / 2 - (x % 2 == 0 ? 1 : 0);
            const int cy = y / 2 - (y % 2 == 0 ? 1 : 0);
            const float wx = x % 2 == 0 ? .75f : .25f;
            const float wy = y % 2 == 0 ? .75f : .25f;
            const auto chroma = [&](int component) {
                return (1 - wy) * ((1 - wx) * chromaAt(cx, cy, component)
                                        + wx * chromaAt(cx + 1, cy, component))
                    + wy * ((1 - wx) * chromaAt(cx, cy + 1, component)
                                  + wx * chromaAt(cx + 1, cy + 1, component));
            };
            const float yy = luma[x] / 65535.f, u = chroma(0), v = chroma(1);
            std::array<float, 3> rgb{};
            for (int channel = 0; channel < 3; ++channel) {
                const int i = channel * 4;
                rgb[channel] = matrix[i] * yy + matrix[i + 1] * u + matrix[i + 2] * v + matrix[i + 3];
            }
            pixels[x] = sdrPixel(rgb, (yy - 16.f / 256) * 256 / 219, hlg, white, output);
        }
    }
    frame.unmap();
    image.setColorSpace(QColorSpace(QColorSpace::SRgb));
    return image;
}

inline QImage normalizeHdrImage(QImage image, const QVideoFrame &frame)
{
    const auto format = frame.surfaceFormat();
    if (image.isNull() || !isHdr(format)
        || (format.pixelFormat() != QVideoFrameFormat::Format_P010
            && format.pixelFormat() != QVideoFrameFormat::Format_P016))
        return image;
    if (image.format() != QImage::Format_RGBA8888_Premultiplied) {
        image = cpuHdrToSdr(frame);
        const QTransform surface = QTransform().scale(1,
            format.scanLineDirection() == QVideoFrameFormat::BottomToTop ? -1 : 1)
            * QTransform().rotate(int(format.rotation()))
            * QTransform().scale(format.isMirrored() ? -1 : 1, 1);
        return image.transformed(surface);
    }

    // Qt's GPU HDR shader writes an artificial alpha even for opaque P010/
    // P016. Its RGB has already been tone mapped and is not premultiplied by
    // that alpha. Discard it without unpremultiplying the valid RGB channels.
    image.reinterpretAsFormat(QImage::Format_RGBX8888);
    image = image.convertToFormat(QImage::Format_RGB32);
    image.setColorSpace(QColorSpace(QColorSpace::SRgb));
    return image;
}

} // namespace ZoinGallery::VideoColorSpace
