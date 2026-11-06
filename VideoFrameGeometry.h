#pragma once

#include "VideoColorSpace.h"

#include <QImage>
#include <QRect>
#include <QTransform>
#include <QVideoFrame>

namespace ZoinGallery::VideoFrameGeometry {

inline QRect visibleViewport(const QVideoFrame &frame)
{
    const QRect bounds(QPoint(0, 0), frame.size());
    const QRect viewport = frame.surfaceFormat().viewport().intersected(bounds);
    return viewport.isEmpty() ? bounds : viewport;
}

inline QSize displaySize(const QVideoFrame &frame)
{
    QSize size = visibleViewport(frame).size();
    const int rotation = int(frame.surfaceFormat().rotation()) + int(frame.rotation());
    if (rotation % 180 != 0)
        size.transpose();
    return size;
}

inline QImage displayImage(const QVideoFrame &frame)
{
    QImage image = VideoColorSpace::normalizeHdrImage(frame.toImage(), frame);
    if (image.isNull())
        return image;

    // toImage() already applies the surface transform, but not the frame's
    // presentation transform. Map the coded viewport into that image before
    // cropping, otherwise a quarter turn crops a portrait into a square.
    const auto format = frame.surfaceFormat();
    const QTransform surface = QTransform().scale(1,
        format.scanLineDirection() == QVideoFrameFormat::BottomToTop ? -1 : 1)
        * QTransform().rotate(int(format.rotation()))
        * QTransform().scale(format.isMirrored() ? -1 : 1, 1);
    const QRect viewport = QImage::trueMatrix(surface, frame.width(), frame.height())
        .mapRect(visibleViewport(frame)).intersected(image.rect());
    if (!viewport.isEmpty())
        image = image.copy(viewport);

    const int rotation = int(frame.rotation());
    if (rotation != 0)
        image = image.transformed(QTransform().rotate(rotation),
                                  Qt::SmoothTransformation);
    if (frame.mirrored())
        image = image.mirrored(true, false);
    return image;
}

} // namespace ZoinGallery::VideoFrameGeometry
