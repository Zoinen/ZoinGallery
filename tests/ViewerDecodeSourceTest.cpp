#include "ThumbnailLoader.h"
#include "Decoders/JpegDecoder.h"
#include "Decoders/PagedImageDocument.h"
#include "Decoders/RawDecoder.h"
#include "Decoders/WebpCodec.h"
#include "PersistentImageCache.h"
#include "Runners/ImageDecodeRunner.h"

#include <QBuffer>
#include <QColorSpace>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QPainter>
#include <QPdfWriter>
#include <QTemporaryDir>
#include <QtTest>
#include <QtGui/private/qtguiglobal_p.h>
#include <turbojpeg.h>

#include <cstring>

namespace {

QByteArray jpeg(QSize size, QRgb color) {
    QImage image(size, QImage::Format_RGB888);
    image.fill(color);
    tjhandle compressor = tjInitCompress();
    if (!compressor) return {};
    unsigned char *encoded = nullptr;
    unsigned long length = 0;
    const int result = tjCompress2(compressor, image.constBits(), image.width(),
        image.bytesPerLine(), image.height(), TJPF_RGB, &encoded, &length,
        TJSAMP_444, 95, 0);
    QByteArray bytes;
    if (result == 0) bytes = QByteArray(reinterpret_cast<const char *>(encoded), length);
    tjFree(encoded);
    tjDestroy(compressor);
    return bytes;
}

QByteArray detailedJpeg(QSize size) {
    QImage image(size, QImage::Format_RGB888);
    for (int y = 0; y < image.height(); ++y) {
        auto *line = image.scanLine(y);
        for (int x = 0; x < image.width(); ++x) {
            const int checker = ((x / 3) ^ (y / 5)) & 1;
            line[x * 3] = static_cast<uchar>((x * 37 + y * 11
                                              + checker * 91) & 0xff);
            line[x * 3 + 1] = static_cast<uchar>((x * 13 + y * 53
                                                  + checker * 47) & 0xff);
            line[x * 3 + 2] = static_cast<uchar>((x * 71 + y * 7
                                                  + checker * 29) & 0xff);
        }
    }
    tjhandle compressor = tjInitCompress();
    if (!compressor) return {};
    unsigned char *encoded = nullptr;
    unsigned long length = 0;
    const int result = tjCompress2(
        compressor, image.constBits(), image.width(), image.bytesPerLine(),
        image.height(), TJPF_RGB, &encoded, &length, TJSAMP_420, 82, 0);
    QByteArray bytes;
    if (result == 0) {
        bytes = QByteArray(reinterpret_cast<const char *>(encoded), length);
    }
    tjFree(encoded);
    tjDestroy(compressor);
    return bytes;
}

QImage turboDecode(const QByteArray &bytes, int flags) {
    tjhandle decompressor = tjInitDecompress();
    if (!decompressor) return {};
    int width = 0;
    int height = 0;
    int subsampling = 0;
    int colorSpace = 0;
    if (tjDecompressHeader3(
            decompressor,
            reinterpret_cast<const unsigned char *>(bytes.constData()),
            static_cast<unsigned long>(bytes.size()), &width, &height,
            &subsampling, &colorSpace) != 0) {
        tjDestroy(decompressor);
        return {};
    }
    QImage image(width, height, QImage::Format_RGB888);
    if (image.isNull() || tjDecompress2(
            decompressor,
            reinterpret_cast<const unsigned char *>(bytes.constData()),
            static_cast<unsigned long>(bytes.size()), image.bits(), width,
            image.bytesPerLine(), height, TJPF_RGB, flags) != 0) {
        image = {};
    }
    tjDestroy(decompressor);
    return image;
}

bool samePixels(const QImage &left, const QImage &right) {
    if (left.size() != right.size()) return false;
    const QImage leftRgb = left.convertToFormat(QImage::Format_RGB888);
    const QImage rightRgb = right.convertToFormat(QImage::Format_RGB888);
    for (int y = 0; y < leftRgb.height(); ++y) {
        if (memcmp(leftRgb.constScanLine(y), rightRgb.constScanLine(y),
                   static_cast<size_t>(leftRgb.width() * 3)) != 0) {
            return false;
        }
    }
    return true;
}

void attachPreview(ImageData &image, QByteArray bytes) {
    auto storage = std::make_shared<QByteArray>(std::move(bytes));
    image.previewData = std::shared_ptr<char>(storage, storage->data());
    image.previewDataSize = storage->size();
    image.previewMimeType = QStringLiteral("image/jpeg");
    image.previewUsed = QStringLiteral("test-preview");
}

class PreviewFixtureDecoder final : public ImageDecoderInterface {
public:
    explicit PreviewFixtureDecoder(bool nativeDecode) : _nativeDecode(nativeDecode) {}
    QStringList supportedFormats() override { return {suffix()}; }
    QString decoderName() const override { return QStringLiteral("viewer-preview-fixture"); }
    bool readMetadata(ImageInfo &) override { return false; }
    bool readPreviewAndMime(ImageData &data) override {
        if (!data.request.info.path.endsWith(suffix())) {
            return false;
        }
        attachPreview(data, jpeg({144, 96}, qRgb(20, 80, 190)));
        return true;
    }
    bool supportsNativeDecode() const override { return _nativeDecode; }
    QImage decode(const QString &, const QByteArray &, QSize) override { return {}; }

private:
    QString suffix() const {
        return _nativeDecode ? QStringLiteral(".preview-native")
                             : QStringLiteral(".preview-only");
    }
    bool _nativeDecode;
};

}

