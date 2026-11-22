#pragma once

#include "ImageDecoderInterface.h"
#include "ImageFile.h"

struct heif_image;
struct heif_image_handle;
QImage copyHeifPixels(const heif_image *image, bool hasAlpha);
QImage decodeHeifHandle(heif_image_handle *handle);

class HeicDecoder : public ImageDecoderInterface {
    REGISTER_DECODER_DECLARATION(HeicDecoder, 2)
public:
    QStringList supportedFormats() override;
    bool readMetadata(ImageInfo& result) override;
    bool readPreviewAndMime(ImageData& result) override;
    QImage decode(const QString& mimeType, const QByteArray& data, QSize targetSize) override;
};
