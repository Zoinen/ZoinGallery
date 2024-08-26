#include "MetadataReader.h"

QString MetadataReader::formatShutterSpeed(double shutterSpeed) {
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

QString MetadataReader::convertDMSToDD(double latitudeDegrees, double latitudeMinutes, double latitudeSeconds, char latitudeDirection,
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
