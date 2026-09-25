#include "PersistentDerivedImageCache.h"
#include "PersistentImageCache.h"
#include "Decoders/ImageDecoderInterface.h"
#include "Decoders/RawDecoder.h"
#include "Runners/CacheImageRunners.h"
#include "Runners/ImageInfoReadRunner.h"
#include "Runners/ImageReadRunner.h"
#include "StorageLocations.h"
#include "TinyEXIF/TinyEXIF.h"

#include <QFileInfo>
#include <QColorSpace>
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
#include <utility>

namespace {

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

    void viewerFitCachePreservesEncodedPixels() {
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
        const QImage cached = PersistentImageCache::retrieveImage(request, true)
                                  .convertToFormat(QImage::Format_RGBA8888);
        QCOMPARE(cached.size(), image.size());
        QCOMPARE(cached.colorSpace(), image.colorSpace());
        int changedPixels = 0;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                changedPixels += cached.pixel(x, y) != image.pixel(x, y);
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
        persist(first, testImage(first.targetSize));

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
        const QByteArray oldPrepared =
            PersistentImageCache::createImageForCache(
                first, testImage(first.targetSize));
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
