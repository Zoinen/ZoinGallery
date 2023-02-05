#ifndef PREVIEW_H
#define PREVIEW_H

#include <exiv2/image.hpp>
#include <exiv2/preview.hpp>

class Exiv2Preview {
public:
    static Exiv2::DataBuf preview(const Exiv2::Image& image, int targetWidth, int targetHeight,
                                  Exiv2::PreviewProperties *outPreviewProperties, int ignoreThumbnailAt = -1,
                                  bool *outLargerImageAvailable = nullptr);
};

#endif // PREVIEW_H
