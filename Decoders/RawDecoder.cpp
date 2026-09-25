#include "RawDecoder.h"
#include "RawBitmapPreview.h"
#include <QBuffer>
#include "ImageFile.h"
#include "Exiftool/LensDatabase.h"

#include <libraw/libraw.h>

#include <QDebug>

#include <memory>
#include <cmath>
#include <string>

REGISTER_DECODER_DEFINITION(RawDecoder)

static const QStringList RawExtensions = {"dng", "crw", "cr2", "cr3", "nef", "nrw", "arw", "arq", "sr2", "srf", "raf",
                                          "orf", "rw2", "pef", "rwl", "x3f", "3fr", "fff", "mef", "srw", "dcr", "k25",
                                          "kdc", "erf", "mos", "iiq", "bay", "cs1"};

ExifOrientation RawDecoder::readOrientationFromExif(LibRaw &rawProcessor) {
    ExifOrientation orientation = Horizontal;

    // LibRaw stores orientation in imgdata.sizes.flip
    switch (rawProcessor.imgdata.sizes.flip) {
    case 0: orientation = Horizontal; break;
    case 3: orientation = Rotate180; break;
    case 5: orientation = Rotate270CW; break;  // 90 deg counterclockwise
    case 6: orientation = Rotate90CW; break;   // 90 deg clockwise
    default: orientation = Horizontal; break;
    }
    return orientation;
}

QVariantMap RawDecoder::readExifToMap(LibRaw &rawProcessor) {
    QVariantMap out;

    // Date and Time
    if (rawProcessor.imgdata.other.timestamp) {
        out["DateTime"] = QDateTime::fromSecsSinceEpoch(rawProcessor.imgdata.other.timestamp);
    }

    // Shooting info
    out["ShutterSpeed"] = formatShutterSpeed(rawProcessor.imgdata.other.shutter);
    out["FNumber"] = QString::number(rawProcessor.imgdata.other.aperture);
    out["ISO"] = QString::number(rawProcessor.imgdata.other.iso_speed);

    // Camera info
    QString cameraModel;
    if (rawProcessor.imgdata.idata.make[0]) {
        cameraModel = QString(rawProcessor.imgdata.idata.make);
    }
    if (rawProcessor.imgdata.idata.model[0]) {
        if (!cameraModel.isEmpty()) {
            cameraModel += " ";
        }
        cameraModel += QString(rawProcessor.imgdata.idata.model);
    }
    if (!cameraModel.isEmpty()) {
        out["Camera"] = cameraModel;
    }

    // Max Focal Length                : 200 mm
    // Min Focal Length

    // Lens info
    if (rawProcessor.imgdata.lens.makernotes.FocalLengthIn35mmFormat) {
        out["FocalLength"] = "EFR " + QString::number(rawProcessor.imgdata.lens.makernotes.FocalLengthIn35mmFormat);
    }
    else if (rawProcessor.imgdata.other.focal_len != 0.0) {
        out["FocalLength"] = QString::number(rawProcessor.imgdata.other.focal_len);
    }

    if (rawProcessor.imgdata.lens.Lens[0]) {
        out["Lens"] = QString(rawProcessor.imgdata.lens.Lens);
    }
    else if (rawProcessor.imgdata.lens.makernotes.Lens[0]) {
        out["Lens"] = QString(rawProcessor.imgdata.lens.makernotes.Lens);
    }
    else {
        // Construct basic lens information using makernotes
        QString lensInfo;

        if (rawProcessor.imgdata.lens.nikon.LensIDNumber) {
            lensInfo = LensDatabase::lensNameForId(rawProcessor.imgdata.lens.makernotes.LensID);
        }
        if (lensInfo.isEmpty()) {
            // uint8_t nikonRawLens[8] = [nikon];

            if (rawProcessor.imgdata.lens.makernotes.MinFocal > 0 && rawProcessor.imgdata.lens.makernotes.MaxFocal > 0) {
                lensInfo += QString("%1-%2mm")
                                .arg(rawProcessor.imgdata.lens.makernotes.MinFocal)
                                .arg(rawProcessor.imgdata.lens.makernotes.MaxFocal);
            }
            else if (rawProcessor.imgdata.lens.makernotes.MinFocal > 0) {
                lensInfo += QString("%1mm").arg(rawProcessor.imgdata.lens.makernotes.MinFocal);
            }

            if (rawProcessor.imgdata.lens.makernotes.MaxAp4MinFocal > 0 && rawProcessor.imgdata.lens.makernotes.MaxAp4MaxFocal > 0) {
                lensInfo += QString(" F%1-%2")
                                .arg(rawProcessor.imgdata.lens.makernotes.MaxAp4MinFocal)
                                .arg(rawProcessor.imgdata.lens.makernotes.MaxAp4MaxFocal);
            }
            else if (rawProcessor.imgdata.lens.makernotes.MaxAp4MinFocal > 0) {
                lensInfo += QString(" F%1").arg(rawProcessor.imgdata.lens.makernotes.MaxAp4MinFocal);
            }
            else if (rawProcessor.imgdata.lens.makernotes.MaxAp > 0) {
                lensInfo += QString(" F%1").arg(rawProcessor.imgdata.lens.makernotes.MaxAp, 0, 'g', 3);
            }

            if (!lensInfo.isEmpty()) {
                out["Lens"] = "Unknown " + lensInfo;
            }
        }
        else {
            out["Lens"] = lensInfo;
        }
    }

    // GPS info
    if (rawProcessor.imgdata.other.parsed_gps.latitude[0]) {
        auto gps = rawProcessor.imgdata.other.parsed_gps;
        out["Location"] = convertDMSToDD(gps.latitude[0], gps.latitude[1], gps.latitude[2], gps.latref,
                                         gps.longitude[0], gps.longitude[1], gps.longitude[2], gps.longref);
    }

    return out;
}

