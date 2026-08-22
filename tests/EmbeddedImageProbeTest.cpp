#include "src/embed/EmbeddedImageProbe.h"
#include "Runners/ImageProbeRunner.h"

#include <QBuffer>
#include <QImageReader>
#include <QtTest>

namespace {

void appendU16(QByteArray &bytes, quint16 value, bool littleEndian)
{
    if (littleEndian) {
        bytes.append(static_cast<char>(value & 0xff));
        bytes.append(static_cast<char>((value >> 8) & 0xff));
    }
    else {
        bytes.append(static_cast<char>((value >> 8) & 0xff));
        bytes.append(static_cast<char>(value & 0xff));
    }
}

void appendU32(QByteArray &bytes, quint32 value, bool littleEndian)
{
    if (littleEndian) {
        appendU16(bytes, static_cast<quint16>(value & 0xffff), true);
        appendU16(bytes, static_cast<quint16>(value >> 16), true);
    }
    else {
        appendU16(bytes, static_cast<quint16>(value >> 16), false);
        appendU16(bytes, static_cast<quint16>(value & 0xffff), false);
    }
}

QByteArray appSegment(uchar marker, const QByteArray &payload);

QByteArray jpegImage(const QSize &size, const QColor &color)
{
    Q_UNUSED(color)
    // A deterministic marker stream keeps this parser test independent from
    // the deployment's optional qjpeg image plugin. The probe only needs SOI
    // and SOF dimensions; pixel decoding belongs to ImageProbeRunner tests.
    QByteArray payload;
    payload.append(char(8)); // precision
    payload.append(static_cast<char>((size.height() >> 8) & 0xff));
    payload.append(static_cast<char>(size.height() & 0xff));
    payload.append(static_cast<char>((size.width() >> 8) & 0xff));
    payload.append(static_cast<char>(size.width() & 0xff));
    payload.append(char(1)); // components
    payload.append(char(1));
    payload.append(char(0x11));
    payload.append(char(0));

    QByteArray encoded;
    encoded.append(char(0xff));
    encoded.append(char(0xd8));
    encoded += appSegment(0xc0, payload);
    encoded.append(char(0xff));
    encoded.append(char(0xd9));
    return encoded;
}

QByteArray appSegment(uchar marker, const QByteArray &payload)
{
    QByteArray segment;
    segment.append(char(0xff));
    segment.append(static_cast<char>(marker));
    const quint16 length = static_cast<quint16>(payload.size() + 2);
    segment.append(static_cast<char>((length >> 8) & 0xff));
    segment.append(static_cast<char>(length & 0xff));
    segment += payload;
    return segment;
}

QByteArray exifPayload(const QByteArray &thumbnail, int orientation,
                       bool littleEndian = true, quint32 forcedOffset = 0)
{
    QByteArray tiff;
    tiff += littleEndian ? QByteArrayLiteral("II") : QByteArrayLiteral("MM");
    appendU16(tiff, 42, littleEndian);
    appendU32(tiff, 8, littleEndian);

    // IFD0 contains orientation and points to IFD1.
    appendU16(tiff, 1, littleEndian);
    appendU16(tiff, 0x0112, littleEndian);
    appendU16(tiff, 3, littleEndian);
    appendU32(tiff, 1, littleEndian);
    appendU16(tiff, static_cast<quint16>(orientation), littleEndian);
    appendU16(tiff, 0, littleEndian);
    constexpr quint32 Ifd1Offset = 26;
    appendU32(tiff, Ifd1Offset, littleEndian);

    appendU16(tiff, 2, littleEndian);
    appendU16(tiff, 0x0201, littleEndian);
    appendU16(tiff, 4, littleEndian);
    appendU32(tiff, 1, littleEndian);
    constexpr quint32 ThumbnailOffset = 56;
    appendU32(tiff, forcedOffset ? forcedOffset : ThumbnailOffset,
              littleEndian);
    appendU16(tiff, 0x0202, littleEndian);
    appendU16(tiff, 4, littleEndian);
    appendU32(tiff, 1, littleEndian);
    appendU32(tiff, static_cast<quint32>(thumbnail.size()), littleEndian);
    appendU32(tiff, 0, littleEndian);
    if (tiff.size() != ThumbnailOffset) {
        return {};
    }
    tiff += thumbnail;
    return QByteArray("Exif\0\0", 6) + tiff;
}

QByteArray jpegWithExif(const QByteArray &outer, const QByteArray &thumbnail,
                        int orientation, bool xmpFirst = false,
                        bool littleEndian = true, quint32 forcedOffset = 0)
{
    QByteArray result;
    result.append(char(0xff));
    result.append(char(0xd8));
    if (xmpFirst) {
        result += appSegment(0xe1, QByteArrayLiteral(
            "http://ns.adobe.com/xap/1.0/\0<xmp/>") );
    }
    result += appSegment(0xe1, exifPayload(
        thumbnail, orientation, littleEndian, forcedOffset));
    result += outer.mid(2);
    return result;
}

ZoinGallery::EmbeddedRangeReader memoryReader(
    const QByteArray &source, int *reads = nullptr, qint64 *bytes = nullptr)
{
    return [source, reads, bytes](qint64 offset, qsizetype length) {
        if (reads) {
            ++*reads;
        }
        ZoinGallery::EmbeddedRangeResult result;
        if (offset < 0 || offset > source.size()) {
            result.error = QStringLiteral("invalid offset");
            return result;
        }
        result.bytes = source.mid(static_cast<qsizetype>(offset), length);
        result.endOfFile = offset + result.bytes.size() >= source.size();
        if (bytes) {
            *bytes += result.bytes.size();
        }
        return result;
    };
}

quint32 pngCrc32(const char *data, qsizetype size)
{
    quint32 crc = 0xffffffffU;
    for (qsizetype index = 0; index < size; ++index) {
        crc ^= static_cast<uchar>(data[index]);
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^
                (0xedb88320U & static_cast<quint32>(
                    -static_cast<qint32>(crc & 1U)));
        }
    }
    return crc ^ 0xffffffffU;
}

