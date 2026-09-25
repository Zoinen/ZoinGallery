#include "ImageDecoderInterface.h"
#include "ImageFile.h"
#include "TinyEXIF.h"

#include <QDateTime>
#include <cmath>

bool ImageDecoderInterface::isFormatSupported(const QString &path) {
    return isExtensionMatch(path, supportedFormats());
}

QString ImageDecoderInterface::formatShutterSpeed(double shutterSpeed) {
    if (shutterSpeed <= 0) return "Invalid";

    if (shutterSpeed < 0.25) {
        int denominator = std::round(1.0 / shutterSpeed);
        // Check if we're close to a common shutter speed fraction
        static const int commonDenominators[] = {2, 4, 8, 15, 30, 60, 125, 250, 500, 1000, 2000, 4000, 8000};
        for (int common : commonDenominators) {
            if (std::abs(1.0 / shutterSpeed - common) < 0.05 * common) {
                denominator = common;
                break;
            }
        }
        return QString("1/%1").arg(denominator);
    } else {
        return QString::number(shutterSpeed, 'f', 1);
    }
}

QString ImageDecoderInterface::convertDMSToDD(double latitudeDegrees, double latitudeMinutes, double latitudeSeconds, char latitudeDirection,
                       double longitudeDegrees, double longitudeMinutes, double longitudeSeconds, char longitudeDirection)
{
    // Convert latitude to decimal degrees
    double lat = latitudeDegrees + latitudeMinutes / 60.0 + latitudeSeconds / 3600.0;

    // Convert longitude to decimal degrees
    double lon = longitudeDegrees + longitudeMinutes / 60.0 + longitudeSeconds / 3600.0;

    // Apply negative sign for South and West
    if (latitudeDirection == 'S' || latitudeDirection == 's')
        lat = -lat;
    if (longitudeDirection == 'W' || longitudeDirection == 'w')
        lon = -lon;

    // Round to 6 decimal places
    lat = std::round(lat * 1000000.0) / 1000000.0;
    lon = std::round(lon * 1000000.0) / 1000000.0;

    // Format the output string
    return QString("%1, %2")
        .arg(lat, 0, 'f', 6)
        .arg(lon, 0, 'f', 6);
}

QVariantMap ImageDecoderInterface::readExifToMap(const TinyEXIF::EXIFInfo &exifInfo) {
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
        out["Location"] = convertDMSToDD(
            exifInfo.GeoLocation.LatComponents.degrees, exifInfo.GeoLocation.LatComponents.minutes,
            exifInfo.GeoLocation.LatComponents.seconds, exifInfo.GeoLocation.LatComponents.direction,
            exifInfo.GeoLocation.LonComponents.degrees, exifInfo.GeoLocation.LonComponents.minutes,
            exifInfo.GeoLocation.LonComponents.seconds, exifInfo.GeoLocation.LonComponents.direction);
    }
    if (exifInfo.GPano.UsePanoramaViewer) {
        out["Panorama"] = "True";
    }
    return out;
}

QVariantMap ImageDecoderInterface::readTypedFileFields(
    const TinyEXIF::EXIFInfo &exifInfo) {
    QVariantMap fields;
    auto valid = [](double value) {
        return std::isfinite(value) && value > 0.0;
    };
    if (valid(exifInfo.ExposureTime)) {
        fields.insert(QStringLiteral("exif.exposure_time"),
                      exifInfo.ExposureTime);
    }
    if (exifInfo.ISOSpeedRatings > 0) {
        fields.insert(QStringLiteral("exif.iso"),
                      static_cast<quint32>(exifInfo.ISOSpeedRatings));
    }
    if (valid(exifInfo.FNumber)) {
        fields.insert(QStringLiteral("exif.f_number"), exifInfo.FNumber);
    }
    if (valid(exifInfo.LensInfo.FocalLengthIn35mm)) {
        fields.insert(QStringLiteral("exif.focal_length_35mm"),
                      exifInfo.LensInfo.FocalLengthIn35mm);
    }
    const double minimum = exifInfo.LensInfo.FocalLengthMin;
    const double maximum = exifInfo.LensInfo.FocalLengthMax;
    if (valid(minimum) && valid(maximum) && maximum >= minimum) {
        fields.insert(QStringLiteral("exif.lens_focal_range"),
                      QVariantMap{{QStringLiteral("min"), minimum},
                                  {QStringLiteral("max"), maximum}});
    }
    const QString camera = (QString::fromStdString(exifInfo.Make) + QLatin1Char(' ')
                            + QString::fromStdString(exifInfo.Model)).simplified();
    if (!camera.isEmpty()) {
        fields.insert(QStringLiteral("exif.camera_model"), camera);
    }
    const QString lens = (QString::fromStdString(exifInfo.LensInfo.Make)
                          + QLatin1Char(' ')
                          + QString::fromStdString(exifInfo.LensInfo.Model)).simplified();
    if (!lens.isEmpty()) {
        fields.insert(QStringLiteral("exif.lens_model"), lens);
    }
    return fields;
}
