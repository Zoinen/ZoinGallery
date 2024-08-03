#include "JpegDecoder.h"

#include <turbojpeg.h>

#include <QDebug>

static const QStringList JpegExtensions = {"jpg", "jpeg", "jpe", "jfif"};

QStringList JpegDecoder::supportedFormats() {
    return JpegExtensions;
}

bool JpegDecoder::canDecode(const QString& mimeType) {
    return mimeType == "image/jpeg";
}

QImage JpegDecoder::decode(const QByteArray &data, QSize targetSize) {
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
    denomIndex--;
    denomIndex = qBound(0, denomIndex, num - 1);

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
    //    img.save(QString("c:\\temp\\%1").arg(QFileInfo(path).fileName()));
    return img;
}
