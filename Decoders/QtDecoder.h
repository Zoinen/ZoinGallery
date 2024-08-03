#ifndef QTIMAGEDECODER_H
#define QTIMAGEDECODER_H

#include "ImageDecoder.h"

class QtDecoder : public ImageDecoder {
public:
    void init() override;
    QStringList supportedFormats() override;

    bool canDecode(const QString& mimeType) override;
    QImage decode(const QByteArray& data, QSize targetSize) override;

private:
    static QStringList _formats;
};

#endif // QTIMAGEDECODER_H
