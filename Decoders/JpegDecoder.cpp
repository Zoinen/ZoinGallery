#include "JpegDecoder.h"

#include <QFile>
#include <QDebug>

#include <iostream>
#include "tinyexif.h"
#include <turbojpeg.h>

REGISTER_DECODER_DEFINITION(JpegDecoder)

static const QStringList JpegExtensions = {"jpg", "jpeg", "jpe", "jfif"};

QSize getJPEGSize(const uchar* mappedFile, qint64 fileSize)
{
    if (fileSize < 2 || mappedFile[0] != 0xFF || mappedFile[1] != 0xD8) {
        return QSize(0, 0);  // Not a valid JPEG file
    }

    qint64 pos = 2;
    while (pos + 9 < fileSize) {  // Need at least 9 bytes for marker, size, and dimensions
        if (mappedFile[pos] != 0xFF) {
            return QSize(0, 0);  // Invalid marker
        }

        uchar marker = mappedFile[pos + 1];
        if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8) {
            // Found Start of Frame marker
            quint16 height = (mappedFile[pos + 5] << 8) | mappedFile[pos + 6];
            quint16 width = (mappedFile[pos + 7] << 8) | mappedFile[pos + 8];
            return QSize(width, height);
        }

        // Move to the next marker
        quint16 segmentLength = (mappedFile[pos + 2] << 8) | mappedFile[pos + 3];
        pos += 2 + segmentLength;
    }

    return QSize(0, 0);  // Couldn't find resolution information
}


bool JpegDecoder::readMetadata(ImageInfo &result) {
    if (!isFormatSupported(result.path)) {
        return false;
    }

    QFile f(result.path);
    if (!f.open(QFile::ReadOnly)) {
        return false;
    }
    uchar *mappedFile = f.map(0, f.size());
    if (!mappedFile) {
        f.unmap(mappedFile);
        return false;
    }

    TinyEXIF::EXIFInfo exifInfo;
    if (exifInfo.parseFrom(mappedFile, f.size()) != TinyEXIF::PARSE_SUCCESS) {
        f.unmap(mappedFile);
        return false;
    }

    result.orientation = readOrientationFromExif(exifInfo);
    // result.imageSize = readResolutionFromExif(exifInfo);
    result.imageSize = getJPEGSize(mappedFile, f.size());
    result.exif = readExifToMap(exifInfo);
    result.exif["Size"] = f.size();

    f.unmap(mappedFile);
    return true;
}

ExifOrientation JpegDecoder::readOrientationFromExif(const TinyEXIF::EXIFInfo &exifInfo) {
    return static_cast<ExifOrientation>(exifInfo.Orientation);
}

QSize JpegDecoder::readResolutionFromExif(const TinyEXIF::EXIFInfo &exifInfo) {
    return QSize(exifInfo.ImageWidth, exifInfo.ImageHeight);
}

QVariantMap JpegDecoder::readExifToMap(const TinyEXIF::EXIFInfo &exifInfo) {
    QVariantMap out;
    if (!exifInfo.DateTimeOriginal.empty()) {
        out["DateTime"] = QDateTime::fromString(QString::fromStdString(exifInfo.DateTimeOriginal), "yyyy:MM:dd hh:mm:ss");
    }
    if (exifInfo.ExposureTime) {
        out["ShutterSpeed"] = formatShutterSpeed(exifInfo.ExposureTime);
    }
    if (exifInfo.FNumber) {
        out["FNumber"] = QString::number(exifInfo.FNumber);
    }
    if (exifInfo.ISOSpeedRatings) {
        out["ISO"] = QString::number(exifInfo.ISOSpeedRatings);
    }
    if (!exifInfo.Make.empty() || !exifInfo.Model.empty()) {
        out["Camera"] = QString::fromStdString(exifInfo.Make) + " " + QString::fromStdString(exifInfo.Model);
    }
    if (exifInfo.LensInfo.FocalLengthIn35mm) {
        out["FocalLength"] = QString::number(exifInfo.LensInfo.FocalLengthIn35mm);
    }
    if (!exifInfo.LensInfo.Make.empty() || !exifInfo.LensInfo.Model.empty()) {
        out["Lens"] = QString::fromStdString(exifInfo.LensInfo.Make) + " " + QString::fromStdString(exifInfo.LensInfo.Model);
    }

    if (exifInfo.GeoLocation.hasLatLon()) {
        out["Location"] = convertDMSToDD(exifInfo.GeoLocation.LatComponents.degrees, exifInfo.GeoLocation.LatComponents.minutes,
                                         exifInfo.GeoLocation.LatComponents.seconds, exifInfo.GeoLocation.LatComponents.direction,
                                         exifInfo.GeoLocation.LonComponents.degrees, exifInfo.GeoLocation.LonComponents.minutes,
                                         exifInfo.GeoLocation.LonComponents.seconds, exifInfo.GeoLocation.LonComponents.direction);
    }

    if (exifInfo.GPano.UsePanoramaViewer) {
        out["Panorama"] = "True";
    }

    return out;
}