QByteArray pngWithDeclaredDimensions(const QSize &declaredSize)
{
    QImage seed(2, 2, QImage::Format_RGB32);
    seed.fill(Qt::blue);
    QByteArray encoded;
    QBuffer output(&encoded);
    output.open(QIODevice::WriteOnly);
    if (!seed.save(&output, "PNG") || encoded.size() < 33 ||
        encoded.mid(12, 4) != QByteArrayLiteral("IHDR")) {
        return {};
    }

    const auto writeBigEndian = [&encoded](qsizetype offset, quint32 value) {
        encoded[offset] = static_cast<char>((value >> 24) & 0xff);
        encoded[offset + 1] = static_cast<char>((value >> 16) & 0xff);
        encoded[offset + 2] = static_cast<char>((value >> 8) & 0xff);
        encoded[offset + 3] = static_cast<char>(value & 0xff);
    };
    writeBigEndian(16, static_cast<quint32>(declaredSize.width()));
    writeBigEndian(20, static_cast<quint32>(declaredSize.height()));
    writeBigEndian(29, pngCrc32(encoded.constData() + 12, 17));
    return encoded;
}

class ProviderPreview final : public ZoinGallery::ImageSourceProvider {
public:
    explicit ProviderPreview(QByteArray encoded)
        : _encoded(std::move(encoded)) {}

    ZoinGallery::ImageSourceReadResult readRange(
        const ZoinGallery::ImageSourceDescriptor &, qint64, qint64,
        const QSharedPointer<ZoinGallery::ImageSourceCancellation> &)
        override {
        return {.errorString = QStringLiteral("unexpected range read")};
    }

