#ifndef IMAGEDECODERINTERFACE_H
#define IMAGEDECODERINTERFACE_H

#include <QStringList>
#include <QImage>
#include <QSize>
#include <QVariantMap>

#include "ImageDecoderFactory.h"

namespace TinyEXIF { class EXIFInfo; }

struct ImageInfo;
struct ImageData;

class ImageDecoderInterface {
public:
    virtual ~ImageDecoderInterface() = default;
    virtual QStringList supportedFormats() = 0;
    virtual QString decoderName() const = 0;

    virtual bool readMetadata(ImageInfo& result) = 0;
    virtual bool readPreviewAndMime(ImageData &result) = 0;
    // Preview readers for formats without a full-pixel decode path can keep
    // the original preview-only I/O behavior for viewer requests.
    virtual bool supportsNativeDecode() const { return true; }
    virtual QImage decode(const QString& mimeType, const QByteArray& data, QSize targetSize) = 0;

protected:
    bool isFormatSupported(const QString &path);

    static QString formatShutterSpeed(double shutterSpeed);
    static QString convertDMSToDD(double latitudeDegrees, double latitudeMinutes, double latitudeSeconds, char latitudeDirection,
                                  double longitudeDegrees, double longitudeMinutes, double longitudeSeconds, char longitudeDirection);
    static QVariantMap readExifToMap(const TinyEXIF::EXIFInfo &exifInfo);
    static QVariantMap readTypedFileFields(const TinyEXIF::EXIFInfo &exifInfo);
};

#endif // IMAGEDECODERINTERFACE_H
