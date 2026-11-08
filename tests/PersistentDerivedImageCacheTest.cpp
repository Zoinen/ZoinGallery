#include "PersistentDerivedImageCache.h"
#include "PersistentImageCache.h"
#include "DisplayColorSpace.h"
#include "Decoders/ImageDecoderInterface.h"
#include "Decoders/RawDecoder.h"
#include "Runners/CacheImageRunners.h"
#include "Runners/ImageInfoReadRunner.h"
#include "Runners/ImageReadRunner.h"
#include "Runners/ImageDecodeRunner.h"
#if defined(ZOIN_ENABLE_VIDEO_THUMBNAILS)
#include "Runners/VideoThumbnailRunner.h"
#endif
#include "StorageLocations.h"
#include "TinyEXIF/TinyEXIF.h"

#include <QFileInfo>
#include <QColorSpace>
#include <QCryptographicHash>
#include <QDataStream>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <libraw/libraw.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <limits>
#include <optional>
#include <utility>

namespace {

class ScopedDisplayColorSpace final {
public:
    explicit ScopedDisplayColorSpace(const QColorSpace &target)
        : previous(DisplayColorSpace::current()),
          conversionEnabled(DisplayColorSpace::conversionEnabled()) {
        DisplayColorSpace::setCurrent(target);
        DisplayColorSpace::setConversionEnabled(true);
    }
    ~ScopedDisplayColorSpace() {
        DisplayColorSpace::setCurrent(previous);
        DisplayColorSpace::setConversionEnabled(conversionEnabled);
    }
private:
    QColorSpace previous;
    bool conversionEnabled;
};

class TypedExifFieldReader final : public ImageDecoderInterface {
public:
    using ImageDecoderInterface::readTypedFileFields;
};

class TypedRawFieldReader final : public RawDecoder {
public:
    using RawDecoder::readTypedFileFields;
};

class CountingSourceProvider final
    : public ZoinGallery::ImageSourceProvider {
public:
    ZoinGallery::ImageSourceReadResult readRange(
        const ZoinGallery::ImageSourceDescriptor &, qint64, qint64,
        const QSharedPointer<ZoinGallery::ImageSourceCancellation> &)
        override {
        ++rangeReads;
        return {.errorString = QStringLiteral("unexpected range read")};
    }

    QSharedPointer<ZoinGallery::ImageSourceLease> materialize(
        const ZoinGallery::ImageSourceDescriptor &,
        const QSharedPointer<ZoinGallery::ImageSourceCancellation> &)
        override {
        ++materializations;
        return {};
    }

    std::atomic_int rangeReads{0};
    std::atomic_int materializations{0};
};

class MetadataSourceLease final : public ZoinGallery::ImageSourceLease {
public:
    explicit MetadataSourceLease(QString path) : _path(std::move(path)) {}
    QString localPath() const override { return _path; }

private:
    QString _path;
};

class MetadataSourceProvider final
    : public ZoinGallery::ImageSourceProvider {
public:
    explicit MetadataSourceProvider(QString path)
        : _path(std::move(path)) {}

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
        ++materializations;
        return QSharedPointer<MetadataSourceLease>::create(_path);
    }

    std::atomic_int materializations{0};

private:
    QString _path;
};

ImageDecodeRequest requestFor(QString versionStrength,
                              QString contentVersion =
                                  QStringLiteral("revision-1"),
                              QString resourceId =
                                  QStringLiteral("resource-17")) {
    ImageDecodeRequest request;
    request.info.path = QStringLiteral("opaque-gallery-entry");
    request.info.source = {
        .resourceId = std::move(resourceId),
        .sourceKey = QStringLiteral("account-a/folder/image.jpg"),
        .contentVersion = contentVersion,
        .versionStrength = versionStrength,
        .storageClass = QStringLiteral("network"),
        .accessProfile = QStringLiteral("nativeRange"),
        .displayName = QStringLiteral("image.jpg"),
        .mimeType = QStringLiteral("image/jpeg"),
        .size = 123456,
    };
    request.info.sourceVersionToken = contentVersion;
    request.info.imageSize = QSize(1920, 1080);
    request.targetSize = QSize(384, 216);
    request.checkCache = true;
    request.storeInPersistentCache = true;
    request.thumbnailTransformKey = QStringLiteral("masonry-fit-v2");
    return request;
}

QImage testImage(const QSize &size, QRgb color = qRgb(25, 90, 180)) {
    QImage image(size, QImage::Format_RGB32);
    image.fill(color);
    return image;
}

void persist(const ImageDecodeRequest &request, const QImage &image) {
    const QByteArray prepared =
        PersistentImageCache::createImageForCache(request, image);
    QVERIFY(!prepared.isEmpty());
    // This is the exact call shape of CachedImageStoreRunner: DecodeManager
    // intentionally hands the runner ImageInfo plus the prepared artifact.
    PersistentImageCache::storeImage(request.info, prepared);
}

} // namespace

class PersistentDerivedImageCacheTest final : public QObject {
    Q_OBJECT
    QTemporaryDir cacheRoot;

private slots:
    void typedExifFieldsKeepDecoderValuesAndDoNotInventMissingTags() {
        TinyEXIF::EXIFInfo exif;
        exif.ExposureTime = 1.0 / 125.0;
        exif.ISOSpeedRatings = 800;
        exif.FNumber = 2.8;
        exif.LensInfo.FocalLengthIn35mm = 75.0;
        exif.LensInfo.FocalLengthMin = 24.0;
        exif.LensInfo.FocalLengthMax = 70.0;
        exif.Make = "Canon";
        exif.Model = "R5";
        exif.LensInfo.Make = "Canon";
        exif.LensInfo.Model = "RF 24-70mm F2.8";

        const QVariantMap fields =
            TypedExifFieldReader::readTypedFileFields(exif);
        QCOMPARE(fields.value(QStringLiteral("exif.exposure_time")).toDouble(),
                 1.0 / 125.0);
        QCOMPARE(fields.value(QStringLiteral("exif.iso")).toUInt(), 800U);
        QCOMPARE(fields.value(QStringLiteral("exif.f_number")).toDouble(), 2.8);
        QCOMPARE(fields.value(QStringLiteral("exif.focal_length_35mm")).toDouble(), 75.0);
        const QVariantMap range = fields.value(
            QStringLiteral("exif.lens_focal_range")).toMap();
        QCOMPARE(range.value(QStringLiteral("min")).toDouble(), 24.0);
        QCOMPARE(range.value(QStringLiteral("max")).toDouble(), 70.0);
        QCOMPARE(fields.value(QStringLiteral("exif.camera_model")).toString(),
                 QStringLiteral("Canon R5"));
        QCOMPARE(fields.value(QStringLiteral("exif.lens_model")).toString(),
                 QStringLiteral("Canon RF 24-70mm F2.8"));

        TinyEXIF::EXIFInfo missing;
        // TinyEXIF initializes the numeric members when parsing, not in its
        // default constructor. clear() gives this absent-tag fixture the
        // same initialized state as a real parser miss.
        missing.clear();
        const QVariantMap empty =
            TypedExifFieldReader::readTypedFileFields(missing);
        QVERIFY(empty.isEmpty());
        missing.ExposureTime = std::numeric_limits<double>::quiet_NaN();
        missing.ISOSpeedRatings = 0;
        missing.FNumber = -1.0;
        missing.LensInfo.FocalLengthIn35mm = 0.0;
        missing.LensInfo.FocalLengthMin = 50.0;
        missing.LensInfo.FocalLengthMax = 35.0;
        const QVariantMap malformed =
            TypedExifFieldReader::readTypedFileFields(missing);
        QVERIFY(malformed.isEmpty());

        // The APP1 segment advertises an EXIF TIFF header whose IFD offset
        // points past the end of the segment. The decoder must treat it as a
        // completed read with no typed values, not manufacture zeroes.
        const QByteArray damagedJpeg = QByteArray::fromHex(
            "FFD8FFE1001045786966000049492A0008000000FFD9");
        TinyEXIF::EXIFInfo damaged;
        const int damagedStatus = damaged.parseFrom(
            reinterpret_cast<const uint8_t *>(damagedJpeg.constData()),
            static_cast<unsigned>(damagedJpeg.size()));
        QCOMPARE(damagedStatus, TinyEXIF::PARSE_CORRUPT_DATA);
        QVERIFY(TypedExifFieldReader::readTypedFileFields(damaged).isEmpty());

        missing.LensInfo.FocalLengthMax = 50.0;
        const QVariantMap fixedLens =
            TypedExifFieldReader::readTypedFileFields(missing);
        const QVariantMap fixedRange = fixedLens.value(
            QStringLiteral("exif.lens_focal_range")).toMap();
        QCOMPARE(fixedRange.value(QStringLiteral("min")).toDouble(), 50.0);
        QCOMPARE(fixedRange.value(QStringLiteral("max")).toDouble(), 50.0);
    }