    QSharedPointer<ZoinGallery::ImageSourceLease> materialize(
        const ZoinGallery::ImageSourceDescriptor &,
        const QSharedPointer<ZoinGallery::ImageSourceCancellation> &)
        override {
        return {};
    }

    ZoinGallery::ImageSourceProbeResult probeEmbedded(
        const ZoinGallery::ImageSourceDescriptor &,
        const QSharedPointer<ZoinGallery::ImageSourceCancellation> &)
        override {
        return {
            .status = ZoinGallery::ImageSourceProbeStatus::Found,
            .encodedData = _encoded,
            .mimeType = QStringLiteral("image/png"),
        };
    }

private:
    QByteArray _encoded;
};

} // namespace

class EmbeddedImageProbeTest final : public QObject {
    Q_OBJECT

private slots:
    void acceptsTinyExifThumbnail()
    {
        const QByteArray thumbnail = jpegImage(QSize(8, 6), Qt::red);
        const QByteArray source = jpegWithExif(
            jpegImage(QSize(640, 480), Qt::blue), thumbnail, 6, true);
        QVERIFY(!thumbnail.isEmpty());
        QVERIFY(!source.isEmpty());

        int reads = 0;
        qint64 bytes = 0;
        const auto result = ZoinGallery::probeEmbeddedImage(
            QStringLiteral("remote.JPG"), memoryReader(source, &reads, &bytes));

        QVERIFY(result.found());
        QCOMPARE(result.encodedPreview, thumbnail);
        QCOMPARE(result.previewMimeType, QStringLiteral("image/jpeg"));
        QCOMPARE(result.previewSize, QSize(8, 6));
        QCOMPARE(result.sourceSize, QSize(640, 480));
        QCOMPARE(result.orientation, 6);
        QCOMPARE(result.rangeRequests, reads);
        QCOMPARE(result.sourceBytesRead, bytes);
        QVERIFY(result.sourceBytesRead <= 32 * 1024);
    }

    void supportsBigEndianExif()
    {
        const QByteArray thumbnail = jpegImage(QSize(5, 4), Qt::green);
        const QByteArray source = jpegWithExif(
            jpegImage(QSize(80, 60), Qt::black), thumbnail, 8, false, false);
        const auto result = ZoinGallery::probeEmbeddedImage(
            QStringLiteral("camera.jpeg"), memoryReader(source));
        QVERIFY(result.found());
        QCOMPARE(result.previewSize, QSize(5, 4));
        QCOMPARE(result.orientation, 8);
    }

    void authoritativeMimeFindsExtensionlessJpeg()
    {
        const QByteArray thumbnail = jpegImage(QSize(7, 5), Qt::red);
        const QByteArray source = jpegWithExif(
            jpegImage(QSize(96, 64), Qt::blue), thumbnail, 1);
        int reads = 0;
        const auto result = ZoinGallery::probeEmbeddedImage(
            QStringLiteral("opaque-resource"),
            memoryReader(source, &reads),
            ZoinGallery::EmbeddedProbeLimits{},
            QStringLiteral("image/jpeg; charset=binary"));

        QVERIFY(result.found());
        QCOMPARE(result.encodedPreview, thumbnail);
        QCOMPARE(result.sourceSize, QSize(96, 64));
        QVERIFY(reads > 0);
    }

    void malformedThumbnailIsTerminalMiss()
    {
        const QByteArray thumbnail = jpegImage(QSize(8, 6), Qt::yellow);
        const QByteArray source = jpegWithExif(
            jpegImage(QSize(64, 48), Qt::cyan), thumbnail, 1, false, true,
            0x7fffffffU);
        const auto result = ZoinGallery::probeEmbeddedImage(
            QStringLiteral("broken.jpg"), memoryReader(source));
        QCOMPARE(result.outcome,
                 ZoinGallery::EmbeddedProbeResult::Outcome::NotFound);
        QVERIFY(!result.diagnostic.isEmpty());
    }

