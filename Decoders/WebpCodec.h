#pragma once

#include <QByteArray>
#include <QImage>
#include <QSize>

namespace WebpCodec {

struct Features {
    QSize size;
    bool hasAlpha = false;
    bool hasAnimation = false;

    bool isValid() const { return size.width() > 0 && size.height() > 0; }
};

Features readFeatures(const QByteArray &data);
QImage decode(const QByteArray &data, const QSize &targetSize = {});
QByteArray encode(const QImage &image, float quality);

}
