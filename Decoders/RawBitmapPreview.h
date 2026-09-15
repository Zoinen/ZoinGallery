#pragma once

#include <QImage>
#include <cstring>

// LibRaw bitmap thumbnails contain native-endian samples, not JPEG bytes.
inline QImage rawBitmapPreview(const unsigned char *data, size_t size,
                              int width, int height, int colors, int bits) {
    if (!data || width <= 0 || height <= 0 ||
        (colors != 1 && colors != 3 && colors != 4) ||
        (bits != 8 && bits != 16)) return {};
    const quint64 required = quint64(width) * height * colors * (bits / 8);
    if (required > size) return {};
    QImage image(width, height, QImage::Format_RGB888);
    if (image.isNull()) return {};
    const auto sample = [data, bits](size_t index) -> unsigned char {
        if (bits == 8) return data[index];
        quint16 value;
        std::memcpy(&value, data + index * 2, sizeof(value));
        return static_cast<unsigned char>(value >> 8);
    };
    for (int y = 0; y < height; ++y) {
        auto *row = image.scanLine(y);
        for (int x = 0; x < width; ++x) {
            const size_t offset = (size_t(y) * width + x) * colors;
            for (int c = 0; c < 3; ++c)
                row[x * 3 + c] = sample(offset + (colors == 1 ? 0 : c));
        }
    }
    return image;
}