    void respectsRangeAndByteCaps()
    {
        QByteArray source;
        source.append(char(0xff));
        source.append(char(0xd8));
        const QByteArray padding(60000, 'p');
        source += appSegment(0xe2, padding);
        source += appSegment(0xe2, padding);
        source.append(char(0xff));
        source.append(char(0xda));

        ZoinGallery::EmbeddedProbeLimits limits;
        limits.initialRangeBytes = 16 * 1024;
        limits.maximumHeaderBytes = 32 * 1024;
        limits.maximumRangeRequests = 2;
        int reads = 0;
        qint64 bytes = 0;
        const auto result = ZoinGallery::probeEmbeddedImage(
            QStringLiteral("large.jpg"), memoryReader(source, &reads, &bytes),
            limits);

        QCOMPARE(result.outcome,
                 ZoinGallery::EmbeddedProbeResult::Outcome::NotFound);
        QCOMPARE(reads, 2);
        QCOMPARE(bytes, qint64(32 * 1024));
    }

    void unsupportedFormatDoesNotRead()
    {
        int reads = 0;
        const auto result = ZoinGallery::probeEmbeddedImage(
            QStringLiteral("image.png"), memoryReader(QByteArray(), &reads));
        QCOMPARE(result.outcome,
                 ZoinGallery::EmbeddedProbeResult::Outcome::Unsupported);
        QCOMPARE(reads, 0);
    }

    void readFailureIsNotNegativeProbe()
    {
        const auto result = ZoinGallery::probeEmbeddedImage(
            QStringLiteral("offline.jpg"), [](qint64, qsizetype) {
                ZoinGallery::EmbeddedRangeResult range;
                range.error = QStringLiteral("network unavailable");
                return range;
            });
        QCOMPARE(result.outcome,
                 ZoinGallery::EmbeddedProbeResult::Outcome::ReadFailed);
        QCOMPARE(result.diagnostic, QStringLiteral("network unavailable"));
    }

    void rejectsProviderPreviewWithBombDimensionsBeforeDecode()
    {
        const QSize declaredSize(20000, 20000);
        const QByteArray encoded = pngWithDeclaredDimensions(declaredSize);
        if (encoded.isEmpty()) {
            QSKIP("PNG image plugin is unavailable");
        }

        QByteArray headerBytes = encoded;
        QBuffer header(&headerBytes);
        header.open(QIODevice::ReadOnly);
        QImageReader reader(&header, "PNG");
        if (reader.size() != declaredSize) {
            QSKIP("PNG image plugin cannot inspect the bomb fixture");
        }

        auto provider = QSharedPointer<ProviderPreview>::create(encoded);
        ZoinGallery::ImageProbeRequest request;
        request.source.resourceId = QStringLiteral("resource:bomb");
        request.source.sourceKey = QStringLiteral("network/bomb");
        request.source.contentVersion = QStringLiteral("v1");
        request.source.displayName = QStringLiteral("bomb.png");
        request.requestNamespace = QStringLiteral("bomb-test");

        ImageProbeRunner runner(request, provider);
        ZoinGallery::ImageProbeResult result;
        bool delivered = false;
        connect(&runner, &ImageProbeRunner::imageProbeReady, this,
                [&result, &delivered](
                    const ZoinGallery::ImageProbeResult &value) {
                    result = value;
                    delivered = true;
                });
        runner.run();

        QVERIFY(delivered);
        QCOMPARE(result.status,
                 ZoinGallery::ImageSourceProbeStatus::NotFound);
        QVERIFY(result.preview.isNull());
        QCOMPARE(result.diagnostic,
                 QStringLiteral(
                     "embedded preview dimensions exceed the pixel budget"));
    }
};

QTEST_MAIN(EmbeddedImageProbeTest)
#include "EmbeddedImageProbeTest.moc"
