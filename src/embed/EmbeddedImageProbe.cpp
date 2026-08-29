#include "EmbeddedImageProbe.h"

#include <QtEndian>

#include <algorithm>

namespace ZoinGallery {
namespace {

constexpr uchar JpegMarkerPrefix = 0xff;
constexpr uchar JpegStartOfImage = 0xd8;
constexpr uchar JpegStartOfScan = 0xda;
constexpr uchar JpegApp1 = 0xe1;

enum class ByteOrder {
    Little,
    Big,
};

quint16 readU16(const uchar *data, ByteOrder order)
{
    return order == ByteOrder::Little
        ? qFromLittleEndian<quint16>(data)
        : qFromBigEndian<quint16>(data);
}

quint32 readU32(const uchar *data, ByteOrder order)
{
    return order == ByteOrder::Little
        ? qFromLittleEndian<quint32>(data)
        : qFromBigEndian<quint32>(data);
}

bool boundedSpan(qsizetype offset, qsizetype length, qsizetype size)
{
    return offset >= 0 && length >= 0 && offset <= size
        && length <= size - offset;
}

bool tiffIfdBounds(const QByteArray &tiff, ByteOrder order, quint32 offset,
                   quint16 *entryCount, qsizetype *nextOffsetPosition)
{
    const qsizetype at = static_cast<qsizetype>(offset);
    if (!boundedSpan(at, 2, tiff.size())) {
        return false;
    }
    const auto *bytes = reinterpret_cast<const uchar *>(tiff.constData());
    const quint16 count = readU16(bytes + at, order);
    constexpr qsizetype EntryBytes = 12;
    // count is only 16-bit, so the multiplication is safely representable in
    // qsizetype on every platform supported by Qt 6.
    const qsizetype entriesBytes = static_cast<qsizetype>(count) * EntryBytes;
    if (!boundedSpan(at + 2, entriesBytes + 4, tiff.size())) {
        return false;
    }
    *entryCount = count;
    *nextOffsetPosition = at + 2 + entriesBytes;
    return true;
}

quint32 tiffInlineValue(const uchar *entry, ByteOrder order)
{
    const quint16 type = readU16(entry + 2, order);
    const quint32 count = readU32(entry + 4, order);
    if (count != 1) {
        return 0;
    }
    // SHORT is stored in the first two bytes of the four-byte value field;
    // LONG occupies the complete field.
    if (type == 3) {
        return readU16(entry + 8, order);
    }
    if (type == 4) {
        return readU32(entry + 8, order);
    }
    return 0;
}

QSize jpegDimensions(const QByteArray &jpeg)
{
    if (jpeg.size() < 4
        || static_cast<uchar>(jpeg[0]) != JpegMarkerPrefix
        || static_cast<uchar>(jpeg[1]) != JpegStartOfImage) {
        return {};
    }
    qsizetype cursor = 2;
    while (cursor + 4 <= jpeg.size()) {
        if (static_cast<uchar>(jpeg[cursor]) != JpegMarkerPrefix) {
            return {};
        }
        while (cursor < jpeg.size()
               && static_cast<uchar>(jpeg[cursor]) == JpegMarkerPrefix) {
            ++cursor;
        }
        if (cursor >= jpeg.size()) {
            return {};
        }
        const uchar marker = static_cast<uchar>(jpeg[cursor++]);
        if (marker == JpegStartOfImage || marker == 0x01
            || (marker >= 0xd0 && marker <= 0xd9)) {
            continue;
        }
        if (marker == JpegStartOfScan || cursor + 2 > jpeg.size()) {
            return {};
        }
        const auto *lengthBytes = reinterpret_cast<const uchar *>(
            jpeg.constData() + cursor);
        const quint16 segmentLength = qFromBigEndian<quint16>(lengthBytes);
        if (segmentLength < 2
            || !boundedSpan(cursor, segmentLength, jpeg.size())) {
            return {};
        }
        const bool isStartOfFrame = marker >= 0xc0 && marker <= 0xcf
            && marker != 0xc4 && marker != 0xc8 && marker != 0xcc;
        if (isStartOfFrame && segmentLength >= 7) {
            const auto *segment = reinterpret_cast<const uchar *>(
                jpeg.constData() + cursor + 2);
            const int height = qFromBigEndian<quint16>(segment + 1);
            const int width = qFromBigEndian<quint16>(segment + 3);
            return width > 0 && height > 0 ? QSize(width, height) : QSize();
        }
        cursor += segmentLength;
    }
    return {};
}

struct ExifPreview {
    QByteArray bytes;
    int orientation = 1;
    QString diagnostic;
};

ExifPreview parseExifPreview(const QByteArray &app1)
{
    ExifPreview result;
    static const QByteArray ExifPrefix("Exif\0\0", 6);
    if (!app1.startsWith(ExifPrefix)) {
        result.diagnostic = QStringLiteral("APP1 is not Exif");
        return result;
    }
    const QByteArray tiff = app1.mid(ExifPrefix.size());
    if (tiff.size() < 8) {
        result.diagnostic = QStringLiteral("Exif TIFF header is truncated");
        return result;
    }
    const auto *bytes = reinterpret_cast<const uchar *>(tiff.constData());
    ByteOrder order;
    if (tiff.startsWith("II")) {
        order = ByteOrder::Little;
    }
    else if (tiff.startsWith("MM")) {
        order = ByteOrder::Big;
    }
    else {
        result.diagnostic = QStringLiteral("Exif TIFF byte order is invalid");
        return result;
    }
    if (readU16(bytes + 2, order) != 42) {
        result.diagnostic = QStringLiteral("Exif TIFF magic is invalid");
        return result;
    }

    const quint32 ifd0Offset = readU32(bytes + 4, order);
    quint16 ifd0Count = 0;
    qsizetype ifd1PointerPosition = 0;
    if (!tiffIfdBounds(tiff, order, ifd0Offset, &ifd0Count,
                       &ifd1PointerPosition)) {
        result.diagnostic = QStringLiteral("Exif IFD0 is outside APP1");
        return result;
    }
    const qsizetype ifd0 = static_cast<qsizetype>(ifd0Offset);
    for (quint16 index = 0; index < ifd0Count; ++index) {
        const auto *entry = bytes + ifd0 + 2 + static_cast<qsizetype>(index) * 12;
        if (readU16(entry, order) == 0x0112) {
            const quint32 value = tiffInlineValue(entry, order);
            if (value >= 1 && value <= 8) {
                result.orientation = static_cast<int>(value);
            }
        }
    }

    const quint32 ifd1Offset = readU32(bytes + ifd1PointerPosition, order);
    if (ifd1Offset == 0) {
        result.diagnostic = QStringLiteral("Exif has no IFD1");
        return result;
    }
    quint16 ifd1Count = 0;
    qsizetype ignoredNextPosition = 0;
    if (!tiffIfdBounds(tiff, order, ifd1Offset, &ifd1Count,
                       &ignoredNextPosition)) {
        result.diagnostic = QStringLiteral("Exif IFD1 is outside APP1");
        return result;
    }

    quint32 previewOffset = 0;
    quint32 previewLength = 0;
    const qsizetype ifd1 = static_cast<qsizetype>(ifd1Offset);
    for (quint16 index = 0; index < ifd1Count; ++index) {
        const auto *entry = bytes + ifd1 + 2 + static_cast<qsizetype>(index) * 12;
        const quint16 tag = readU16(entry, order);
        if (tag == 0x0201) {
            previewOffset = tiffInlineValue(entry, order);
        }
        else if (tag == 0x0202) {
            previewLength = tiffInlineValue(entry, order);
        }
    }
    const qsizetype offset = static_cast<qsizetype>(previewOffset);
    const qsizetype length = static_cast<qsizetype>(previewLength);
    if (previewOffset == 0 || previewLength == 0
        || !boundedSpan(offset, length, tiff.size())) {
        result.diagnostic = QStringLiteral("Exif thumbnail range is invalid");
        return result;
    }
    const QByteArray preview = tiff.mid(offset, length);
    if (preview.size() < 2
        || static_cast<uchar>(preview[0]) != JpegMarkerPrefix
        || static_cast<uchar>(preview[1]) != JpegStartOfImage) {
        result.diagnostic = QStringLiteral("Exif thumbnail is not JPEG");
        return result;
    }
    result.bytes = preview;
    return result;
}

struct JpegScanResult {
    enum class State { NeedMore, Found, TerminalMiss };
    State state = State::NeedMore;
    QByteArray preview;
    QSize sourceSize;
    int orientation = 1;
    QString diagnostic;
};

JpegScanResult scanJpegHeader(const QByteArray &header, bool noMoreBytes)
{
    JpegScanResult result;
    if (header.size() < 2) {
        if (noMoreBytes) {
            result.state = JpegScanResult::State::TerminalMiss;
            result.diagnostic = QStringLiteral("JPEG header is truncated");
        }
        return result;
    }
    if (static_cast<uchar>(header[0]) != JpegMarkerPrefix
        || static_cast<uchar>(header[1]) != JpegStartOfImage) {
        result.state = JpegScanResult::State::TerminalMiss;
        result.diagnostic = QStringLiteral("source is not JPEG");
        return result;
    }

    qsizetype cursor = 2;
    QString lastExifDiagnostic;
    while (true) {
        if (cursor >= header.size()) {
            if (!result.preview.isEmpty()) {
                result.state = JpegScanResult::State::Found;
                return result;
            }
            if (noMoreBytes) {
                result.state = JpegScanResult::State::TerminalMiss;
                result.diagnostic = lastExifDiagnostic.isEmpty()
                    ? QStringLiteral("JPEG has no embedded preview")
                    : lastExifDiagnostic;
            }
            return result;
        }
        if (static_cast<uchar>(header[cursor]) != JpegMarkerPrefix) {
            if (!result.preview.isEmpty()) {
                result.state = JpegScanResult::State::Found;
                return result;
            }
            result.state = JpegScanResult::State::TerminalMiss;
            result.diagnostic = QStringLiteral("JPEG marker stream is malformed");
            return result;
        }
        while (cursor < header.size()
               && static_cast<uchar>(header[cursor]) == JpegMarkerPrefix) {
            ++cursor;
        }
        if (cursor >= header.size()) {
            if (!result.preview.isEmpty()) {
                result.state = JpegScanResult::State::Found;
                return result;
            }
            if (noMoreBytes) {
                result.state = JpegScanResult::State::TerminalMiss;
                result.diagnostic = QStringLiteral("JPEG marker is truncated");
            }
            return result;
        }
        const uchar marker = static_cast<uchar>(header[cursor++]);
        if (marker == JpegStartOfImage || marker == 0x01
            || (marker >= 0xd0 && marker <= 0xd9)) {
            continue;
        }
        if (marker == JpegStartOfScan) {
            if (!result.preview.isEmpty()) {
                result.state = JpegScanResult::State::Found;
                return result;
            }
            result.state = JpegScanResult::State::TerminalMiss;
            result.diagnostic = lastExifDiagnostic.isEmpty()
                ? QStringLiteral("JPEG has no embedded preview before SOS")
                : lastExifDiagnostic;
            return result;
        }
        if (cursor + 2 > header.size()) {
            if (!result.preview.isEmpty()) {
                result.state = JpegScanResult::State::Found;
                return result;
            }
            if (noMoreBytes) {
                result.state = JpegScanResult::State::TerminalMiss;
                result.diagnostic = QStringLiteral("JPEG segment length is truncated");
            }
            return result;
        }
        const auto *lengthBytes = reinterpret_cast<const uchar *>(
            header.constData() + cursor);
        const quint16 segmentLength = qFromBigEndian<quint16>(lengthBytes);
        if (segmentLength < 2) {
            if (!result.preview.isEmpty()) {
                result.state = JpegScanResult::State::Found;
                return result;
            }
            result.state = JpegScanResult::State::TerminalMiss;
            result.diagnostic = QStringLiteral("JPEG segment length is invalid");
            return result;
        }
        if (!boundedSpan(cursor, segmentLength, header.size())) {
            if (!result.preview.isEmpty()) {
                result.state = JpegScanResult::State::Found;
                return result;
            }
            if (noMoreBytes) {
                result.state = JpegScanResult::State::TerminalMiss;
                result.diagnostic = QStringLiteral("JPEG segment is truncated");
            }
            return result;
        }
        const QByteArray payload = header.mid(cursor + 2, segmentLength - 2);

        const bool isStartOfFrame = marker >= 0xc0 && marker <= 0xcf
            && marker != 0xc4 && marker != 0xc8 && marker != 0xcc;
        if (isStartOfFrame && payload.size() >= 5) {
            const auto *sof = reinterpret_cast<const uchar *>(payload.constData());
            const int height = qFromBigEndian<quint16>(sof + 1);
            const int width = qFromBigEndian<quint16>(sof + 3);
            if (width > 0 && height > 0) {
                result.sourceSize = QSize(width, height);
            }
        }
        if (marker == JpegApp1) {
            const ExifPreview exif = parseExifPreview(payload);
            if (!exif.bytes.isEmpty()) {
                result.preview = exif.bytes;
                result.orientation = exif.orientation;
            }
            if (payload.startsWith(QByteArray("Exif\0\0", 6))) {
                lastExifDiagnostic = exif.diagnostic;
            }
        }
        cursor += segmentLength;
    }
}

bool isJpegSource(const QString &sourceName, QString mimeType)
{
    mimeType = mimeType.section(QLatin1Char(';'), 0, 0).trimmed().toLower();
    if (!mimeType.isEmpty()) {
        return mimeType == QStringLiteral("image/jpeg") ||
            mimeType == QStringLiteral("image/jpg") ||
            mimeType == QStringLiteral("image/pjpeg");
    }
    const qsizetype separator = sourceName.lastIndexOf(QLatin1Char('.'));
    const QString suffix = separator >= 0
        ? sourceName.mid(separator + 1).toLower() : QString();
    return suffix == QStringLiteral("jpg") || suffix == QStringLiteral("jpeg")
        || suffix == QStringLiteral("jpe") || suffix == QStringLiteral("jfif");
}

} // namespace

EmbeddedProbeResult probeEmbeddedImage(
    const QString &sourceName, const EmbeddedRangeReader &reader,
    const EmbeddedProbeLimits &requestedLimits,
    const QString &sourceMimeType)
{
    EmbeddedProbeResult result;
    if (!isJpegSource(sourceName, sourceMimeType)) {
        result.outcome = EmbeddedProbeResult::Outcome::Unsupported;
        result.diagnostic = QStringLiteral("format has no bounded embedded probe");
        return result;
    }
    if (!reader) {
        result.outcome = EmbeddedProbeResult::Outcome::ReadFailed;
        result.diagnostic = QStringLiteral("source has no range reader");
        return result;
    }

    EmbeddedProbeLimits limits = requestedLimits;
    limits.initialRangeBytes = std::clamp<qsizetype>(
        limits.initialRangeBytes, 1, 64 * 1024);
    limits.maximumHeaderBytes = std::max(
        limits.initialRangeBytes, limits.maximumHeaderBytes);
    limits.maximumRangeRequests = std::max(1, limits.maximumRangeRequests);

    QByteArray header;
    bool endOfFile = false;
    while (header.size() < limits.maximumHeaderBytes
           && result.rangeRequests < limits.maximumRangeRequests) {
        const qsizetype remaining = limits.maximumHeaderBytes - header.size();
        const qsizetype requested = std::min(limits.initialRangeBytes, remaining);
        const EmbeddedRangeResult range = reader(header.size(), requested);
        ++result.rangeRequests;
        if (!range.succeeded()) {
            result.outcome = EmbeddedProbeResult::Outcome::ReadFailed;
            result.diagnostic = range.error;
            return result;
        }
        if (range.bytes.size() > requested) {
            result.outcome = EmbeddedProbeResult::Outcome::ReadFailed;
            result.diagnostic = QStringLiteral("range source returned too many bytes");
            return result;
        }
        result.sourceBytesRead += range.bytes.size();
        header += range.bytes;
        endOfFile = range.endOfFile || range.bytes.size() < requested;

        const bool exhausted = endOfFile
            || header.size() >= limits.maximumHeaderBytes
            || result.rangeRequests >= limits.maximumRangeRequests;
        const JpegScanResult scan = scanJpegHeader(header, exhausted);
        if (scan.state == JpegScanResult::State::Found) {
            result.outcome = EmbeddedProbeResult::Outcome::Found;
            result.encodedPreview = scan.preview;
            result.previewMimeType = QStringLiteral("image/jpeg");
            result.previewSize = jpegDimensions(scan.preview);
            result.sourceSize = scan.sourceSize;
            result.orientation = scan.orientation;
            return result;
        }
        if (scan.state == JpegScanResult::State::TerminalMiss) {
            result.outcome = EmbeddedProbeResult::Outcome::NotFound;
            result.sourceSize = scan.sourceSize;
            result.orientation = scan.orientation;
            result.diagnostic = scan.diagnostic;
            return result;
        }
        if (range.bytes.isEmpty()) {
            endOfFile = true;
        }
        if (endOfFile) {
            break;
        }
    }

    result.outcome = EmbeddedProbeResult::Outcome::NotFound;
    result.diagnostic = QStringLiteral("embedded probe reached its byte/request limit");
    return result;
}

} // namespace ZoinGallery
