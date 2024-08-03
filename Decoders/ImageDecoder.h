#ifndef IMAGEDECODER_H
#define IMAGEDECODER_H

#include <QStringList>
#include <QImage>
#include <QSize>

class ImageDecoder {
public:
    virtual ~ImageDecoder() = default;
    virtual void init() {}
    virtual QStringList supportedFormats() = 0;

    virtual bool canDecode(const QString& mimeType) = 0;
    virtual QImage decode(const QByteArray& data, QSize targetSize) = 0;
};

#endif // IMAGEDECODER_H