QVariantMap RawDecoder::readTypedFileFields(LibRaw &rawProcessor) {
    QVariantMap fields;
    auto valid = [](double value) {
        return std::isfinite(value) && value > 0.0;
    };
    const auto &other = rawProcessor.imgdata.other;
    const auto &make = rawProcessor.imgdata.idata.make;
    const auto &model = rawProcessor.imgdata.idata.model;
    if (valid(other.shutter)) {
        fields["exif.exposure_time"] = other.shutter;
    }
    if (valid(other.iso_speed)) {
        fields["exif.iso"] = static_cast<quint32>(std::lround(other.iso_speed));
    }
    if (valid(other.aperture)) {
        fields["exif.f_number"] = other.aperture;
    }
    // LibRaw stores the standard EXIF FocalLengthIn35mmFilm tag on lensinfo;
    // the similarly named makernotes member is only for vendor metadata.
    if (valid(rawProcessor.imgdata.lens.FocalLengthIn35mmFormat)) {
        fields["exif.focal_length_35mm"] =
            rawProcessor.imgdata.lens.FocalLengthIn35mmFormat;
    } else if (valid(rawProcessor.imgdata.lens.makernotes.FocalLengthIn35mmFormat)) {
        fields["exif.focal_length_35mm"] =
            rawProcessor.imgdata.lens.makernotes.FocalLengthIn35mmFormat;
    }
    const double minFocal = rawProcessor.imgdata.lens.makernotes.MinFocal;
    const double maxFocal = rawProcessor.imgdata.lens.makernotes.MaxFocal;
    if (valid(minFocal) && valid(maxFocal) && maxFocal >= minFocal) {
        fields["exif.lens_focal_range"] =
            QVariantMap{{QStringLiteral("min"), minFocal},
                        {QStringLiteral("max"), maxFocal}};
    }
    const QString camera = (QString(make) + QLatin1Char(' ')
                            + QString(model)).simplified();
    if (!camera.isEmpty()) {
        fields["exif.camera_model"] = camera;
    }
    QString lensModel;
    if (rawProcessor.imgdata.lens.Lens[0]) {
        lensModel = QString(rawProcessor.imgdata.lens.Lens).trimmed();
    } else if (rawProcessor.imgdata.lens.makernotes.Lens[0]) {
        lensModel = QString(rawProcessor.imgdata.lens.makernotes.Lens).trimmed();
    }
    if (!lensModel.isEmpty()) {
        fields["exif.lens_model"] = lensModel;
    }

    return fields;
}

QStringList RawDecoder::supportedFormats() {
    return RawExtensions;
}

