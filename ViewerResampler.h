#pragma once

#include <QImage>
#include <QSize>
#include <functional>

class QThreadPool;

namespace ZoinGallery::ViewerResampler {

// Prepare a viewer tier in physical pixels; never upscale either axis.
// Equal-size requests share the original image without altering its pixels.
// Reduction uses the viewer's sRGB sampling transfer and ordered dither;
// existing color-space tags and image metadata are preserved without conversion.
// With a helper pool, the cancellation callback may run on several threads.
QImage downsample(const QImage &image, QSize targetSize,
                  const std::function<bool()> &isCanceled = {},
                  QThreadPool *helperPool = nullptr);

}