    void rawFileFieldsStaySeparateFromLegacyExifDisplayMap() {
        LibRaw raw;
        raw.imgdata.other.shutter = 1.0 / 125.0;
        raw.imgdata.other.iso_speed = 800.0;
        raw.imgdata.other.aperture = 2.8;
        std::strncpy(raw.imgdata.idata.make, "Canon",
                     sizeof(raw.imgdata.idata.make) - 1);
        std::strncpy(raw.imgdata.idata.model, "R5",
                     sizeof(raw.imgdata.idata.model) - 1);
        raw.imgdata.lens.FocalLengthIn35mmFormat = 75;
        raw.imgdata.lens.makernotes.FocalLengthIn35mmFormat = 50.0;
        raw.imgdata.lens.makernotes.MinFocal = 24.0;
        raw.imgdata.lens.makernotes.MaxFocal = 70.0;
        std::strncpy(raw.imgdata.lens.Lens, "RF 24-70mm",
                     sizeof(raw.imgdata.lens.Lens) - 1);

        TypedRawFieldReader reader;
        const QVariantMap fields = reader.readTypedFileFields(raw);
        QVERIFY(qAbs(fields.value(QStringLiteral("exif.exposure_time")).toDouble()
                     - 1.0 / 125.0) < 0.000001);
        QCOMPARE(fields.value(QStringLiteral("exif.iso")).toUInt(), 800U);
        QVERIFY(qAbs(fields.value(QStringLiteral("exif.f_number")).toDouble()
                     - 2.8) < 0.000001);
        QVERIFY(qAbs(fields.value(
                         QStringLiteral("exif.focal_length_35mm")).toDouble()
                     - 75.0) < 0.000001);
        const QVariantMap range = fields.value(
            QStringLiteral("exif.lens_focal_range")).toMap();
        QVERIFY(qAbs(range.value(QStringLiteral("min")).toDouble() - 24.0)
                < 0.000001);
        QVERIFY(qAbs(range.value(QStringLiteral("max")).toDouble() - 70.0)
                < 0.000001);
        QCOMPARE(fields.value(QStringLiteral("exif.camera_model")).toString(),
                 QStringLiteral("Canon R5"));
        QCOMPARE(fields.value(QStringLiteral("exif.lens_model")).toString(),
                 QStringLiteral("RF 24-70mm"));

        raw.imgdata.other.shutter = 0.0;
        raw.imgdata.other.iso_speed = 0.0;
        raw.imgdata.other.aperture = 0.0;
        raw.imgdata.lens.FocalLengthIn35mmFormat = 0;
        QCOMPARE(reader.readTypedFileFields(raw).value(
                     QStringLiteral("exif.focal_length_35mm")).toDouble(), 50.0);
        raw.imgdata.lens.makernotes.FocalLengthIn35mmFormat = 0.0;
        raw.imgdata.lens.makernotes.MinFocal = 0.0;
        raw.imgdata.lens.makernotes.MaxFocal = 0.0;
        raw.imgdata.idata.make[0] = '\0';
        raw.imgdata.idata.model[0] = '\0';
        raw.imgdata.lens.Lens[0] = '\0';
        QVERIFY(reader.readTypedFileFields(raw).isEmpty());
    }

    void rawMetadataReads35mmFieldDuringLibRawPass() {
        const QString externalDng =
            qEnvironmentVariable("F4_EXIF_TEST_DNG").trimmed();
        if (externalDng.isEmpty()) {
            QSKIP("Set F4_EXIF_TEST_DNG to a DNG fixture to test LibRaw EXIF capture");
        }
        LibRaw expectedRaw;
#if defined(Q_OS_WIN)
        QCOMPARE(expectedRaw.open_file(externalDng.toStdWString().c_str()),
                 LIBRAW_SUCCESS);
#else
        QCOMPARE(expectedRaw.open_file(externalDng.toUtf8().constData()),
                 LIBRAW_SUCCESS);
#endif
        const double expectedFocalLength35mm =
            expectedRaw.imgdata.lens.FocalLengthIn35mmFormat;
        QVERIFY(expectedFocalLength35mm > 0.0);

        TypedRawFieldReader reader;
        ImageInfo decoded;
        decoded.path = externalDng;
        QVERIFY(reader.readMetadata(decoded));
        QCOMPARE(decoded.typedFileFields.value(
                     QStringLiteral("exif.focal_length_35mm")).toDouble(),
                 expectedFocalLength35mm);
    }

    void typedFileFieldsAndReadCompletionSurviveMetadataCache() {
        ImageInfo source;
        source.source = {
            .resourceId = QStringLiteral("field-cache-resource"),
            .sourceKey = QStringLiteral("field-cache/photo.jpg"),
            .contentVersion = QStringLiteral("field-cache-v1"),
            .versionStrength = QStringLiteral("strong"),
            .storageClass = QStringLiteral("network"),
            .displayName = QStringLiteral("photo.jpg"),
            .size = 4096,
        };
        source.sourceVersionToken = source.source.contentVersion;
        source.imageSize = QSize(120, 80);
        source.fileFieldsRead = true;
        source.typedFileFields = {
            {QStringLiteral("exif.exposure_time"), 1.0 / 60.0},
            {QStringLiteral("exif.iso"), 800},
            {QStringLiteral("exif.f_number"), 2.8},
            {QStringLiteral("exif.focal_length_35mm"), 75.0},
            {QStringLiteral("exif.lens_focal_range"), QVariantMap{
                {QStringLiteral("min"), 24.0}, {QStringLiteral("max"), 70.0}}},
            {QStringLiteral("exif.camera_model"), QStringLiteral("Canon R5")},
            {QStringLiteral("exif.lens_model"), QStringLiteral("RF 24-70mm")},
        };
        PersistentDerivedImageCache::storeMetadata(source);

        ImageInfo restored;
        restored.source = source.source;
        restored.sourceVersionToken = source.sourceVersionToken;
        QVERIFY(PersistentDerivedImageCache::retrieveMetadata(restored));
        QVERIFY(restored.fileFieldsRead);
        QCOMPARE(restored.typedFileFields, source.typedFileFields);

        ImageInfo noTags = source;
        noTags.source.contentVersion = QStringLiteral("field-cache-no-tags");
        noTags.sourceVersionToken = noTags.source.contentVersion;
        noTags.typedFileFields.clear();
        PersistentDerivedImageCache::storeMetadata(noTags);
        ImageInfo noTagsRestored;
        noTagsRestored.source = noTags.source;
        noTagsRestored.sourceVersionToken = noTags.sourceVersionToken;
        QVERIFY(PersistentDerivedImageCache::retrieveMetadata(noTagsRestored));
        QVERIFY(noTagsRestored.fileFieldsRead);
        QVERIFY(noTagsRestored.typedFileFields.isEmpty());
    }

    void oldViewerFilterCacheIsNotReused_data() {
        QTest::addColumn<QString>("oldTransform");
        QTest::newRow("area") << QStringLiteral("viewer-fit-v1");
        QTest::newRow("encoded-pyramid-v1") << QStringLiteral("viewer-fit-bc-pyramid-v1");
        QTest::newRow("encoded-pyramid-v2") << QStringLiteral("viewer-fit-bc-pyramid-v2");
        QTest::newRow("fast-idct-pyramid-v3")
            << QStringLiteral("viewer-fit-bc-pyramid-v3-linear-half-dither");
    }

    void oldViewerFilterCacheIsNotReused() {
        QFETCH(QString, oldTransform);
        auto request = requestFor(QStringLiteral("strong"));
        request.viewerRequest = true;
        request.fitToViewerRequest = true;
        const QByteArray prepared = PersistentImageCache::createImageForCache(
            request, testImage(request.targetSize));
        QVERIFY(!prepared.isEmpty());

        // Recreate the previous cache entry with unchanged source/tier and
        // encoded image. Only the transform differs from the current entry.
        QDataStream input(prepared);
        input.setVersion(QDataStream::Qt_6_0);
        quint32 magic = 0;
        quint16 version = 0;
        QString sourceKey, contentVersion, authority, transform, decoder;
        qint64 sourceSize = 0;
        quint8 artifact = 0;
        QSize targetTier, pixelSize;
        QByteArray encoded;
        input >> magic >> version >> sourceKey >> contentVersion >> authority
              >> sourceSize >> artifact >> targetTier >> transform >> decoder
              >> pixelSize >> encoded;
        QCOMPARE(input.status(), QDataStream::Ok);
        QVERIFY(transform != oldTransform);
        QByteArray oldEntry;
        QDataStream output(&oldEntry, QIODevice::WriteOnly);
        output.setVersion(QDataStream::Qt_6_0);
        output << magic << version << sourceKey << contentVersion << authority
               << sourceSize << artifact << targetTier
               << oldTransform << decoder << pixelSize << encoded;
        PersistentImageCache::storeImage(request.info, oldEntry);
        QVERIFY(!PersistentImageCache::hasImage(request));
        QVERIFY(PersistentImageCache::retrieveImage(request).isNull());

        PersistentImageCache::storeImage(request.info, prepared);
        QVERIFY(PersistentImageCache::hasImage(request));
        QVERIFY(!PersistentImageCache::retrieveImage(request).isNull());
    }

