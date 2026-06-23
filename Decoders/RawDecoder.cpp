#include "RawDecoder.h"
#include "ImageFile.h"
#include "Exiftool/LensDatabase.h"

#include <libraw/libraw.h>

#include <QDebug>
#include <QFile>

#include <memory>

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

    // Note: LibRaw doesn't provide direct access to XMP data for panorama info
    return out;
}

QStringList RawDecoder::supportedFormats() {
    return RawExtensions;
}

#include <QElapsedTimer>
bool RawDecoder::readMetadata(ImageInfo &result) {
    if (!isFormatSupported(result.path)) {
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

    return true;
}

bool RawDecoder::readPreviewAndMime(ImageData &result) {
    if (!isFormatSupported(result.request.info.path)) {
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

    result.previewData.reset(reinterpret_cast<char *>(thumb->data), [=] (char *ptr) { LibRaw::dcraw_clear_mem(thumb); });
    result.previewDataSize = thumb->data_size;
    result.previewMimeType = "image/jpeg";
    result.previewUsed = QString("%1 of %2 (%3x%4 %5 %6)").arg(thumbnailIndex).arg(rawProcessor->imgdata.thumbs_list.thumbcount)
                             .arg(thumb->width).arg(thumb->height).arg(thumb->colors).arg(thumb->bits);

    return true;
}