QStringList JpegDecoder::supportedFormats() {
    return JpegExtensions;
}

QImage JpegDecoder::decode(const QString& mimeType, const QByteArray &data, QSize targetSize) {
    if (mimeType != "image/jpeg") {
        return QImage();
    }

    const unsigned char* _compressedImage = reinterpret_cast<const uint8_t *>(data.constData());
    long unsigned int _jpegSize = data.size();

    int jpegSubsamp, jpegColorspace, width, height;

    tjhandle _jpegDecompressor = tjInitDecompress();

    int res = tjDecompressHeader3(_jpegDecompressor, _compressedImage, _jpegSize, &width, &height, &jpegSubsamp, &jpegColorspace);
    if (res == -1) {
        if (tjGetErrorCode(_jpegDecompressor) == TJERR_FATAL) {
            qDebug() << "JPEG header decode error:" << tjGetErrorStr2(_jpegDecompressor) << "," << _compressedImage << _jpegSize << width << height;
            tjDestroy(_jpegDecompressor);
            return QImage();
        }
    }

    int num = 0;
    tjscalingfactor *factors = tjGetScalingFactors(&num);
    int denomIndex = 0;
    for (; denomIndex < num; denomIndex++) {
        int scaledWidth = TJSCALED(width, factors[denomIndex]);
        int scaledHeight = TJSCALED(height, factors[denomIndex]);
        //qDebug() << denomIndex << scaledWidth << "x" << scaledHeight;
        if (scaledWidth < targetSize.width() || scaledHeight < targetSize.height()) {
            denomIndex--;
            break;
        }
    }
    denomIndex = qBound(0, denomIndex, num - 1);

    // Higher quality due to increased base resolution
    // denomIndex--;
    // denomIndex = qBound(0, denomIndex, num - 1);

    width = TJSCALED(width, factors[denomIndex]);
    height = TJSCALED(height, factors[denomIndex]);
    //    qDebug() << "Chosen" << denomIndex << "of" << num << width << "x" << height << ", req" << targetSize;

    int pitch = TJPAD(width * tjPixelSize[TJPF_RGB]);
    unsigned char *buffer = new unsigned char[pitch * height];

    res = tjDecompress2(_jpegDecompressor, _compressedImage, _jpegSize, buffer, width, pitch, height, TJPF_RGB, TJFLAG_FASTDCT);
    if (res == -1) {
        if (tjGetErrorCode(_jpegDecompressor) == TJERR_FATAL) {
            qDebug() << "JPEG decode error:" << tjGetErrorStr2(_jpegDecompressor) << ",";
            delete[] buffer;
            tjDestroy(_jpegDecompressor);
            return QImage();
        }
    }
    tjDestroy(_jpegDecompressor);

    QImage img(buffer, width, height, QImage::Format_RGB888, [] (void *ptr) {delete[] (uint8_t *)ptr;}, buffer);
    /*int taken3 = t.restart();
    qDebug() << "time:" << path << taken << taken2 << taken3;*/
    // static int index = 0;
    // img.save(QString("c:\\tmp\\%1.png").arg(index++));
    return img;
}
