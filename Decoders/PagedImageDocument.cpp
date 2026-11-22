#include "PagedImageDocument.h"

#include "RawDecoder.h"
#include "HeicDecoder.h"
#include "ThumbnailLoader.h"

#include <QColorSpace>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QPainter>
#include <QSet>
#include <QtEndian>
#include <QtMath>
#include <tiffio.h>

#include <cmath>

#if defined(ZOIN_ENABLE_PDF)
#include <QMutex>
#include <QPdfDocument>
#include <QPdfDocumentRenderOptions>
#include <QWaitCondition>
#endif

namespace ZoinGallery {
namespace {

constexpr int MaxRasterEdge = 16384;
constexpr qint64 MaxRasterPixels = 32 * 1024 * 1024;

const QStringList &rawExtensions()
{
    static const QStringList formats = RawDecoder().supportedFormats();
    return formats;
}

const QStringList &heifExtensions()
{
    static const QStringList formats = HeicDecoder().supportedFormats();
    return formats;
}

const QSet<QByteArray> &qtPagedFormats()
{
    static const QSet<QByteArray> formats = [] {
        const auto registered = QImageReader::supportedImageFormats();
        QSet<QByteArray> result(registered.cbegin(), registered.cend());
        // Dedicated codecs handle these formats. Explicitly single-image
        // handlers keep their existing thumbnail and viewer paths.
        for (const auto *format : {"pdf", "png", "jpg", "jpeg", "jpe", "jfif", "svg", "svgz",
                                   "bmp", "dib", "pbm", "pgm", "ppm", "xbm", "xpm", "wbmp"})
            result.remove(format);
        return result;
    }();
    return formats;
}

class QtImagePages final : public PagedImageDocument {
public:
    QtImagePages(const QString &path, const QByteArray &format)
        : _path(path), _format(format)
    {
        resetReader();
        _count = _reader->imageCount();
        if (_count < 0)
            _count = 0;
        _animated = _count > 1 && _reader->supportsAnimation();
        if (_animated)
            _repeats = _reader->loopCount();
        if (_count == 0 && _reader->canRead())
            _count = 1;
    }

    int pageCount() const override { return _count; }
    bool animated() const override { return _animated; }
    int repeats() const override { return _repeats; }

    QSizeF pageSize(int index) override
    {
        if (_animated)
            return _last.isNull() ? _reader->size() : _last.size();
        if (!_reader->jumpToImage(index)) {
            if (index != 0)
                return {};
            resetReader();
        }
        return _reader->size();
    }

    ImagePage render(int index, QSize target, QRect, const Canceled &canceled) override
    {
        if (index < 0 || index >= _count)
            return {{}, {}, 0, PageError::EndOfSequence};
        if (_animated) {
            if (index < _lastIndex)
                resetReader();
            while (_lastIndex < index && !canceled()) {
                QImage frame = _reader->read();
                if (frame.isNull())
                    return {{}, {}, 0, PageError::CannotRead};
                _last = frame;
                _delay = _reader->nextImageDelay();
                ++_lastIndex;
            }
        } else if (!canceled()) {
            _last = {};
            if (_reader->jumpToImage(index)) {
                _last = _reader->read();
            } else {
                resetReader();
                for (int frame = 0; frame <= index && !canceled(); ++frame) {
                    _last = _reader->read();
                    if (_last.isNull())
                        break;
                }
            }
        }
        if (_last.isNull() || canceled())
            return {{}, {}, 0, PageError::CannotRead};
        const QSize size = _last.size();
        return {ThumbnailLoader::createViewerImage(_last, boundedPageRaster(size, target, false)),
                size, _delay};
    }

private:
    void resetReader()
    {
        _reader = std::make_unique<QImageReader>(_path);
        _reader->setAutoTransform(true);
        if (!_format.isEmpty())
            _reader->setFormat(_format);
        _lastIndex = -1;
        _last = {};
    }

    QString _path;
    QByteArray _format;
    std::unique_ptr<QImageReader> _reader;
    QImage _last;
    int _lastIndex = -1;
    int _delay = 0;
    int _count = 0;
    int _repeats = 0;
    bool _animated = false;
};

class TiffPages final : public PagedImageDocument {
public:
    explicit TiffPages(const QString &path)
    {
#ifdef Q_OS_WIN
        _tiff.reset(TIFFOpenW(reinterpret_cast<const wchar_t *>(path.utf16()), "r"));
#else
        _tiff.reset(TIFFOpen(QFile::encodeName(path).constData(), "r"));
#endif
        if (!_tiff)
            return;
        do {
            uint32_t kind = 0;
            TIFFGetField(_tiff.get(), TIFFTAG_SUBFILETYPE, &kind);
            if ((kind & (FILETYPE_REDUCEDIMAGE | FILETYPE_MASK)) == 0)
                _pages.append(TIFFCurrentDirectory(_tiff.get()));
        } while (TIFFReadDirectory(_tiff.get()));
    }