class ViewerDecodeSourceTest : public QObject {
    Q_OBJECT

private slots:
    void rgbPngThumbnailRemainsOpaque() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QString path = qEnvironmentVariable("ZG_PNG_PROBE");
        if (path.isEmpty()) {
            path = directory.filePath("opaque.png");
            QImage fixture(688, 992, QImage::Format_RGB888);
            fixture.fill(QColor(180, 110, 50));
            QVERIFY(fixture.save(path));
        }
        ImageDecodeRequest request;
        request.info.path = path;
        request.targetSize = QSize(256, 384);
        request.expandToCacheResolution = false;
        QVERIFY(ThumbnailLoader::readMetadata(request.info));
        ImageData data(request);
        QVERIFY(ThumbnailLoader::readImage(data));
        DecodedImageInfo decoded;
        const auto image = ThumbnailLoader::decode(data, decoded);
        QVERIFY(!image.isNull());
        const auto thumb = ThumbnailLoader::createThumbnail(image, data.request.targetSize);
        QVERIFY(!thumb.isNull());
        for (int y = 0; y < thumb.height(); ++y)
            for (int x = 0; x < thumb.width(); ++x)
                QCOMPARE(thumb.pixelColor(x, y).alpha(), 255);
    }
    void viewerReadsOriginalOnlyWhenNativeDecodeExists() {
        ImageDecoderFactory::registerClass(
            [] { return new PreviewFixtureDecoder(false); }, 1000);
        ImageDecoderFactory::registerClass(
            [] { return new PreviewFixtureDecoder(true); }, 1000);
        RawDecoder raw;
        QVERIFY(!raw.supportsNativeDecode());
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        for (bool nativeDecode : {false, true}) {
            const QString path = directory.filePath(nativeDecode
                ? QStringLiteral("fixture.preview-native")
                : QStringLiteral("fixture.preview-only"));
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            const QByteArray sourceBytes("original-byte-fixture");
            QCOMPARE(file.write(sourceBytes), sourceBytes.size());
            file.close();
            ImageDecodeRequest request;
            request.viewerRequest = true;
            request.checkCache = true;
            request.info.path = path;
            request.info.imageSize = QSize(144, 96);
            request.targetSize = QSize(72, 48);
            ImageData data(request);
            QVERIFY(ThumbnailLoader::readImage(data));
            QVERIFY(data.previewData);
            QCOMPARE(data.request.targetSize, request.targetSize);
            if (nativeDecode) {
                QCOMPARE(data.data, sourceBytes);
            } else {
                QVERIFY(data.data.isNull());
            }
            DecodedImageInfo decodedInfo;
            const QImage decoded = ThumbnailLoader::decode(data, decodedInfo);
            QCOMPARE(decoded.size(), QSize(144, 96));
            QVERIFY(!decodedInfo.previewUsed.isEmpty());
            QCOMPARE(decodedInfo.isAuthoritativePreview, !nativeDecode);
        }
    }

    void pagedTiffAndWebpDecodeEachFrame() {
        using namespace ZoinGallery;
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QByteArray tiff = QByteArray::fromBase64(
            "SUkqABgAAAB4nHukrPH/0TDGAGy7eaEACwAAAQMAAQAAAAgAAAABAQMAAQAAAAcAAAACAQMABAAAAKIA"
            "AAADAQMAAQAAAAgAAAAGAQMAAQAAAAIAAAARAQQAAQAAAAgAAAAVAQMAAQAAAAQAAAAWAQMAAQAAAAcA"
            "AAAXAQQAAQAAAA8AAAAcAQMAAQAAAAEAAABSAQMAAQAAAAIAAADIAAAACAAIAAgACAAAAAAAAABJSSoA"
            "GAAAAHic0zhr/F9jiGEAFPZg3AALAAABAwABAAAABQAAAAEBAwABAAAACQAAAAIBAwAEAAAAUgEAAAMB"
            "AwABAAAACAAAAAYBAwABAAAAAgAAABEBBAABAAAAuAAAABUBAwABAAAABAAAABYBAwABAAAACQAAABcB"
            "BAABAAAADwAAABwBAwABAAAAAQAAAFIBAwABAAAAAgAAAAAAAAAIAAgACAAIAAAAAAAAAA==");
        const QByteArray webp = QByteArray::fromBase64(
            "UklGRogAAABXRUJQVlA4WAoAAAACAAAABwAABgAAQU5JTQYAAAAAAAAAAQBBTk1GKgAAAAAAAAAAAAcA"
            "AAYAACwBAAJWUDhMEQAAAC8HgAEAB9CRihel/4GI6H8AAEFOTUYqAAAAAAAAAAAABwAABgAA9AEAAFZQ"
            "OEwRAAAALweAAQAHUJA+FLr/gYjofwAA");
        for (const auto &fixture : {
                 std::make_pair(QStringLiteral("pages.TIFF"), tiff),
                 std::make_pair(QStringLiteral("frames.WebP"), webp)}) {
            QFile file(directory.filePath(fixture.first));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(fixture.second), fixture.second.size());
            file.close();
            PageError error;
            auto document = PagedImageDocument::open(file.fileName(), fixture.first, error);
            QVERIFY(document);
            QCOMPARE(error, PageError::None);
            QCOMPARE(document->pageCount(), 2);
            const auto canceled = [] { return false; };
            const ImagePage second = document->render(1, {}, {}, canceled);
            const ImagePage first = document->render(0, {}, {}, canceled);
            QCOMPARE(first.pixels.pixelColor(0, 0), QColor(226, 35, 40));
            if (fixture.second == tiff) {
                QCOMPARE(second.size, QSizeF(5, 9));
                QCOMPARE(second.pixels.pixelColor(0, 0), QColor(40, 205, 51));
                QVERIFY(!document->animated());
            } else {
                QCOMPARE(second.size, QSizeF(8, 7));
                QCOMPARE(second.pixels.pixelColor(0, 0), QColor(15, 32, 208));
                QCOMPARE(first.delayMs, 300);
                QCOMPARE(second.delayMs, 500);
                QCOMPARE(document->repeats(), 0);
            }
        }
    }

    void pdfThumbnailRendersOnlyTheFirstPageAtRequestedSize() {
        using namespace ZoinGallery;
        if (!PagedImageDocument::isPdf(QStringLiteral("document.pdf")))
            QSKIP("Qt PDF is disabled in this build");
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("document.pdf"));
        {
            QPdfWriter writer(path);
            writer.setResolution(72);
            QPainter painter(&writer);
            QVERIFY(painter.isActive());
            painter.fillRect(painter.window(), Qt::red);
            QVERIFY(writer.newPage());
            painter.fillRect(painter.window(), Qt::blue);
        }
        ImageDecodeRequest request;
        request.info.path = path;
        request.targetSize = QSize(96, 64);
        QVERIFY(ThumbnailLoader::readMetadata(request.info));
        QCOMPARE(request.info.imageSize, QSize(595, 842));
        ImageData data(request);
        QVERIFY(ThumbnailLoader::readImage(data));
        QVERIFY(data.data.isEmpty());
        DecodedImageInfo info;
        const QImage raster = ThumbnailLoader::decode(data, info);
        QCOMPARE(raster.size(), QSize(45, 64));
        QCOMPARE(raster.pixelColor(22, 32), QColor(Qt::red));
    }

    void emptyJpegTargetMeansNativeResolution() {
        const QByteArray bytes = jpeg({257, 131}, qRgb(20, 80, 190));
        QVERIFY(!bytes.isEmpty());
        JpegDecoder decoder;
        QCOMPARE(decoder.decode(QStringLiteral("image/jpeg"), bytes, {}).size(),
                 QSize(257, 131));
    }

    void pagedIconReturnsEachSize() {
        using namespace ZoinGallery;
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QFile file(directory.filePath(QStringLiteral("sizes.ico")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray icon = QByteArray::fromBase64(
            "AAABAAIAEBAAAAAAIABeAAAAJgAAACAgAAAAACAAcAAAAIQAAACJUE5HDQoaCgAAAA1JSERSAAAA"
            "EAAAABAIBgAAAB/z/2EAAAAlSURBVHicY5RvvvufgQLARInmUQMggImBQsA0agDDaBgwUB4GAF3O"
            "Ap5JFKETAAAAAElFTkSuQmCCiVBORw0KGgoAAAANSUhEUgAAACAAAAAgCAYAAABzenr0AAAAN0lE"
            "QVR4nO3QQREAMAgDQYoRxKKzWspUBZ+Ngcvsqb4vFpebcQcIECBAgAABAgQIECBAgACBTzA+ngK+"
            "BE9jVgAAAABJRU5ErkJggg==");
        QCOMPARE(file.write(icon), icon.size());
        file.close();
        PageError error;
        auto document = PagedImageDocument::open(file.fileName(), file.fileName(), error);
        QVERIFY(document);
        QCOMPARE(error, PageError::None);
        QCOMPARE(document->pageCount(), 2);
        QVERIFY(!document->animated());
        QCOMPARE(document->pageSize(1), QSizeF(32, 32));
        QCOMPARE(document->pageSize(0), QSizeF(16, 16));
        QCOMPARE(document->render(1, {}, {}, [] { return false; }).pixels.size(), QSize(32, 32));
        QCOMPARE(document->render(0, {}, {}, [] { return false; }).pixels.size(), QSize(16, 16));
    }

    void timedAvifSequenceRewinds() {
        using namespace ZoinGallery;
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QFile file(directory.filePath(QStringLiteral("frames.avifs")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray avif = QByteArray::fromBase64(
            "AAAALGZ0eXBhdmlzAAAAAGF2aWZhdmlzbXNmMWlzbzhtaWYxbWlhZk1BMUEAAADrbWV0YQAAAAAA"
            "AAAhaGRscgAAAAAAAAAAcGljdAAAAAAAAAAAAAAAAAAAAAAOcGl0bQAAAAAAAQAAAB5pbG9jAAAA"
            "AEQAAAEAAQAAAAEAAAPtAAAAJAAAAChpaW5mAAAAAAABAAAAGmluZmUCAAAAAAEAAGF2MDFDb2xv"
            "cgAAAABqaXBycAAAAEtpcGNvAAAAFGlzcGUAAAAAAAAAEAAAABAAAAAQcGl4aQAAAAADCAgIAAAA"
            "DGF2MUOBIAAAAAAAE2NvbHJuY2x4AAEADQAGgAAAABdpcG1hAAAAAAAAAAEAAQQBAoMEAAACzm1v"
            "b3YAAAB4bXZoZAEAAAAAAAAA5ykeEQAAAADnKR4RAAAD6P//////////AAEAAAEAAAAAAAAAAAAA"
            "AAABAAAAAAAAAAAAAAAAAAAAAQAAAAAAAAAAAAAAAAAAQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
            "AAAAAAAAAAEAAAJOdHJhawAAAGh0a2hkAQAAAQAAAADnKR4RAAAAAOcpHhEAAAABAAAAAP//////"
            "////AAAAAAAAAAAAAAAAAAAAAAABAAAAAAAAAAAAAAAAAAAAAQAAAAAAAAAAAAAAAAAAQAAAAAAQ"
            "AAAAEAAAAAAALGVkdHMAAAAkZWxzdAEAAAEAAAABAAAAAAAAAhIAAAAAAAAAAAABAAAAAAGybWRp"
            "YQAAACxtZGhkAQAAAAAAAADnKR4RAAAAAOcpHhEAAAPoAAAAAAAAAhJVxAAAAAAAIWhkbHIAAAAA"
            "AAAAAHBpY3QAAAAAAAAAAAAAAAAAAAABXW1pbmYAAAAUdm1oZAAAAAEAAAAAAAAAAAAAACRkaW5m"
            "AAAAHGRyZWYAAAAAAAAAAQAAAAx1cmwgAAAAAQAAAR1zdGJsAAAAlXN0c2QAAAAAAAAAAQAAAIVh"
            "djAxAAAAAAAAAAEAAAAAAAAAAAAAAAAAAAAAABAAEABIAAAASAAAAAAAAAABCkFPTSBDb2RpbmcA"
            "AAAAAAAAAAAAAAAAAAAAAAAAAAAAGP//AAAADGF2MUOBIAAAAAAAE2NvbHJuY2x4AAEADQAGgAAA"
            "ABBjY3N0AAAAAHwAAAAAAAAgc3R0cwAAAAAAAAACAAAAAQAAALQAAAABAAABXgAAABxzdHNjAAAA"
            "AAAAAAEAAAABAAAAAgAAAAEAAAAcc3RzegAAAAAAAAAAAAAAAgAAACQAAAAgAAAAFHN0Y28AAAAA"
            "AAAAAQAAA+0AAAAUc3RzcwAAAAAAAAABAAAAAQAAAExtZGF0EgAKCSAAAAGf+bXyJDIVEACAAAAU"
            "uboKM00UXyTHSo5Ou3YwEgAyHDADwIAAAHoABAAAAKzNHn7zZTHwN2Wqmuq1RYA=");
        QCOMPARE(file.write(avif), avif.size());
        file.close();
        PageError error;
        auto document = PagedImageDocument::open(file.fileName(), file.fileName(), error);
        QVERIFY(document);
        QCOMPARE(error, PageError::None);
        QVERIFY(document->animated());
        const auto canceled = [] { return false; };
        const auto blue = document->render(1, {}, {}, canceled);
        QCOMPARE(blue.error, PageError::None);
        QCOMPARE(blue.delayMs, 350);
        QVERIFY(blue.pixels.pixelColor(0, 0).blue() > 240);
        QCOMPARE(document->render(2, {}, {}, canceled).error, PageError::EndOfSequence);
        QCOMPARE(document->pageCount(), 2);
        const auto red = document->render(0, {}, {}, canceled);
        QCOMPARE(red.error, PageError::None);
        QCOMPARE(red.delayMs, 180);
        QVERIFY(red.pixels.pixelColor(0, 0).red() > 240);
    }

    void jpegDecodeUsesAccurateInverseDct() {
        const QByteArray bytes = detailedJpeg({257, 131});
        QVERIFY(!bytes.isEmpty());
        const QImage accurate = turboDecode(bytes, TJFLAG_ACCURATEDCT);
        const QImage fast = turboDecode(bytes, TJFLAG_FASTDCT);
        QVERIFY(!accurate.isNull());
        QVERIFY(!fast.isNull());
        QVERIFY2(!samePixels(accurate, fast),
                 "The fixture must distinguish fast and accurate IDCT");

        JpegDecoder decoder;
        const QImage actual = decoder.decode(
            QStringLiteral("image/jpeg"), bytes, {});
        QVERIFY(!actual.isNull());
        QVERIFY2(samePixels(actual, accurate),
                 "Native viewer JPEGs must not use TurboJPEG's approximate IDCT");
    }

    void viewerKeepsNativePixelsAndRequestTier() {
        ImageDecodeRequest request;
        request.viewerRequest = true;
        request.targetSize = QSize(37, 19);
        request.expandToCacheResolution = false;
        ImageData data(request);
        data.data = jpeg({256, 128}, qRgb(20, 80, 190));
        data.mimeType = QStringLiteral("image/jpeg");
        attachPreview(data, jpeg({64, 32}, qRgb(200, 0, 0)));
        DecodedImageInfo info;
        const QImage native = ThumbnailLoader::decode(data, info);
        QCOMPARE(native.size(), QSize(256, 128));
        QCOMPARE(native.format(), QImage::Format_RGB888);
        QCOMPARE(data.request.targetSize, request.targetSize);
        QVERIFY(info.previewUsed.isEmpty());
        QVERIFY(qBlue(native.pixel(0, 0)) > qRed(native.pixel(0, 0)));
        QCOMPARE(ThumbnailLoader::createViewerImage(native, request.targetSize).size(),
                 request.targetSize);

        request.viewerRequest = false;
        ImageData thumbnail(request);
        thumbnail.data = data.data;
        thumbnail.mimeType = data.mimeType;
        const QImage scaled = ThumbnailLoader::decode(thumbnail, info);
        QCOMPARE(scaled.format(), QImage::Format_RGB888);
        QVERIFY(scaled.width() < native.width());
        QVERIFY(scaled.height() < native.height());
    }

    void thumbnailJpegDecodesToRgb32AndPreservesPixels() {
        if (!QT_CONFIG(qtgui_threadpool))
            QSKIP("This Qt build uses the painter for moderate RGB888 reductions");

        const QByteArray bytes = detailedJpeg({2601, 1735});
        QVERIFY(!bytes.isEmpty());
        ImageDecodeRequest request;
        request.targetSize = QSize(1024, 684);
        request.checkCache = true;
        request.storeInPersistentCache = false;

        JpegDecoder decoder;
        const QImage rgb888 = decoder.decode(
            QStringLiteral("image/jpeg"), bytes, request.targetSize);
        QCOMPARE(rgb888.format(), QImage::Format_RGB888);
        QVERIFY(rgb888.width() > request.targetSize.width());
        QVERIFY(rgb888.width() * rgb888.height() >= (1 << 20));
        const QImage expected = ThumbnailLoader::createThumbnail(
            ThumbnailLoader::rotateAndFlip(rgb888, ExifOrientation::Rotate90CW),
            request.targetSize.transposed());

        // Exercise the embedded JPEG route with orientation applied before
        // resizing, as it is for RAW previews.
        request.info.orientation = ExifOrientation::Rotate90CW;
        request.targetSize.transpose();
        ImageData data(request);
        attachPreview(data, bytes);
        DecodedImageInfo info;
        const QImage decoded = ThumbnailLoader::decode(data, info);
        QCOMPARE(info.decoderUsed, QStringLiteral("JpegDecoder"));
        QCOMPARE(decoded.format(), QImage::Format_RGB32);
        QCOMPARE(decoded.colorSpace(), rgb888.colorSpace());
        QVERIFY(samePixels(decoded, ThumbnailLoader::rotateAndFlip(
            rgb888, ExifOrientation::Rotate90CW)));

        ImageDecodeRunner runner(data);
        QImage actual;
        connect(&runner, &ImageDecodeRunner::imageReady, this,
                [&](const ImageDecodeRequest &, const QImage &image,
                    const DecodedImageInfo &) { actual = image; });
        runner.run();
        QCOMPARE(actual.size(), expected.size());
        QVERIFY(samePixels(actual, expected));
    }

    void jpegUsesRgb32WhenOnlyTheCacheNeedsResize() {
        if (!QT_CONFIG(qtgui_threadpool))
            QSKIP("This Qt build uses the painter for moderate RGB888 reductions");

        const QSize size(1301, 869);
        const QByteArray bytes = detailedJpeg(size);
        QVERIFY(!bytes.isEmpty());
        ImageDecodeRequest request;
        request.targetSize = size;
        request.expandToCacheResolution = false;
        request.storeInPersistentCache = true;
        ImageData data(request);
        data.data = bytes;
        data.mimeType = QStringLiteral("image/jpeg");
        DecodedImageInfo info;
        const QImage decoded = ThumbnailLoader::decode(data, info);
        QCOMPARE(decoded.size(), size);
        QCOMPARE(decoded.format(), QImage::Format_RGB32);

        request.storeInPersistentCache = false;
        ImageData uncached(request);
        uncached.data = bytes;
        uncached.mimeType = data.mimeType;
        const QImage baseline = ThumbnailLoader::decode(uncached, info);
        QCOMPARE(baseline.format(), QImage::Format_RGB888);
        const QByteArray prepared = PersistentImageCache::createImageForCache(decoded);
        QVERIFY(!prepared.isEmpty());
        QCOMPARE(prepared, PersistentImageCache::createImageForCache(baseline));

        request.storeInPersistentCache = true;
        request.info.source.resourceId = QStringLiteral("jpeg-resource");
        request.info.source.sourceKey = QStringLiteral("jpeg-source");
        ImageData external(request);
        external.data = bytes;
        external.mimeType = data.mimeType;
        const QImage derived = ThumbnailLoader::decode(external, info);
        QCOMPARE(derived.format(), QImage::Format_RGB32);
        QVERIFY(samePixels(derived, baseline));
    }

    void cachedWebpCanDecodeDirectlyToRgb32BeforeResizing() {
        if (!QT_CONFIG(qtgui_threadpool))
            QSKIP("This Qt build uses the painter for moderate RGB888 reductions");

        const QByteArray jpegBytes = detailedJpeg({1401, 921});
        JpegDecoder decoder;
        const QImage source = decoder.decode(QStringLiteral("image/jpeg"), jpegBytes, {});
        QVERIFY(!source.isNull());
        for (bool lossless : {false, true}) {
            const QByteArray encoded = WebpCodec::encode(source, 75, lossless);
            QVERIFY(!encoded.isEmpty());
            const QImage baseline = WebpCodec::decode(encoded);
            QCOMPARE(baseline.format(), QImage::Format_RGB888);
            const QSize bounds(777, 500);
            const QImage native = WebpCodec::decodeForThumbnailResize(encoded, bounds);
            QCOMPARE(native.format(), QImage::Format_RGB32);
            QCOMPARE(native.size(), source.size());
            QVERIFY(samePixels(native, baseline));
            QVERIFY(samePixels(
                ThumbnailLoader::createThumbnail(native, bounds, Qt::IgnoreAspectRatio),
                ThumbnailLoader::createThumbnail(baseline, bounds, Qt::IgnoreAspectRatio)));
            QCOMPARE(WebpCodec::decodeForThumbnailResize(encoded, source.size()).format(),
                     QImage::Format_RGB888);
        }
    }

    void previewFallbackDecodesNativelyAndRotates() {
        ImageDecodeRequest request;
        request.viewerRequest = true;
        request.targetSize = QSize(19, 37);
        request.info.orientation = ExifOrientation::Rotate90CW;
        ImageData data(request);
        attachPreview(data, jpeg({144, 96}, qRgb(20, 80, 190)));
        DecodedImageInfo info;
        const QImage decoded = ThumbnailLoader::decode(data, info);
        QCOMPARE(decoded.size(), QSize(96, 144));
        QCOMPARE(data.request.targetSize, request.targetSize);
        QVERIFY(info.previewUsed.contains(QStringLiteral("test-preview")));
    }

    void thumbnailBoundsPreserveAspectWithoutMetadata() {
        for (const QSize sourceSize : {QSize(144, 256), QSize(256, 144)}) {
            QImage source(sourceSize, QImage::Format_RGB32);
            source.fill(Qt::green);
            QByteArray bytes;
            QBuffer buffer(&bytes);
            QVERIFY(buffer.open(QIODevice::WriteOnly));
            QVERIFY(source.save(&buffer, "PNG"));
            ImageDecodeRequest request;
            request.targetSize = QSize(84, 64);
            request.expandToCacheResolution = false;
            // Deliberately leave imageSize unset: dimensions can arrive late.
            ImageData data(request);
            data.data = bytes;
            data.mimeType = QStringLiteral("image/png");
            DecodedImageInfo info;
            const QImage decoded = ThumbnailLoader::decode(data, info);
            QCOMPARE(decoded.size(), sourceSize);
            const QImage derived = ThumbnailLoader::createThumbnail(source, request.targetSize);
            QCOMPARE(derived.size(), sourceSize.scaled(request.targetSize, Qt::KeepAspectRatio));
        }
    }

    void benchmarkThumbnailResampling() {
        const QString fixturePath = qEnvironmentVariable("ZOIN_THUMBNAIL_BENCHMARK_FIXTURES");
        if (fixturePath.isEmpty())
            QSKIP("Set ZOIN_THUMBNAIL_BENCHMARK_FIXTURES to a directory of JPEGs");

        const auto files = QDir(fixturePath).entryInfoList(
            {"*.jpg"}, QDir::Files, QDir::Name);
        QVERIFY(!files.isEmpty());
        for (const QSize bounds : {QSize(256, 292), QSize(512, 635)}) {
            for (const auto &file : files) {
                QFile input(file.absoluteFilePath());
                QVERIFY(input.open(QIODevice::ReadOnly));
                ImageDecodeRequest request;
                request.targetSize = bounds;
                request.expandToCacheResolution = false;
                ImageData data(request);
                data.data = input.readAll();
                data.mimeType = QStringLiteral("image/jpeg");
                DecodedImageInfo info;
                const QImage raster = ThumbnailLoader::decode(data, info);
                QVERIFY(!raster.isNull());
                QList<qint64> qtTimes, cubicTimes;
                for (int repeat = 0; repeat < 8; ++repeat) {
                    for (int pass = 0; pass < 2; ++pass) {
                        const bool cubic = (repeat + pass) % 2 != 0;
                        QElapsedTimer timer;
                        timer.start();
                        const QImage resized = cubic
                            ? ThumbnailLoader::createThumbnail(raster, bounds)
                            : raster.scaled(bounds, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                        const qint64 elapsed = timer.nsecsElapsed();
                        QVERIFY(!resized.isNull());
                        if (repeat > 0)
                            (cubic ? cubicTimes : qtTimes).append(elapsed);
                    }
                }
                qInfo() << "THUMBNAIL_RESIZE" << file.fileName() << raster.size() << bounds
                        << qtTimes << cubicTimes;
            }
        }
    }

    void runnerRoutesViewerAndThumbnailsThroughScaleAwareFilter() {
        QImage source(256, 128, QImage::Format_RGB32);
        for (int y = 0; y < source.height(); ++y) {
            for (int x = 0; x < source.width(); ++x) {
                source.setPixel(x, y, (x % 2) ? qRgb(255, 255, 255) : qRgb(0, 0, 0));
            }
        }
        QByteArray bytes;
        QBuffer buffer(&bytes);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(source.save(&buffer, "PNG"));
        const QSize target(192, 96);
        const QImage viewer = ThumbnailLoader::createViewerImage(source, target);
        const QImage thumbnail = ThumbnailLoader::createThumbnail(source, target);
        QVERIFY(samePixels(viewer, thumbnail));
        QVERIFY(!samePixels(thumbnail, source.scaled(
            target, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
        for (bool viewerRequest : {false, true}) {
            ImageDecodeRequest request;
            request.viewerRequest = viewerRequest;
            request.storeInPersistentCache = false;
            request.expandToCacheResolution = false;
            request.targetSize = target;
            ImageData data(request);
            data.data = bytes;
            data.mimeType = QStringLiteral("image/png");
            ImageDecodeRunner runner(data);
            QImage actual;
            connect(&runner, &ImageDecodeRunner::imageReady, this,
                    [&](const ImageDecodeRequest &delivered, const QImage &image,
                        const DecodedImageInfo &) {
                QCOMPARE(delivered.targetSize, target);
                QCOMPARE(delivered.viewerRequest, viewerRequest);
                actual = image;
            });
            runner.run();
            const QImage expected = viewerRequest ? viewer : thumbnail;
            QCOMPARE(actual.size(), target);
            for (int x = 0; x < target.width(); ++x) {
                QCOMPARE(actual.pixel(x, target.height() / 2),
                         expected.pixel(x, target.height() / 2));
            }
        }
    }
};

QTEST_GUILESS_MAIN(ViewerDecodeSourceTest)
#include "ViewerDecodeSourceTest.moc"
