#pragma once

#include <QImage>
#include <QString>

#include <functional>
#include <memory>

namespace ZoinGallery {

enum class PageError { None, CannotRead, PasswordRequired, EndOfSequence };

struct ImagePage {
    QImage pixels;
    QSizeF size;
    int delayMs = 0;
    PageError error = PageError::None;
};

// The device and decoder stay on the worker that opened the document. Sizes
// describe the oriented page, independent of the requested raster resolution.
class PagedImageDocument {
public:
    using Canceled = std::function<bool()>;

    virtual ~PagedImageDocument() = default;
    virtual int pageCount() const = 0;
    virtual QSizeF pageSize(int index) = 0;
    virtual ImagePage render(int index, QSize target, QRect clip, const Canceled &canceled) = 0;
    virtual bool scalable() const { return false; }
    // -1 means infinite; zero means one pass. Other values count repeats.
    virtual int repeats() const { return 0; }
    virtual bool animated() const { return false; }

    static bool canOpen(const QString &name);
    static bool isPdf(const QString &name);
    static bool usesPageThumbnail(const QString &name);
    static std::unique_ptr<PagedImageDocument> open(const QString &path, const QString &name,
                                                   PageError &error, bool interactive = false);
};

QSize boundedPageRaster(QSizeF size, QSize target, bool allowUpscale);
bool validPageRaster(QSize size);

std::unique_ptr<PagedImageDocument> openWebpPages(const QString &path);
std::unique_ptr<PagedImageDocument> openAvifPages(const QString &path);
std::unique_ptr<PagedImageDocument> openHeifPages(const QString &path);
std::unique_ptr<PagedImageDocument> openRawPages(const QString &path);

} // namespace ZoinGallery