    int pageCount() const override { return int(_pages.size()); }

    QSizeF pageSize(int index) override
    {
        if (!select(index))
            return {};
        ExifOrientation orientation;
        const QSize size = dimensions(orientation);
        return rotateToOrientation(size, orientation);
    }

    ImagePage render(int index, QSize target, QRect, const Canceled &canceled) override
    {
        if (!select(index))
            return {{}, {}, 0, PageError::CannotRead};
        ExifOrientation orientation;
        const QSize size = dimensions(orientation);
        if (!validPageRaster(size) || canceled())
            return {{}, {}, 0, PageError::CannotRead};
        QImage result(size, QImage::Format_RGBA8888_Premultiplied);
        if (result.isNull())
            return {{}, {}, 0, PageError::CannotRead};
        TIFFSetField(_tiff.get(), TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
        const bool ok = TIFFReadRGBAImageOriented(_tiff.get(), size.width(), size.height(),
                                                  reinterpret_cast<uint32_t *>(result.bits()),
                                                  ORIENTATION_TOPLEFT, 1);
        TIFFSetField(_tiff.get(), TIFFTAG_ORIENTATION, uint16_t(orientation));
        if (!ok || canceled())
            return {{}, {}, 0, PageError::CannotRead};
#if Q_BYTE_ORDER == Q_BIG_ENDIAN
        for (int y = 0; y < result.height(); ++y) {
            auto *row = reinterpret_cast<uint32_t *>(result.scanLine(y));
            for (int x = 0; x < result.width(); ++x)
                row[x] = qToLittleEndian(row[x]);
        }
#endif
        uint32_t profileSize = 0;
        void *profile = nullptr;
        if (TIFFGetField(_tiff.get(), TIFFTAG_ICCPROFILE, &profileSize, &profile))
            result.setColorSpace(QColorSpace::fromIccProfile(
                QByteArray::fromRawData(static_cast<const char *>(profile), profileSize)));
        result = ThumbnailLoader::rotateAndFlip(result, orientation);
        const QSize page = result.size();
        return {ThumbnailLoader::createViewerImage(result, boundedPageRaster(page, target, false)), page};
    }

private:
    bool select(int index)
    {
        return index >= 0 && index < _pages.size()
            && TIFFSetDirectory(_tiff.get(), _pages[index]);
    }

    QSize dimensions(ExifOrientation &orientation)
    {
        uint32_t width = 0;
        uint32_t height = 0;
        uint16_t tag = ORIENTATION_TOPLEFT;
        TIFFGetField(_tiff.get(), TIFFTAG_IMAGEWIDTH, &width);
        TIFFGetField(_tiff.get(), TIFFTAG_IMAGELENGTH, &height);
        TIFFGetFieldDefaulted(_tiff.get(), TIFFTAG_ORIENTATION, &tag);
        orientation = tag >= 1 && tag <= 8 ? ExifOrientation(tag) : Horizontal;
        if (width > INT_MAX || height > INT_MAX)
            return {};
        return QSize(int(width), int(height));
    }

    std::unique_ptr<TIFF, decltype(&TIFFClose)> _tiff{nullptr, &TIFFClose};
    QList<tdir_t> _pages;
};

#if defined(ZOIN_ENABLE_PDF)
// Qt PDF serializes its calls inside the library. Keep queued thumbnail
// workers outside that lock so they cannot run ahead of the active viewer.
class PdfWorkPermit {
public:
    PdfWorkPermit(bool interactive, const PagedImageDocument::Canceled &canceled)
    {
        QMutexLocker lock(&mutex);
        if (interactive)
            ++waitingViewers;
        while ((running || (!interactive && waitingViewers > 0)) && !canceled())
            ready.wait(&mutex, 20);
        if (interactive)
            --waitingViewers;
        _admitted = !canceled();
        if (_admitted)
            running = true;
        else
            ready.wakeAll();
    }

    ~PdfWorkPermit()
    {
        if (_admitted) {
            QMutexLocker lock(&mutex);
            running = false;
            ready.wakeAll();
        }
    }

    explicit operator bool() const { return _admitted; }

private:
    inline static QMutex mutex;
    inline static QWaitCondition ready;
    inline static int waitingViewers = 0;
    inline static bool running = false;
    bool _admitted = false;
};

class PdfPages final : public PagedImageDocument {
public:
    PdfPages(const QString &path, PageError &error, bool interactive) : _interactive(interactive)
    {
        const PdfWorkPermit permit(_interactive, [] { return false; });
        const QPdfDocument::Error result = _document.load(path);
        if (result == QPdfDocument::Error::IncorrectPassword)
            error = PageError::PasswordRequired;
    }

    int pageCount() const override { return _document.pageCount(); }
    QSizeF pageSize(int index) override
    {
        const PdfWorkPermit permit(_interactive, [] { return false; });
        return _document.pagePointSize(index);
    }
    bool scalable() const override { return true; }