    void viewerFitCachePreservesEncodedPixels_data() {
        QTest::addColumn<QColorSpace>("displayTarget");
        QTest::newRow("sRGB-lossless") << QColorSpace(QColorSpace::SRgb);
        QTest::newRow("P3-display-conversion") << QColorSpace(QColorSpace::DisplayP3);
    }

    void viewerFitCachePreservesEncodedPixels() {
        QFETCH(QColorSpace, displayTarget);
        const ScopedDisplayColorSpace display(displayTarget);
        auto request = requestFor(QStringLiteral("strong"));
        request.viewerRequest = true;
        request.fitToViewerRequest = true;
        request.targetSize = QSize(67, 43);
        QImage image(request.targetSize, QImage::Format_RGBA8888);
        image.setColorSpace(QColorSpace(QColorSpace::SRgb));
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                // Fine color/dither variations and partially transparent pixels
                // must survive a persistent Fit-cache round trip unchanged.
                image.setPixelColor(x, y, QColor((x * 43 + y * 17) % 256,
                    (x * 7 + y * 59) % 256, (x * 31 + y * 3) % 256,
                    (x + y) % 5 == 0 ? (x * 11 + y * 19) % 256 : 255));
            }
        }
        persist(request, image);
        // Lossless sRGB cache pixels must survive unchanged. Retrieval then
        // applies the display profile, just as a freshly decoded frame does.
        const QImage expected = image.convertedToColorSpace(displayTarget)
                                    .convertToFormat(QImage::Format_RGBA8888);
        const QImage cached = PersistentImageCache::retrieveImage(request, true)
                                  .convertToFormat(QImage::Format_RGBA8888);
        QCOMPARE(cached.size(), image.size());
        QCOMPARE(cached.colorSpace(), displayTarget);
        int changedPixels = 0;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                changedPixels += cached.pixel(x, y) != expected.pixel(x, y);
            }
        }
        QCOMPARE(changedPixels, 0);
    }

    void initTestCase() {
        QVERIFY(ZoinGallery::StorageLocations::configure(
            QStringLiteral("derived-cache-tests")));
        QVERIFY(cacheRoot.isValid());
        QVERIFY(ZoinGallery::StorageLocations::configureCacheRoot(cacheRoot.path()));
        QCOMPARE(ZoinGallery::StorageLocations::cacheRoot(), cacheRoot.path());
        QVERIFY(!ZoinGallery::StorageLocations::configureCacheRoot(cacheRoot.filePath("changed")));
        PersistentImageCache::clear();
    }

    void init() {
        PersistentImageCache::clear();
    }

    void cleanupTestCase() {
        PersistentImageCache::clear();
    }

    void configurableDiskBudgetPrunesExistingEntries() {
        QCOMPARE(PersistentDerivedImageCache::byteBudget(), 512LL * 1024 * 1024);
        PersistentDerivedImageCache::setByteBudget(4096LL * 1024 * 1024);
        QCOMPARE(PersistentDerivedImageCache::byteBudget(), 4096LL * 1024 * 1024);
        const QString path = cacheRoot.filePath("zg_derived_v1/budget-test.zgd");
        QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.resize(80LL * 1024 * 1024));
        file.close();
        QCOMPARE(PersistentDerivedImageCache::cacheSize(), 80LL * 1024 * 1024);
        PersistentDerivedImageCache::setByteBudget(64LL * 1024 * 1024);
        QVERIFY(!QFile::exists(path));
        QCOMPARE(PersistentDerivedImageCache::cacheSize(), 0);
        PersistentDerivedImageCache::setByteBudget(512LL * 1024 * 1024);
    }

    void strongHitSkipsSourceMaterialization() {
        const ImageDecodeRequest request =
            requestFor(QStringLiteral("strong"));
        persist(request, testImage(request.targetSize));

        auto provider = QSharedPointer<CountingSourceProvider>::create();
        // Construction order mirrors DecodeManager::decodeImages: the cache
        // runner registers the lookup before the source runner joins it.
        CachedImageRetrieveRunner cacheRunner(request, true);
        ImageReadRunner sourceRunner(request, provider);
        QSignalSpy readySpy(
            &cacheRunner,
            &CachedImageRetrieveRunner::cachedThumbnailRetrieved);

        cacheRunner.run();
        sourceRunner.run();

        QCOMPARE(readySpy.count(), 1);
        QCOMPARE(provider->materializations.load(), 0);
        QCOMPARE(provider->rangeReads.load(), 0);
    }

    void largerThumbnailReplacesSmallerAndServesOtherSizes_data() {
        QTest::addColumn<QString>("transform");
        QTest::newRow("image") << QStringLiteral("thumbnail-aspect-v1");
        QTest::newRow("video") << QStringLiteral("video-contact-sheet-2x2-display-v6");
    }

    void largerThumbnailReplacesSmallerAndServesOtherSizes() {
        QFETCH(QString, transform);
        auto smaller = requestFor(QStringLiteral("localStat"));
        smaller.thumbnailTransformKey = transform;
        smaller.targetSize = QSize(240, 180);
        const auto staleSmaller = PersistentImageCache::createImageForCache(
            smaller, testImage(smaller.targetSize));
        QVERIFY(!staleSmaller.isEmpty());
        PersistentImageCache::storeImage(smaller.info, staleSmaller);

        auto taller = smaller;
        taller.targetSize = QSize(410, 320);
        const auto staleTaller = PersistentImageCache::createImageForCache(
            taller, testImage(QSize(350, 263)));
        QVERIFY(!staleTaller.isEmpty());

        auto large = smaller;
        large.targetSize = QSize(400, 300);
        QVERIFY(!PersistentImageCache::hasImage(large));
        QVERIFY(PersistentImageCache::retrieveImage(large, true).isNull());
        persist(large, testImage(large.targetSize));

        auto between = smaller;
        between.targetSize = QSize(362, 271);
        auto provider = QSharedPointer<CountingSourceProvider>::create();
        CachedImageRetrieveRunner cacheRunner(between, true);
        ImageReadRunner sourceRunner(between, provider);
        QSignalSpy readySpy(&cacheRunner, &CachedImageRetrieveRunner::cachedThumbnailRetrieved);
        cacheRunner.run();
        sourceRunner.run();
        QCOMPARE(readySpy.count(), 1);
        QCOMPARE(provider->materializations.load(), 0);
        QCOMPARE(provider->rangeReads.load(), 0);

        // A smaller decode can finish after a larger one.
        PersistentImageCache::storeImage(smaller.info, staleSmaller);
        QCOMPARE(PersistentImageCache::retrieveImage(large, true).size(), large.targetSize);
        QCOMPARE(QDir(cacheRoot.filePath("zg_derived_v1")).entryList({"*.zgd"}, QDir::Files).size(), 1);

        QVERIFY(PersistentImageCache::retrieveImage(taller, true).isNull());
        // The bounding box requests more height, but the source yields a
        // smaller frame. A resize must not leave a second size on disk.
        PersistentImageCache::storeImage(taller.info, staleTaller);
        QCOMPARE(QDir(cacheRoot.filePath("zg_derived_v1")).entryList({"*.zgd"}, QDir::Files).size(), 1);
        QCOMPARE(PersistentImageCache::retrieveImage(large, true).size(), large.targetSize);

        auto revised = between;
        revised.info.source.contentVersion = QStringLiteral("revision-2");
        revised.info.sourceVersionToken = revised.info.source.contentVersion;
        QVERIFY(PersistentImageCache::retrieveImage(revised, true).isNull());
    }

    void cappedThumbnailIsPreservedWhileLargerRequestsDecode_data() {
        QTest::addColumn<QString>("transform");
        QTest::addColumn<QSize>("generatedSize");
        QTest::addColumn<QSize>("cachedSize");
        QTest::newRow("portrait") << QStringLiteral("thumbnail-aspect-v1")
                                 << QSize(1200, 1600) << QSize(768, 1024);
        QTest::newRow("video") << QStringLiteral("video-contact-sheet-2x2-display-v6")
                              << QSize(1600, 900) << QSize(1024, 576);
    }

    void cappedThumbnailIsPreservedWhileLargerRequestsDecode() {
        QFETCH(QString, transform);
        QFETCH(QSize, generatedSize);
        QFETCH(QSize, cachedSize);
        auto request = requestFor(QStringLiteral("localStat"));
        request.thumbnailTransformKey = transform;
        request.targetSize = QSize(1600, 1600);
        persist(request, testImage(generatedSize));
        // The slot records this exact request tier even though it stores capped pixels.
        QVERIFY(!PersistentImageCache::hasImage(request));
        QVERIFY(PersistentImageCache::retrieveImage(request, true).isNull());
        QCOMPARE(PersistentImageCache::retrieveImage(request, false).size(), cachedSize);

        const QDir cache(cacheRoot.filePath("zg_derived_v1"));
        const auto entries = cache.entryList({"*.zgd"}, QDir::Files);
        QCOMPARE(entries.size(), 1);
        QFile stored(cache.filePath(entries.first()));
        QVERIFY(stored.open(QIODevice::ReadOnly));
        const QByteArray original = stored.readAll();
        stored.close();

        auto larger = request;
        larger.targetSize = generatedSize;
        QVERIFY(!PersistentImageCache::hasImage(larger));
        const QString sourcePath = cacheRoot.filePath("large-preview.png");
        const QImage source = testImage(generatedSize * 2, qRgb(180, 25, 90));
        QVERIFY(source.save(sourcePath));
        larger.info.source.displayName = QStringLiteral("large-preview.png");
        larger.info.source.mimeType = QStringLiteral("image/png");
        larger.info.imageSize = source.size();
        auto provider = QSharedPointer<MetadataSourceProvider>::create(sourcePath);
        CachedImageRetrieveRunner cacheRunner(larger, true);
        ImageReadRunner sourceRunner(larger, provider);
        QSignalSpy readySpy(&cacheRunner, &CachedImageRetrieveRunner::cachedThumbnailRetrieved);
        std::optional<ImageData> sourceRead;
        connect(&sourceRunner, &ImageReadRunner::imageReadReady, this,
                [&sourceRead](const ImageData &data) { sourceRead.emplace(data); });
        cacheRunner.run();
        sourceRunner.run();
        QCOMPARE(readySpy.count(), 0);
        QCOMPARE(provider->materializations.load(), 1);
        QVERIFY(sourceRead.has_value());

        ImageDecodeRunner decoder(*sourceRead);
        QSignalSpy decoded(&decoder, &ImageDecodeRunner::imageReady);
        QSignalSpy writes(&decoder, &ImageDecodeRunner::storeInCache);
        decoder.run();
        QCOMPARE(decoded.count(), 1);
        QCOMPARE(decoded.first().at(1).value<QImage>().size(), generatedSize);
        QCOMPARE(writes.count(), 0);
        auto smaller = request;
        smaller.targetSize = QSize(400, 400);
        QCOMPARE(PersistentImageCache::retrieveImage(smaller, true).size(), cachedSize);
        QCOMPARE(cache.entryList({"*.zgd"}, QDir::Files), entries);
        QVERIFY(stored.open(QIODevice::ReadOnly));
        QCOMPARE(stored.readAll(), original);
    }

    void viewerPreviewKeepsLegacyLocalThumbnailDimensions() {
        const QImage thumbnail = testImage(QSize(120, 80));
        ImageDecodeRequest request;
        request.info.path = cacheRoot.filePath("legacy-local.png");
        QVERIFY(thumbnail.save(request.info.path));
        const QFileInfo file(request.info.path);
        request.info.lastModified = file.lastModified();
        request.info.fileSize = file.size();
        request.info.imageSize = thumbnail.size();
        const auto encoded = PersistentImageCache::createImageForCache(thumbnail);
        QVERIFY(!encoded.isEmpty());
        PersistentImageCache::storeImage(request.info, encoded);
        request.targetSize = QSize(1, 1);
        request.viewerThumbnailRequest = true;
        QCOMPARE(PersistentImageCache::retrieveImage(request, true).size(), thumbnail.size());
    }

    void viewerFitWriterRespectsCacheClear() {
        auto request = requestFor(QStringLiteral("localStat"));
        request.viewerRequest = true;
        request.fitToViewerRequest = true;
        const QImage frame = testImage(request.targetSize);
        const quint64 generation = PersistentDerivedImageCache::writeGeneration();
        PersistentDerivedImageCache::clear();
        PersistentDerivedImageCache::storeViewerFitFrame(request, frame, generation);
        QVERIFY(PersistentImageCache::retrieveImage(request, true).isNull());
        PersistentDerivedImageCache::storeViewerFitFrame(
            request, frame, PersistentDerivedImageCache::writeGeneration());
        QCOMPARE(PersistentImageCache::retrieveImage(request, true).size(), frame.size());
    }

    void exactSizeThumbnailFromExistingCacheStillLoads() {
        auto request = requestFor(QStringLiteral("localStat"));
        request.thumbnailTransformKey = QStringLiteral("thumbnail-aspect-v1");
        const auto entry = PersistentImageCache::createImageForCache(
            request, testImage(request.targetSize));
        QVERIFY(!entry.isEmpty());

        QDataStream header(entry);
        header.setVersion(QDataStream::Qt_6_0);
        quint32 magic;
        quint16 version;
        QString sourceKey, revision, authority, transform, decoder;
        qint64 sourceSize;
        quint8 artifact;
        QSize target;
        header >> magic >> version;
        const auto keyStart = header.device()->pos();
        header >> sourceKey >> revision >> authority >> sourceSize >> artifact
               >> target >> transform >> decoder;
        QCOMPARE(header.status(), QDataStream::Ok);
        const auto key = entry.mid(keyStart, header.device()->pos() - keyStart);
        const auto name = QCryptographicHash::hash(key, QCryptographicHash::Sha256).toHex();
        const QDir cache(cacheRoot.filePath("zg_derived_v1"));
        QVERIFY(QDir().mkpath(cache.path()));
        QFile legacy(cache.filePath(QString::fromLatin1(name) + QStringLiteral(".zgd")));
        QVERIFY(legacy.open(QIODevice::WriteOnly));
        QCOMPARE(legacy.write(entry), entry.size());
        legacy.close();

        QCOMPARE(PersistentImageCache::retrieveImage(request, true).size(), request.targetSize);
        auto viewerThumbnail = request;
        viewerThumbnail.viewerThumbnailRequest = true;
        viewerThumbnail.targetSize = QSize(1, 1);
        QCOMPARE(PersistentImageCache::retrieveImage(viewerThumbnail, true).size(),
                 request.targetSize);
        viewerThumbnail.info.source.contentVersion = QStringLiteral("changed");
        viewerThumbnail.info.sourceVersionToken = QStringLiteral("changed");
        QVERIFY(PersistentImageCache::retrieveImage(viewerThumbnail, true).isNull());
        request.targetSize = QSize(512, 288);
        persist(request, testImage(request.targetSize));
        QVERIFY(legacy.open(QIODevice::ReadOnly));
        QCOMPARE(legacy.readAll(), entry);
        QCOMPARE(cache.entryList({"*.zgd"}, QDir::Files).size(), 2);
    }

    void legacyVideoHitCompletesSourceLookupGate() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(
            QStringLiteral("local-video.mp4"));
        QFile source(path);
        QVERIFY(source.open(QIODevice::WriteOnly));
        QVERIFY(source.write("cached-video-source") > 0);
        source.close();

        ImageDecodeRequest request;
        request.info.path = path;
        request.info.lastModified = QFileInfo(path).lastModified();
        request.info.fileSize = QFileInfo(path).size();
        request.info.thumbnailKind = QStringLiteral("video");
        request.targetSize = QSize(96, 54);
        request.info.imageSize = request.targetSize;
        request.checkCache = true;
        request.storeInPersistentCache = true;

        const QByteArray prepared = PersistentImageCache::createImageForCache(
            request, testImage(request.targetSize));
        QVERIFY(!prepared.isEmpty());
        PersistentImageCache::storeImage(request.info, prepared);
        QVERIFY(PersistentImageCache::hasImage(request));

        // The cache runner and the local video runner are admitted in
        // parallel by DecodeManager.  A local request has no derived source
        // descriptor, so the gate must still be registered from the
        // path/mtime/size cache key; otherwise QMediaPlayer can start before
        // the legacy cache hit is delivered.
        CachedImageRetrieveRunner cacheRunner(request, true);
        const auto gate = PersistentDerivedImageCache::joinLookup(request);
        QVERIFY(gate);
        auto cancellation =
            QSharedPointer<ZoinGallery::ImageSourceCancellation>::create();
        cacheRunner.run();
        QVERIFY(PersistentDerivedImageCache::waitForLookup(
            gate, cancellation));
    }

