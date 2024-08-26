#ifndef METADATAREADER_H
#define METADATAREADER_H

#include <QString>

struct ImageInfo;
struct ImageData;

class MetadataReader {
public:
    virtual ~MetadataReader() = default;
    virtual bool readMetadata(ImageInfo& result) = 0;
    virtual bool readPreviewAndMime(ImageData &result) = 0;
    virtual bool isFormatSupported(const QString &path) = 0;

protected:
    static QString formatShutterSpeed(double shutterSpeed);
    static QString convertDMSToDD(double latitudeDegrees, double latitudeMinutes, double latitudeSeconds, char latitudeDirection,
                                  double longitudeDegrees, double longitudeMinutes, double longitudeSeconds, char longitudeDirection);
};

#endif // METADATAREADER_H
