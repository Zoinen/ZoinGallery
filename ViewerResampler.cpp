#include "ViewerResampler.h"

#include <QColorSpace>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace ZoinGallery::ViewerResampler {

namespace {

constexpr int MaximumTaps = 8;
constexpr int TransferTableSize = 65536;

constexpr std::array<int, 64> DitherRanks = {
     0, 48, 12, 60,  3, 51, 15, 63,
    32, 16, 44, 28, 35, 19, 47, 31,
     8, 56,  4, 52, 11, 59,  7, 55,
    40, 24, 36, 20, 43, 27, 39, 23,
     2, 50, 14, 62,  1, 49, 13, 61,
    34, 18, 46, 30, 33, 17, 45, 29,
    10, 58,  6, 54,  9, 57,  5, 53,
    42, 26, 38, 22, 41, 25, 37, 21
};

struct TransferTables {
    std::array<float, TransferTableSize> decode;
    std::array<float, TransferTableSize> encode;
    std::array<float, 256 * 256> premultiplied8;

    TransferTables() {
        const auto toLinear = [](double value) {
            return value <= 0.04045 ? value / 12.92
                                   : std::pow((value + 0.055) / 1.055, 2.4);
        };
        for (int i = 0; i < TransferTableSize; ++i) {
            const double value = static_cast<double>(i) / (TransferTableSize - 1);
            decode[i] = static_cast<float>(toLinear(value));
            encode[i] = static_cast<float>(value <= 0.0031308 ? value * 12.92
                : 1.055 * std::pow(value, 1.0 / 2.4) - 0.055);
        }
        for (int alpha = 0; alpha < 256; ++alpha) {
            for (int color = 0; color < 256; ++color) {
                premultiplied8[alpha * 256 + color] = alpha == 0 ? 0
                    : static_cast<float>(toLinear(static_cast<double>(std::min(color, alpha))
                                                   / alpha) * alpha / 255.0);
            }
        }
    }
};

const TransferTables &transferTables() {
    static const TransferTables tables;
    return tables;
}

float transfer(const std::array<float, TransferTableSize> &table, float value) {
    const float index = std::clamp(value, 0.0F, 1.0F) * (TransferTableSize - 1);
    const int lower = static_cast<int>(index);
    const int upper = std::min(lower + 1, TransferTableSize - 1);
    return table[lower] + (table[upper] - table[lower]) * (index - lower);
}

struct AxisWeights {
    std::array<int, MaximumTaps> indices{};
    std::array<float, MaximumTaps> weights{};
    int count = 0;
};

struct Pixel {
    float r = 0;
    float g = 0;
    float b = 0;
    float a = 0;
};

Pixel channels(QRgb sample, const TransferTables &tables) {
    const int alpha = qAlpha(sample);
    const float *colors = tables.premultiplied8.data() + alpha * 256;
    return {colors[qRed(sample)], colors[qGreen(sample)], colors[qBlue(sample)],
            alpha / 255.0F};
}

Pixel channels(QRgba64 sample, const TransferTables &tables) {
    const int alpha = sample.alpha();
    if (alpha == 0) {
        return {};
    }
    if (alpha == 65535) {
        return {tables.decode[sample.red()], tables.decode[sample.green()],
                tables.decode[sample.blue()], 1};
    }
    const float inverseAlpha = 1.0F / alpha;
    const float a = alpha / 65535.0F;
    return {transfer(tables.decode, sample.red() * inverseAlpha) * a,
            transfer(tables.decode, sample.green() * inverseAlpha) * a,
            transfer(tables.decode, sample.blue() * inverseAlpha) * a, a};
}

template<typename Sample>
void linearizeRow(const Sample *input, std::vector<Pixel> &row,
                  const TransferTables &tables) {
    for (size_t x = 0; x < row.size(); ++x) {
        row[x] = channels(input[x], tables);
    }
}

void filterRow(const std::vector<Pixel> &input, const std::vector<AxisWeights> &weights,
               std::vector<Pixel> &row) {
    for (size_t x = 0; x < row.size(); ++x) {
        Pixel pixel;
        const AxisWeights &taps = weights[x];
        for (int i = 0; i < taps.count; ++i) {
            const Pixel &sample = input[taps.indices[i]];
            const float weight = taps.weights[i];
            pixel.r += sample.r * weight;
            pixel.g += sample.g * weight;
            pixel.b += sample.b * weight;
            pixel.a += sample.a * weight;
        }
        row[x] = pixel;
    }
}

// The sampling transfer matches the viewer shader; color-space tags and
// profile conversion belong to the existing decode/display pipeline.
double cubic(double distance) {
    const double x = std::abs(distance);
    constexpr double b = -0.4;
    constexpr double c = 0.8;
    if (x < 1.0) {
        return ((12 - 9 * b - 6 * c) * x * x * x
                + (-18 + 12 * b + 6 * c) * x * x + 6 - 2 * b) / 6;
    }
    if (x < 2.0) {
        return ((-b - 6 * c) * x * x * x
                + (6 * b + 30 * c) * x * x
                + (-12 * b - 48 * c) * x + 8 * b + 24 * c) / 6;
    }
    return 0;
}

std::vector<AxisWeights> axisWeights(int sourceSize, int targetSize, bool exactHalf) {
    std::vector<AxisWeights> result(targetSize);
    // A pyramid stage covers exactly 2 * targetSize source texels. An odd
    // unpaired texel must not stretch the grid across the entire image.
    const double scale = exactHalf ? 0.5 : static_cast<double>(targetSize) / sourceSize;
    for (int output = 0; output < targetSize; ++output) {
        AxisWeights &taps = result[output];
        if (sourceSize == targetSize) {
            taps.count = 1;
            taps.indices[0] = output;
            taps.weights[0] = 1;
            continue;
        }
        const double center = (output + 0.5) / scale - 0.5;
        const double radius = 2.0 / scale;
        double sum = 0;
        for (int input = static_cast<int>(std::ceil(center - radius));
             input <= static_cast<int>(std::floor(center + radius)); ++input) {
            const double weight = cubic((input - center) * scale);
            if (weight == 0) {
                continue;
            }
            // Each stage reduces by at most two, so the open support has
            // at most eight source texels. Edge extension repeats endpoints.
            Q_ASSERT(taps.count < MaximumTaps);
            taps.indices[taps.count] = std::clamp(input, 0, sourceSize - 1);
            taps.weights[taps.count] = static_cast<float>(weight);
            ++taps.count;
            sum += weight;
        }
        for (int tap = 0; tap < taps.count; ++tap) {
            taps.weights[tap] /= static_cast<float>(sum);
        }
    }
    return result;
}

QImage reduceStage(const QImage &source, QSize target, bool halfX, bool halfY) {
    QImage result(target, QImage::Format_ARGB32_Premultiplied);
    if (result.isNull()) {
        return {};
    }
    const auto horizontal = axisWeights(source.width(), target.width(), halfX);
    const auto vertical = axisWeights(source.height(), target.height(), halfY);
    const auto &tables = transferTables();

    // Only the vertical filter's eight active source rows need horizontal
    // intermediates. A 40 MP source does not require a 640 MB float frame.
    std::array<std::vector<Pixel>, MaximumTaps> rows;
    // Linearize each source row once, not once per filter tap. LUTs retain
    // 16-bit input precision without a power function in the pixel loops.
    std::vector<Pixel> linearInput(source.width());
    std::array<int, MaximumTaps> rowIndices;
    rowIndices.fill(-1);
    for (auto &row : rows) {
        row.resize(target.width());
    }
    for (int y = 0; y < target.height(); ++y) {
        const AxisWeights &yTaps = vertical[y];
        for (int tap = 0; tap < yTaps.count; ++tap) {
            const int inputY = yTaps.indices[tap];
            const int slot = inputY % MaximumTaps;
            if (rowIndices[slot] == inputY) {
                continue;
            }
            rowIndices[slot] = inputY;
            auto &row = rows[slot];
            if (source.format() == QImage::Format_RGBA64_Premultiplied) {
                linearizeRow(reinterpret_cast<const QRgba64 *>(source.constScanLine(inputY)),
                             linearInput, tables);
            } else {
                linearizeRow(reinterpret_cast<const QRgb *>(source.constScanLine(inputY)),
                             linearInput, tables);
            }
            filterRow(linearInput, horizontal, row);
        }
        auto *output = reinterpret_cast<QRgb *>(result.scanLine(y));
        for (int x = 0; x < target.width(); ++x) {
            Pixel pixel;
            for (int tap = 0; tap < yTaps.count; ++tap) {
                const Pixel &sample = rows[yTaps.indices[tap] % MaximumTaps][x];
                const float weight = yTaps.weights[tap];
                pixel.r += sample.r * weight;
                pixel.g += sample.g * weight;
                pixel.b += sample.b * weight;
                pixel.a += sample.a * weight;
            }
            // Negative lobes may overshoot. Clamp only after both axes have
            // been filtered, preserving valid premultiplied alpha for Qt.
            const float a = std::clamp(pixel.a, 0.0F, 1.0F);
            const int alpha = qRound(a * 255);
            if (alpha == 0) {
                output[x] = 0;
                continue;
            }
            const float dither = (2 * DitherRanks[(y % 8) * 8 + x % 8] - 63) / 128.0F;
            const auto encode = [&](float value) {
                const float encoded = transfer(tables.encode,
                    std::clamp(value, 0.0F, a) / a) * a * 255;
                return std::clamp(qRound(encoded + dither), 0, alpha);
            };
            output[x] = qRgba(encode(pixel.r), encode(pixel.g), encode(pixel.b), alpha);
        }
    }
    return result;
}

}

