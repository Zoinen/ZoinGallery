#pragma once

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
    const int rotation = int(frame.rotation());
    if (rotation == 90 || rotation == 270)
        size.transpose();
    return size;
}

inline QImage displayImage(const QVideoFrame &frame)
{
    QImage image = frame.toImage();
    if (image.isNull())
        return image;

    const QRect viewport = visibleViewport(frame).intersected(image.rect());
    if (!viewport.isEmpty())
        image = image.copy(viewport);

    const int rotation = int(frame.rotation());
    if (rotation != 0)
        image = image.transformed(QTransform().rotate(rotation),
                                  Qt::SmoothTransformation);
    if (frame.mirrored() || frame.surfaceFormat().isMirrored())
        image = image.mirrored(true, false);
    return image;
}

} // namespace ZoinGallery::VideoFrameGeometry