#if defined(ZOIN_ENABLE_VIDEO_THUMBNAILS)
    void portraitVideoContactSheetKeepsDisplayAspectRatio() {
        const QString externalFixture = qEnvironmentVariable("ZOIN_PORTRAIT_VIDEO_FILE");
        const QString fixture = externalFixture.isEmpty()
            ? QFINDTESTDATA("data/video-thumbnail-portrait.mp4") : externalFixture;
        QVERIFY(!fixture.isEmpty());
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath("portrait.mp4");
        QVERIFY(QFile::copy(fixture, path));
        ImageDecodeRequest request;
        request.info.path = path;
        request.info.lastModified = QFileInfo(path).lastModified();
        request.info.fileSize = QFileInfo(path).size();
        request.info.imageSize = QSize(16, 9);
        request.info.thumbnailKind = QStringLiteral("video");
        request.targetSize = QSize(320, 180);
        request.checkCache = false;
        request.storeInPersistentCache = false;
        VideoThumbnailRunner runner(request);
        QImage sheet;
        QImage poster;
        QSize displaySize;
        connect(&runner, &VideoThumbnailRunner::imageInfoReady, this,
                [&](const ImageInfo &info) { displaySize = info.imageSize; });
        connect(&runner, &VideoThumbnailRunner::imageReady, this,
                [&](const ImageDecodeRequest &ready, const QImage &image,
                    const DecodedImageInfo &) {
            if (ready.videoPosterRequest)
                poster = image;
            else
                sheet = image;
        });
        runner.run();
        QVERIFY(!sheet.isNull());
        QVERIFY2(qAbs(qreal(sheet.width()) / sheet.height() - 9.0 / 16.0)
                     < 0.01, qPrintable(QStringLiteral("sheet is %1x%2")
                         .arg(sheet.width()).arg(sheet.height())));
        QVERIFY(sheet.width() <= 320 && sheet.height() <= 180);
        QVERIFY(!poster.isNull());
        QVERIFY(qAbs(qreal(poster.width()) / poster.height() - 9.0 / 16.0) < 0.01);
        for (const QImage &image : {poster, sheet})
            for (const QPoint point : {QPoint(0, 0), QPoint(image.width() / 2, image.height() / 2),
                                       QPoint(image.width() - 1, image.height() - 1)})
                QCOMPARE(image.pixelColor(point).alpha(), 255);
        QVERIFY(qAbs(qreal(displaySize.width()) / displaySize.height() - 9.0 / 16.0) < 0.01);
        qInfo() << "Portrait video display" << displaySize << "poster" << poster.size()
                << "contact sheet" << sheet.size();
        const QString capture = qEnvironmentVariable("ZOIN_PORTRAIT_VIDEO_CAPTURE");
        if (!capture.isEmpty()) {
            QVERIFY(sheet.save(capture + "-sheet.png"));
            QVERIFY(poster.save(capture + "-poster.png"));
        }
        // A solid frame must reach both edges of each cell, without bars.
        if (externalFixture.isEmpty()) {
            QVERIFY(sheet.pixelColor(0, 0).red() > 200);
            QVERIFY(sheet.pixelColor(sheet.width() - 1, sheet.height() - 1).red() > 200);
        }
    }

    void decodedVideoPosterSizeIsIndependentOfPanelSize() {
        const QString fixture = QFINDTESTDATA("data/video-poster-1280x720.mp4");
        QVERIFY(!fixture.isEmpty());
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath("video.mp4");
        QVERIFY(QFile::copy(fixture, path));

        bool cachedPosterExpected = false;
        for (const QSize panelSize : {QSize(96, 54), QSize(640, 360)}) {
            ImageDecodeRequest request;
            request.info.path = path;
            request.info.lastModified = QFileInfo(path).lastModified();
            request.info.fileSize = QFileInfo(path).size();
            request.info.imageSize = QSize(1280, 720);
            request.info.thumbnailKind = QStringLiteral("video");
            request.targetSize = panelSize;
            request.panelThumbnailRequest = true;
            request.checkCache = true;

            VideoThumbnailRunner runner(request);
            QImage poster;
            QImage sheet;
            ImageDecodeRequest posterRequest;
            bool posterFromCache = false;
            connect(&runner, &VideoThumbnailRunner::imageReady, this,
                    [&](const ImageDecodeRequest &ready, const QImage &image,
                        const DecodedImageInfo &info) {
                if (ready.videoPosterRequest) {
                    poster = image;
                    posterRequest = ready;
                    posterFromCache = info.isFromCache;
                } else {
                    sheet = image;
                }
            });
            connect(&runner, &VideoThumbnailRunner::storeInCache, this,
                    [](const ImageDecodeRequest &ready, const QByteArray &data) {
                PersistentImageCache::storeImage(ready, data);
            });
            runner.run();

            QCOMPARE(sheet.size(), panelSize);
            QCOMPARE(poster.size(), QSize(1024, 576));
            QCOMPARE(posterRequest.targetSize, QSize(1024, 1024));
            QVERIFY(!posterRequest.panelThumbnailRequest);
            QCOMPARE(posterFromCache, cachedPosterExpected);
            QCOMPARE(PersistentImageCache::retrieveImage(posterRequest).size(),
                     QSize(1024, 576));
            cachedPosterExpected = true;
        }
    }

    void videoFieldsArePublishedWithThumbnailAndCached_data() {
        QTest::addColumn<bool>("rotated");
        QTest::newRow("landscape") << false;
        QTest::newRow("portrait-track-matrix") << true;
    }

    void videoFieldsArePublishedWithThumbnailAndCached() {
        QFETCH(bool, rotated);
        const QString fixture = QFINDTESTDATA("data/video-fields-av.mp4");
        QVERIFY(!fixture.isEmpty());
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath("video.mp4");
        QFile input(fixture);
        QVERIFY(input.open(QIODevice::ReadOnly));
        QByteArray bytes = input.readAll();
        if (rotated) {
            // This fixture has a version-0 video tkhd. Replace only its
            // identity display matrix; encoded pixels and dimensions stay put.
            const qsizetype tkhd = bytes.indexOf("tkhd");
            QVERIFY(tkhd >= 0 && tkhd + 80 <= bytes.size());
            QCOMPARE(bytes.at(tkhd + 4), char(0));
            QByteArray matrix;
            QDataStream stream(&matrix, QIODevice::WriteOnly);
            for (qint32 value : {0, 65536, 0, -65536, 0, 0, 0, 0, 1073741824})
                stream << value;
            bytes.replace(tkhd + 44, matrix.size(), matrix);
        }
        QFile output(path);
        QVERIFY(output.open(QIODevice::WriteOnly));
        QCOMPARE(output.write(bytes), bytes.size());
        output.close();
        const QSize displaySize = rotated ? QSize(240, 320) : QSize(320, 240);
        ImageDecodeRequest request;
        request.info.path = path;
        request.info.thumbnailKind = "video";
        request.info.imageSize = QSize(16, 9);
        request.info.source = {.resourceId="video-fields-resource", .sourceKey="video-fields/sample.mp4",
            .contentVersion=rotated ? "video-fields-portrait-v1" : "video-fields-v2",
            .versionStrength="strong", .storageClass="network",
            .displayName="sample.mp4", .size=QFileInfo(path).size()};
        request.info.sourceVersionToken = request.info.source.contentVersion;
        request.targetSize = QSize(160, 160);
        request.thumbnailTransformKey = "video-contact-sheet-2x2-display-v6";
        request.checkCache = true;
        request.storeInPersistentCache = true;
        auto provider = QSharedPointer<MetadataSourceProvider>::create(path);
        CachedImageRetrieveRunner firstLookup(request, true);
        VideoThumbnailRunner runner(request, provider);
        firstLookup.run();
        ImageInfo metadata;
        QImage poster;
        QImage sheet;
        connect(&runner, &VideoThumbnailRunner::imageReady, this,
                [&](const ImageDecodeRequest &ready, const QImage &image, const DecodedImageInfo &) {
            if (ready.videoPosterRequest)
                poster = image;
            else
                sheet = image;
        });
        connect(&runner, &VideoThumbnailRunner::imageInfoReady, this,
                [&](const ImageInfo &info) { metadata = info; });
        connect(&runner, &VideoThumbnailRunner::storeInCache, this,
                [](const ImageDecodeRequest &ready, const QByteArray &bytes) {
            PersistentImageCache::storeImage(ready, bytes);
        });
        runner.run();
        QVERIFY(metadata.fileFieldsRead);
        QCOMPARE(metadata.imageSize, displaySize);
        QCOMPARE(poster.size(), displaySize);
        QCOMPARE(sheet.size(), displaySize.scaled(request.targetSize, Qt::KeepAspectRatio));
        const auto fields = metadata.typedFileFields;
        for (const auto *key : {"media.duration", "media.bitrate", "media.format",
                               "video.codec", "video.resolution", "video.frame_rate",
                               "video.bitrate", "audio.codec", "audio.bitrate"})
            QVERIFY2(fields.contains(key), key);
        QCOMPARE(fields.value("video.resolution").toString(), QString("320×240"));
        QCOMPARE(fields.value("video.frame_rate").toDouble(), 24.0);
        QVERIFY(qAbs(fields.value("media.duration").toDouble() - 1.0) < 0.1);
        QCOMPARE(provider->materializations.load(), 1);
        PersistentDerivedImageCache::clearMemoryMetadata();
        ImageInfo restored;
        restored.source = metadata.source;
        restored.sourceVersionToken = metadata.sourceVersionToken;
        QVERIFY(PersistentDerivedImageCache::retrieveMetadata(restored));
        QCOMPARE(restored.typedFileFields, fields);
        QVERIFY(restored.fileFieldsRead);
        request.info = restored;
        request.info.thumbnailKind = "video";
        request.info.imageSize = QSize(16,9);
        request.info.fileFieldsRead = false;
        request.info.typedFileFields.clear();
        CachedImageRetrieveRunner warmLookup(request, true);
        VideoThumbnailRunner warm(request, provider);
        ImageInfo cachedVideoInfo;
        connect(&warmLookup, &CachedImageRetrieveRunner::cachedThumbnailRetrieved, this,
                [&](const ImageDecodeRequest &ready, const QImage &, const DecodedImageInfo &) {
            cachedVideoInfo = ready.info;
        });
        warmLookup.run();
        QCOMPARE(cachedVideoInfo.imageSize, metadata.imageSize);
        QCOMPARE(cachedVideoInfo.typedFileFields, fields);
        QVERIFY(cachedVideoInfo.fileFieldsRead);
        warm.run();
        QCOMPARE(provider->materializations.load(), 1);
        // If metadata was evicted while pixels survive, reuse the existing
        // player pass without rebuilding the cached sheet or poster.
        ImageInfo missingMetadata = metadata;
        missingMetadata.fileFieldsRead = false;
        missingMetadata.typedFileFields.clear();
        PersistentDerivedImageCache::storeMetadata(missingMetadata);
        request.info.fileFieldsRead = false;
        request.info.typedFileFields.clear();
        CachedImageRetrieveRunner missingLookup(request, true);
        VideoThumbnailRunner missing(request, provider);
        missingLookup.run();
        ImageInfo reloaded;
        connect(&missing, &VideoThumbnailRunner::imageInfoReady, this,
                [&](const ImageInfo &info) { reloaded = info; });
        missing.run();
        QVERIFY(reloaded.fileFieldsRead);
        QCOMPARE(reloaded.imageSize, displaySize);
        QCOMPARE(reloaded.typedFileFields, fields);
        QCOMPARE(provider->materializations.load(), 2);
    }