#include <QElapsedTimer>
bool RawDecoder::readMetadata(ImageInfo &result) {
    if (!isFormatSupported(result.formatHint())) {
        return false;
    }

    auto rawProcessor = std::make_unique<LibRaw>();

    // ZZZZ: THIS ONE SHOULD BE FAST BUT IT'S NOT SINCE IT DOESN'T CHECK FOR FILE FORMAT
    // Open the CR3 file
#if defined(Q_OS_WIN)
    if (rawProcessor->open_file(result.path.toStdWString().c_str()) != LIBRAW_SUCCESS) {
#else
    if (rawProcessor->open_file(result.path.toUtf8().constData()) != LIBRAW_SUCCESS) {
#endif
        // ZOIN ZOIN ZOIN
        // DONT COMMIT THIS
        // IT SHOULDNT HAPPEN THAT OFTEN
        qDebug() << "Failed to open file: " << result.path;
        return false;
    }

    const libraw_thumbnail_t &thumbnailInfo = rawProcessor->imgdata.thumbnail;
    result.imageSize = QSize(thumbnailInfo.twidth, thumbnailInfo.theight);

    result.orientation = readOrientationFromExif(*rawProcessor);
    result.exif = readExifToMap(*rawProcessor);
    result.typedFileFields = readTypedFileFields(*rawProcessor);

    return true;
}

bool RawDecoder::readPreviewAndMime(ImageData &result) {
    if (!isFormatSupported(result.request.info.formatHint())) {
        return false;
    }

    auto rawProcessor = std::make_unique<LibRaw>();

    // Open the CR3 file from memory buffer
#if defined(Q_OS_WIN)
    if (rawProcessor->open_file(result.request.info.path.toStdWString().c_str()) != LIBRAW_SUCCESS) {
#else
    if (rawProcessor->open_file(result.request.info.path.toUtf8().constData()) != LIBRAW_SUCCESS) {
#endif
        // ZOIN ZOIN ZOIN
        // DONT COMMIT THIS
        // IT SHOULDNT HAPPEN THAT OFTEN
        qCritical() << "Failed to open buffer 2";
        return false;
    }

    QSize targetSize = result.request.targetSize;
    if (!result.request.checkCache) {
        targetSize = expandToCacheImageResolution(targetSize);
    }
    QSize preferredSizeRotated = rotateToOrientation(targetSize, result.request.info.orientation);

    int thumbnailIndex = -1;
    for (int i = 0; i < rawProcessor->imgdata.thumbs_list.thumbcount; i++) {
        auto item = rawProcessor->imgdata.thumbs_list.thumblist[i];
        if (item.twidth >= preferredSizeRotated.width() && item.theight >= preferredSizeRotated.height()) {
            thumbnailIndex = i;
            break;
        }
    }

    if (thumbnailIndex != -1) {
        // Unpack the thumbnail (or preview)
        if (rawProcessor->unpack_thumb_ex(thumbnailIndex) != LIBRAW_SUCCESS) {
            qCritical() << "Failed to unpack thumbnail #" << thumbnailIndex << "from buffer";
            return false;
        }
    }
    else {
        // Unpack the thumbnail (or preview)
        if (rawProcessor->unpack_thumb() != LIBRAW_SUCCESS) {
            qCritical() << "Failed to unpack thumbnail from buffer";
            return false;
        }
    }

    // Access the thumbnail in memory
    libraw_processed_image_t *thumb = rawProcessor->dcraw_make_mem_thumb();
    if (!thumb) {
        qCritical() << "Failed to extract thumbnail to memory";
        return false;
    }

    result.previewUsed = QString("%1 of %2 (%3x%4 %5 %6)").arg(thumbnailIndex).arg(rawProcessor->imgdata.thumbs_list.thumbcount)
                             .arg(thumb->width).arg(thumb->height).arg(thumb->colors).arg(thumb->bits);
    if (thumb->type == LIBRAW_IMAGE_BITMAP) {
        const QImage bitmap = rawBitmapPreview(thumb->data, thumb->data_size,
            thumb->width, thumb->height, thumb->colors, thumb->bits);
        LibRaw::dcraw_clear_mem(thumb);
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        if (bitmap.isNull() || !bitmap.save(&buffer, "PNG")) return false;
        auto storage = std::make_shared<QByteArray>(std::move(png));
        result.previewData = std::shared_ptr<char>(storage, storage->data());
        result.previewDataSize = storage->size();
        result.previewMimeType = "image/png";
    } else if (thumb->type == LIBRAW_IMAGE_JPEG) {
        result.previewData.reset(reinterpret_cast<char *>(thumb->data), [thumb](char *) { LibRaw::dcraw_clear_mem(thumb); });
        result.previewDataSize = thumb->data_size;
        result.previewMimeType = "image/jpeg";
    } else {
        LibRaw::dcraw_clear_mem(thumb);
        return false;
    }

    return true;
}