    ImagePage render(int index, QSize target, QRect clip, const Canceled &canceled) override
    {
        const QSizeF page = pageSize(index);
        if (target.isEmpty())
            target = boundedPageRaster(page, {}, true);
        if (canceled() || !page.isValid() || target.isEmpty())
            return {{}, page, 0, PageError::CannotRead};
        QPdfDocumentRenderOptions options;
        options.setRenderFlags(QPdfDocumentRenderOptions::RenderFlag::Annotations);
        QSize renderSize = target;
        if (!clip.isEmpty()) {
            clip = clip.intersected(QRect(QPoint{}, target));
            if (clip.isEmpty())
                return {{}, page, 0, PageError::CannotRead};
            options.setScaledSize(target);
            options.setScaledClipRect(clip);
            renderSize = clip.size();
        }
        if (!validPageRaster(renderSize))
            return {{}, page, 0, PageError::CannotRead};
        QImage transparent;
        {
            const PdfWorkPermit permit(_interactive, canceled);
            if (permit)
                transparent = _document.render(index, renderSize, options);
        }
        if (transparent.isNull() || canceled())
            return {{}, page, 0, PageError::CannotRead};
        // PDF pages without a painted background are still white paper.
        QPainter painter(&transparent);
        painter.setCompositionMode(QPainter::CompositionMode_DestinationOver);
        painter.fillRect(transparent.rect(), Qt::white);
        painter.end();
        return {transparent, page};
    }
private:
    QPdfDocument _document;
    bool _interactive;
};
#endif

} // namespace

bool validPageRaster(QSize size)
{
    return !size.isEmpty() && size.width() <= MaxRasterEdge && size.height() <= MaxRasterEdge
        && qint64(size.width()) * size.height() <= MaxRasterPixels;
}

QSize boundedPageRaster(QSizeF size, QSize target, bool allowUpscale)
{
    if (size.isEmpty() || !std::isfinite(size.width()) || !std::isfinite(size.height()))
        return {};
    QSizeF bounds = target.isEmpty() ? size : QSizeF(target);
    bounds = bounds.boundedTo(QSize(MaxRasterEdge, MaxRasterEdge));
    QSizeF scaled = size.scaled(bounds, Qt::KeepAspectRatio);
    if (!allowUpscale)
        scaled = scaled.boundedTo(size);
    const qreal pixels = scaled.width() * scaled.height();
    if (pixels > MaxRasterPixels)
        scaled *= std::sqrt(MaxRasterPixels / pixels);
    return QSize(qMax(1, qFloor(scaled.width())), qMax(1, qFloor(scaled.height())));
}

bool PagedImageDocument::isPdf(const QString &name)
{
#if defined(ZOIN_ENABLE_PDF)
    return QFileInfo(name).suffix().compare(QLatin1String("pdf"), Qt::CaseInsensitive) == 0;
#else
    Q_UNUSED(name)
    return false;
#endif
}

bool PagedImageDocument::canOpen(const QString &name)
{
    const QString extension = QFileInfo(name).suffix().toLower();
    static const QSet<QString> codecs{QStringLiteral("tif"), QStringLiteral("tiff"),
                                     QStringLiteral("webp")};
    return isPdf(name) || codecs.contains(extension) || rawExtensions().contains(extension)
        || heifExtensions().contains(extension) || qtPagedFormats().contains(extension.toLatin1());
}

bool PagedImageDocument::usesPageThumbnail(const QString &name)
{
    const QString extension = QFileInfo(name).suffix().toLower();
    return isPdf(name) || extension == QLatin1String("tiff") || extension == QLatin1String("tif")
        || heifExtensions().contains(extension);
}

std::unique_ptr<PagedImageDocument> PagedImageDocument::open(const QString &path, const QString &name,
                                                            PageError &error, bool interactive)
{
    error = PageError::CannotRead;
    const QString extension = QFileInfo(name).suffix().toLower();
    std::unique_ptr<PagedImageDocument> document;
#if defined(ZOIN_ENABLE_PDF)
    if (isPdf(name))
        document = std::make_unique<PdfPages>(path, error, interactive);
#else
    Q_UNUSED(interactive)
#endif
    if (extension == QLatin1String("tif") || extension == QLatin1String("tiff"))
        document = std::make_unique<TiffPages>(path);
    else if (extension == QLatin1String("webp"))
        document = openWebpPages(path);
    else if (extension == QLatin1String("avif") || extension == QLatin1String("avifs"))
        document = openAvifPages(path);
    else if (heifExtensions().contains(extension))
        document = openHeifPages(path);
    else if (rawExtensions().contains(extension))
        document = openRawPages(path);
    else if (qtPagedFormats().contains(extension.toLatin1()))
        document = std::make_unique<QtImagePages>(path, extension.toLatin1());
    if (!document || document->pageCount() == 0)
        return {};
    error = PageError::None;
    return document;
}

} // namespace ZoinGallery