QImage downsample(const QImage &image, QSize targetSize) {
    if (image.isNull() || targetSize.isEmpty()) {
        return {};
    }
    targetSize = targetSize.boundedTo(image.size());
    if (targetSize == image.size()) {
        return image;
    }
    QImage result = image;
    if (result.depth() > 32) {
        // Preserve 16-bit source precision through the first convolution;
        // pyramid stages are then quantized to the viewer's 8-bit GPU format.
        result = result.convertToFormat(QImage::Format_RGBA64_Premultiplied);
    } else if (result.format() != QImage::Format_RGB32
        && result.format() != QImage::Format_ARGB32_Premultiplied) {
        result = result.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    }
    // For thin or anisotropic requests, each axis stops halving separately;
    // the remaining reduction never needs more than eight source taps.
    while (!result.isNull() && result.size() != targetSize) {
        const bool halfX = result.width() / 2 >= targetSize.width();
        const bool halfY = result.height() / 2 >= targetSize.height();
        const QSize stage(halfX ? result.width() / 2 : targetSize.width(),
                          halfY ? result.height() / 2 : targetSize.height());
        result = reduceStage(result, stage, halfX, halfY);
    }
    if (!result.isNull()) {
        result.setColorSpace(image.colorSpace());
        result.setDevicePixelRatio(image.devicePixelRatio());
        result.setDotsPerMeterX(image.dotsPerMeterX());
        result.setDotsPerMeterY(image.dotsPerMeterY());
        result.setOffset(image.offset());
        for (const QString &key : image.textKeys()) {
            result.setText(key, image.text(key));
        }
    }
    return result;
}

}