#endif

    void firstVideoFramePosterUsesSeparateStatValidatedCacheArtifact_data() {
        QTest::addColumn<QColorSpace>("displayTarget");
        QTest::newRow("sRGB") << QColorSpace(QColorSpace::SRgb);
        QTest::newRow("DisplayP3") << QColorSpace(QColorSpace::DisplayP3);
    }

    void firstVideoFramePosterUsesSeparateStatValidatedCacheArtifact() {
        QFETCH(QColorSpace, displayTarget);
        const ScopedDisplayColorSpace display(displayTarget);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(
            QStringLiteral("local-video.mp4"));
        QFile source(path);
        QVERIFY(source.open(QIODevice::WriteOnly));
        QCOMPARE(source.write(QByteArrayLiteral("video-revision-one")),
                 qint64(18));
        source.close();

        ImageDecodeRequest sheet;
        sheet.info.path = path;
        sheet.info.lastModified = QFileInfo(path).lastModified();
        sheet.info.fileSize = QFileInfo(path).size();
        sheet.info.imageSize = QSize(384, 216);
        sheet.info.thumbnailKind = QStringLiteral("video");
        sheet.targetSize = QSize(384, 216);
        sheet.checkCache = true;
        sheet.thumbnailTransformKey = QStringLiteral(
            "video-contact-sheet-2x2-display-v6");
        const QImage sheetImage = testImage(sheet.targetSize, qRgb(30, 60, 90));
        const QByteArray sheetBytes = PersistentImageCache::createImageForCache(
            sheet, sheetImage);
        QVERIFY(!sheetBytes.isEmpty());
        PersistentImageCache::storeImage(sheet.info, sheetBytes);

        ImageDecodeRequest poster = sheet;
        poster.videoPosterRequest = true;
        poster.targetSize = QSize(1024, 1024);
        poster.thumbnailTransformKey = QStringLiteral(
            "video-first-frame-poster-1024-display-v5");
        const QImage posterImage = testImage(QSize(960, 540), qRgb(200, 120, 40));
        const QByteArray posterBytes = PersistentDerivedImageCache::
            createImageForCache(poster, posterImage);
        QVERIFY(!posterBytes.isEmpty());
        PersistentDerivedImageCache::storeImage(poster, posterBytes);

        const auto verifiesCachedColor = [displayTarget](const QImage &actual,
                                            const QImage &expected) {
            if (actual.isNull() || actual.size() != expected.size()
                || actual.colorSpace() != displayTarget) {
                return false;
            }
            QImage expectedDisplay = expected;
            expectedDisplay.setColorSpace(QColorSpace(QColorSpace::SRgb));
            expectedDisplay = expectedDisplay.convertedToColorSpace(displayTarget);
            const QColor actualColor = actual.pixelColor(0, 0);
            const QColor expectedColor = expectedDisplay.pixelColor(0, 0);
            return qAbs(actualColor.red() - expectedColor.red()) <= 12
                && qAbs(actualColor.green() - expectedColor.green()) <= 12
                && qAbs(actualColor.blue() - expectedColor.blue()) <= 12;
        };
        QVERIFY(verifiesCachedColor(
            PersistentImageCache::retrieveImage(sheet), sheetImage));
        QVERIFY(verifiesCachedColor(
            PersistentDerivedImageCache::retrieveImage(poster), posterImage));

        QVERIFY(source.open(QIODevice::Append));
        QCOMPARE(source.write(QByteArrayLiteral("-changed")), qint64(8));
        source.close();
        const QFileInfo changed(path);
        poster.info.lastModified = changed.lastModified();
        poster.info.fileSize = changed.size();
        QVERIFY(!PersistentDerivedImageCache::hasImage(poster));
    }

    void jpegWithoutOrientationRetainsMetadata() {
        QTemporaryDir directory;
        const QString path = directory.filePath("without-orientation.jpg");
        QVERIFY(testImage(QSize(120, 80)).save(path, "JPEG"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QByteArray jpeg = file.readAll();
        file.close();
        // Valid EXIF with an ImageDescription, but no Orientation tag.
        jpeg.insert(2, QByteArray::fromHex(
            "ffe1002245786966000049492a000800000001000e010200040000007465737400000000"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(jpeg), jpeg.size());
        file.close();
        auto request = requestFor("strong", "jpeg-without-orientation");
        request.info.source.size = jpeg.size();
        auto provider = QSharedPointer<MetadataSourceProvider>::create(path);
        ImageInfoReadRunner reader(request.info.source.runtimeIdentity(), true, false, false,
            0, false, "jpeg-first", request.info.sourceVersionToken, request.info.source, provider);
        QSignalSpy result(&reader, &ImageInfoReadRunner::imageInfoReady);
        reader.run();
        QCOMPARE(result.size(), 1);
        const auto discovered = result.first().first().value<ImageInfo>();
        QCOMPARE(discovered.imageSize, QSize(120, 80));
        QCOMPARE(discovered.orientation, ExifOrientation::Horizontal);
        auto cached = request.info;
        QVERIFY(PersistentDerivedImageCache::retrieveMetadata(cached));
        QCOMPARE(cached.imageSize, QSize(120, 80));
    }

    void coldMetadataManifestReachesDerivedHitWithoutMaterialization() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString backing = directory.filePath(
            QStringLiteral("cold-cache.png"));
        QImage source(80, 60, QImage::Format_RGB32);
        source.fill(Qt::darkCyan);
        QVERIFY(source.save(backing, "PNG"));

        ImageDecodeRequest request = requestFor(
            QStringLiteral("strong"), QStringLiteral("png-revision-1"));
        request.info.source.sourceKey = QStringLiteral("remote/cold-cache.png");
        request.info.source.displayName = QStringLiteral("cold-cache.png");
        request.info.source.mimeType = QStringLiteral("image/png");
        request.info.source.size = QFileInfo(backing).size();
        request.info.imageSize = {};

        auto firstProvider =
            QSharedPointer<MetadataSourceProvider>::create(backing);
        ImageInfoReadRunner firstMetadata(
            request.info.source.runtimeIdentity(), true, false, false, 0,
            false, QStringLiteral("cold-first"),
            request.info.sourceVersionToken, request.info.source,
            firstProvider);
        QSignalSpy firstMetadataSpy(
            &firstMetadata, &ImageInfoReadRunner::imageInfoReady);
        firstMetadata.run();
        QCOMPARE(firstMetadataSpy.size(), 1);
        QCOMPARE(firstProvider->materializations.load(), 1);
        const ImageInfo discovered = firstMetadataSpy.constFirst().at(0)
            .value<ImageInfo>();
        QCOMPARE(discovered.imageSize, QSize(80, 60));
        QVERIFY(!discovered.isCached);

        request.info = discovered;
        request.targetSize = QSize(80, 60);
        request.viewerRequest = true;
        request.backgroundViewerRequest = true;
        request.fitToViewerRequest = true;
        persist(request, testImage(request.targetSize));

        auto coldProvider = QSharedPointer<CountingSourceProvider>::create();
        ImageInfoReadRunner coldMetadata(
            request.info.source.runtimeIdentity(), true, false, false, 0,
            false, QStringLiteral("cold-second"),
            request.info.sourceVersionToken, request.info.source,
            coldProvider);
        QSignalSpy coldMetadataSpy(
            &coldMetadata, &ImageInfoReadRunner::imageInfoReady);
        coldMetadata.run();
        QCOMPARE(coldMetadataSpy.size(), 1);
        const ImageInfo cachedInfo = coldMetadataSpy.constFirst().at(0)
            .value<ImageInfo>();
        QVERIFY(cachedInfo.isCached);
        QCOMPARE(cachedInfo.imageSize, QSize(80, 60));
        QCOMPARE(coldProvider->materializations.load(), 0);

        request.info = cachedInfo;
        CachedImageRetrieveRunner cacheRunner(request, true);
        ImageReadRunner sourceRunner(request, coldProvider);
        QSignalSpy readySpy(
            &cacheRunner,
            &CachedImageRetrieveRunner::cachedThumbnailRetrieved);
        cacheRunner.run();
        sourceRunner.run();
        QCOMPARE(readySpy.size(), 1);
        QCOMPARE(coldProvider->materializations.load(), 0);

        ImageInfo changed = cachedInfo;
        changed.source.contentVersion = QStringLiteral("png-revision-2");
        changed.sourceVersionToken = QStringLiteral("png-revision-2");
        ImageInfoReadRunner changedMetadata(
            changed.source.runtimeIdentity(), true, false, false, 0, false,
            QStringLiteral("cold-changed"), changed.sourceVersionToken,
            changed.source, coldProvider);
        QSignalSpy changedSpy(
            &changedMetadata, &ImageInfoReadRunner::imageInfoReady);
        changedMetadata.run();
        QCOMPARE(changedSpy.size(), 1);
        QVERIFY(changedSpy.constFirst().at(0)
                    .value<ImageInfo>().sourceAccessFailed);
        QCOMPARE(coldProvider->materializations.load(), 1);
    }

    void metadataManifestsAreRetrievedAsOneLargeBatch() {
        constexpr int hitCount = 128;
        QList<ImageInfo> candidates;
        candidates.reserve(hitCount + 1);
        for (int index = 0; index <= hitCount; ++index) {
            ImageDecodeRequest request = requestFor(
                QStringLiteral("strong"),
                QStringLiteral("batch-revision-%1").arg(index),
                QStringLiteral("batch-resource-%1").arg(index));
            request.info.source.sourceKey =
                QStringLiteral("remote/batch/image-%1.png").arg(index);
            request.info.source.displayName =
                QStringLiteral("image-%1.png").arg(index);
            request.info.source.mimeType = QStringLiteral("image/png");
            request.info.source.size = 10'000 + index;
            request.info.fileSize = request.info.source.size;
            request.info.imageSize = QSize(640 + index, 480 + index);
            request.info.requestNamespace = QStringLiteral("batch-test");
            request.info.isLast = index == hitCount;
            if (index < hitCount) {
                PersistentDerivedImageCache::storeMetadata(request.info);
            }
            request.info.imageSize = {};
            candidates.append(request.info);
        }

        QList<ImageInfo> hits;
        QList<ImageInfo> misses;
        PersistentDerivedImageCache::retrieveMetadataBatch(
            candidates, hits, misses);

        QCOMPARE(hits.size(), hitCount);
        QCOMPARE(misses.size(), 1);
        for (int index = 0; index < hits.size(); ++index) {
            QVERIFY(hits.at(index).isCached);
            QCOMPARE(hits.at(index).imageSize,
                     QSize(640 + index, 480 + index));
            QCOMPARE(hits.at(index).requestNamespace,
                     QStringLiteral("batch-test"));
            QVERIFY(!hits.at(index).isLast);
        }
        QCOMPARE(misses.first().source.contentVersion,
                 QStringLiteral("batch-revision-128"));
        QVERIFY(misses.first().isLast);
    }

    void diskMetadataLookupDoesNotBlockCaller() {
        auto request = requestFor("strong", "async-disk-metadata");
        PersistentDerivedImageCache::storeMetadata(request.info);
        PersistentDerivedImageCache::clearMemoryMetadata();
        auto provider = QSharedPointer<CountingSourceProvider>::create();
        DecodeManager manager(nullptr, 4, provider);
        QSignalSpy ready(&manager, &DecodeManager::imagesInfoReady);
        manager.readVersionedImagesInfo({{request.info.source.runtimeIdentity(),
            request.info.sourceVersionToken, request.info.source}}, false, false, "disk-lookup");
        QCOMPARE(ready.size(), 0);
        QTRY_COMPARE(ready.size(), 1);
        QCOMPARE(provider->materializations.load(), 0);
    }

    void decodeManagerPublishesCachedMetadataAsOneBatch() {
        constexpr int imageCount = 128;
        QList<DecodeManager::VersionedImageInfoRequest> requests;
        requests.reserve(imageCount);
        for (int index = 0; index < imageCount; ++index) {
            ImageDecodeRequest request = requestFor(
                QStringLiteral("strong"),
                QStringLiteral("manager-revision-%1").arg(index),
                QStringLiteral("manager-resource-%1").arg(index));
            request.info.source.sourceKey =
                QStringLiteral("remote/manager/image-%1.png").arg(index);
            request.info.source.displayName =
                QStringLiteral("image-%1.png").arg(index);
            request.info.source.mimeType = QStringLiteral("image/png");
            request.info.source.size = 20'000 + index;
            request.info.fileSize = request.info.source.size;
            request.info.imageSize = QSize(800 + index, 600 + index);
            PersistentDerivedImageCache::storeMetadata(request.info);
            DecodeManager::VersionedImageInfoRequest infoRequest{
                request.info.source.runtimeIdentity(),
                request.info.sourceVersionToken,
                request.info.source};
            infoRequest.highPriority = index < 16;
            requests.append(std::move(infoRequest));
        }

        auto provider = QSharedPointer<CountingSourceProvider>::create();
        DecodeManager manager(nullptr, 4, provider);
        int batchSignals = 0;
        int singleSignals = 0;
        QList<ImageInfo> received;
        connect(&manager, &DecodeManager::imagesInfoReady, this,
                [&](const QList<ImageInfo> &infos) {
                    ++batchSignals;
                    received = infos;
                });
        connect(&manager, &DecodeManager::imageInfoReady, this,
                [&](const ImageInfo &) { ++singleSignals; });

        manager.readVersionedImagesInfo(
            requests, false, false, QStringLiteral("manager-batch"));

        QCOMPARE(batchSignals, 1);
        QCOMPARE(singleSignals, 0);
        QCOMPARE(received.size(), imageCount);
        QCOMPARE(provider->materializations.load(), 0);
        QCOMPARE(provider->rangeReads.load(), 0);
        QCOMPARE(std::count_if(received.cbegin(), received.cend(),
                               [](const ImageInfo &info) {
                                   return info.highPriority;
                               }),
                 16);
        QVERIFY(received.constLast().isLast);
        QCOMPARE(received.constLast().requestNamespace,
                 QStringLiteral("manager-batch"));
    }

    void decodeManagerFlushesMixedBatchAfterFinalMiss() {
        constexpr int imageCount = 128;
        constexpr int missingIndex = 37;
        QList<DecodeManager::VersionedImageInfoRequest> requests;
        requests.reserve(imageCount);
        for (int index = 0; index < imageCount; ++index) {
            ImageDecodeRequest request = requestFor(
                QStringLiteral("strong"),
                QStringLiteral("mixed-revision-%1").arg(index),
                QStringLiteral("mixed-resource-%1").arg(index));
            request.info.source.sourceKey =
                QStringLiteral("remote/mixed/image-%1.png").arg(index);
            request.info.source.displayName =
                QStringLiteral("image-%1.png").arg(index);
            request.info.source.mimeType = QStringLiteral("image/png");
            request.info.source.size = 30'000 + index;
            request.info.fileSize = request.info.source.size;
            request.info.imageSize = QSize(1000 + index, 700 + index);
            if (index != missingIndex) {
                PersistentDerivedImageCache::storeMetadata(request.info);
            }
            DecodeManager::VersionedImageInfoRequest infoRequest{
                request.info.source.runtimeIdentity(),
                request.info.sourceVersionToken,
                request.info.source};
            infoRequest.highPriority = index == missingIndex;
            requests.append(std::move(infoRequest));
        }

        auto provider = QSharedPointer<CountingSourceProvider>::create();
        DecodeManager manager(nullptr, 4, provider);
        QList<ImageInfo> cached;
        QList<ImageInfo> sourceResults;
        connect(&manager, &DecodeManager::imagesInfoReady, this,
                [&](const QList<ImageInfo> &infos) { cached = infos; });
        connect(&manager, &DecodeManager::imageInfoReady, this,
                [&](const ImageInfo &info) { sourceResults.append(info); });

        manager.readVersionedImagesInfo(
            requests, false, false, QStringLiteral("mixed-batch"));

        QCOMPARE(cached.size(), imageCount - 1);
        QVERIFY(std::none_of(cached.cbegin(), cached.cend(),
                             [](const ImageInfo &info) {
                                 return info.isLast;
                             }));
        QTRY_COMPARE_WITH_TIMEOUT(sourceResults.size(), 1, 5000);
        QVERIFY(sourceResults.first().isLast);
        QVERIFY(sourceResults.first().highPriority);
        QVERIFY(sourceResults.first().sourceAccessFailed);
        QCOMPARE(provider->materializations.load(), 1);
    }

    void weakHitSkipsSourceMaterializationWithinAuthority() {
        const ImageDecodeRequest request =
            requestFor(QStringLiteral("weakRemote"));
        persist(request, testImage(request.targetSize));

        auto provider = QSharedPointer<CountingSourceProvider>::create();
        CachedImageRetrieveRunner cacheRunner(request, true);
        ImageReadRunner sourceRunner(request, provider);
        QSignalSpy readySpy(
            &cacheRunner,
            &CachedImageRetrieveRunner::cachedThumbnailRetrieved);

        cacheRunner.run();
        sourceRunner.run();

        QCOMPARE(readySpy.count(), 1);
        QCOMPARE(provider->materializations.load(), 0);
        QCOMPARE(provider->rangeReads.load(), 0);
    }

    void opaqueVersionInvalidatesEntry() {
        const ImageDecodeRequest first =
            requestFor(QStringLiteral("strong"),
                       QStringLiteral("revision-1"));
        persist(first, testImage(first.targetSize));
        ImageDecodeRequest firstCopy = first;
        QVERIFY(!PersistentImageCache::retrieveImage(firstCopy, true)
                     .isNull());

        ImageDecodeRequest changed =
            requestFor(QStringLiteral("strong"),
                       QStringLiteral("revision-2"));
        QVERIFY(PersistentImageCache::retrieveImage(changed, true).isNull());
    }

    void weakAndSessionRevisionsUseOnlySessionDiskCache() {
        const QStringList strengths = {
            QStringLiteral("weakRemote"), QStringLiteral("session")};
        for (const QString &strength : strengths) {
            PersistentDerivedImageCache::clearSession();
            ImageDecodeRequest request = requestFor(strength);
            persist(request, testImage(request.targetSize));

            QVERIFY(!PersistentImageCache::retrieveImage(request, true)
                         .isNull());
            QCOMPARE(PersistentDerivedImageCache::persistentCacheSize(), 0);
            QVERIFY(PersistentDerivedImageCache::sessionCacheSize() > 0);

            PersistentDerivedImageCache::clearSession();
            QVERIFY(PersistentImageCache::retrieveImage(request, true)
                        .isNull());
        }
    }

    void weakAuthorityResourceIdInvalidatesEntry() {
        const ImageDecodeRequest first =
            requestFor(QStringLiteral("weak"),
                       QStringLiteral("revision-1"),
                       QStringLiteral("authority-a/resource-17"));
        const QByteArray oldPrepared = PersistentImageCache::createImageForCache(
            first, testImage(first.targetSize));
        QVERIFY(!oldPrepared.isEmpty());
        PersistentImageCache::storeImage(first.info, oldPrepared);

        ImageDecodeRequest firstCopy = first;
        QVERIFY(!PersistentImageCache::retrieveImage(firstCopy, true)
                     .isNull());

        ImageDecodeRequest newAuthority =
            requestFor(QStringLiteral("weak"),
                       QStringLiteral("revision-1"),
                       QStringLiteral("authority-b/resource-17"));
        QVERIFY(PersistentImageCache::retrieveImage(newAuthority, true)
                    .isNull());

        // A delayed store runner from the old authority cannot populate the
        // new authority's key even if source/version/tier are identical.
        PersistentImageCache::storeImage(newAuthority.info, oldPrepared);
        QVERIFY(PersistentImageCache::retrieveImage(newAuthority, true)
                    .isNull());
    }

    void clearingSessionArtifactsPreservesStrongArtifacts() {
        ImageDecodeRequest strong = requestFor(QStringLiteral("strong"));
        ImageDecodeRequest weak = requestFor(
            QStringLiteral("weak"), QStringLiteral("revision-1"),
            QStringLiteral("authority-a/resource-17"));
        persist(strong, testImage(strong.targetSize, qRgb(20, 200, 20)));
        persist(weak, testImage(weak.targetSize, qRgb(20, 20, 200)));

        QVERIFY(PersistentDerivedImageCache::persistentCacheSize() > 0);
        QVERIFY(PersistentDerivedImageCache::sessionCacheSize() > 0);
        PersistentDerivedImageCache::clearSession();

        QVERIFY(!PersistentImageCache::retrieveImage(strong, true).isNull());
        QVERIFY(PersistentImageCache::retrieveImage(weak, true).isNull());
        QVERIFY(PersistentDerivedImageCache::persistentCacheSize() > 0);
        QCOMPARE(PersistentDerivedImageCache::sessionCacheSize(), 0);
    }

    void nativeArtifactIsNeverPersisted() {
        ImageDecodeRequest native =
            requestFor(QStringLiteral("localStat"));
        native.viewerRequest = true;
        native.fitToViewerRequest = false;
        native.targetSize = native.info.imageSize;

        QVERIFY(PersistentImageCache::createImageForCache(
                    native, testImage(native.targetSize)).isEmpty());
        QCOMPARE(PersistentDerivedImageCache::cacheSize(), 0);
        QVERIFY(PersistentImageCache::retrieveImage(native, true).isNull());
    }

    void viewerFitAndThumbnailUseSeparateArtifacts() {
        const ImageDecodeRequest thumbnail =
            requestFor(QStringLiteral("strong"));
        persist(thumbnail,
                testImage(thumbnail.targetSize, qRgb(200, 20, 20)));

        ImageDecodeRequest fit = thumbnail;
        fit.viewerRequest = true;
        fit.backgroundViewerRequest = true;
        fit.fitToViewerRequest = true;
        QVERIFY(PersistentImageCache::retrieveImage(fit, true).isNull());
        persist(fit, testImage(fit.targetSize, qRgb(20, 20, 200)));

        QVERIFY(!PersistentImageCache::retrieveImage(fit, true).isNull());
        ImageDecodeRequest thumbnailCopy = thumbnail;
        QVERIFY(!PersistentImageCache::retrieveImage(thumbnailCopy, true)
                     .isNull());
    }
};

QTEST_MAIN(PersistentDerivedImageCacheTest)
#include "PersistentDerivedImageCacheTest.moc"
