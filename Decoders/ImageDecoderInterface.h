#ifndef IMAGEDECODERINTERFACE_H
#define IMAGEDECODERINTERFACE_H

#include <QStringList>
#include <QImage>
#include <QSize>

#include "ImageDecoderFactory.h"

struct ImageInfo;
struct ImageData;

class ImageDecoderInterface {
public:
    virtual ~ImageDecoderInterface() = default;
    virtual QStringList supportedFormats() = 0;
    virtual QString decoderName() const = 0;

    virtual bool readMetadata(ImageInfo& result) = 0;
    virtual bool readPreviewAndMime(ImageData &result) = 0;
    virtual QImage decode(const QString& mimeType, const QByteArray& data, QSize targetSize) = 0;

protected:
    bool isFormatSupported(const QString &path);

    static QString formatShutterSpeed(double shutterSpeed);
    static QString convertDMSToDD(double latitudeDegrees, double latitudeMinutes, double latitudeSeconds, char latitudeDirection,
                                  double longitudeDegrees, double longitudeMinutes, double longitudeSeconds, char longitudeDirection);
};

#endif // IMAGEDECODERINTERFACE_H
