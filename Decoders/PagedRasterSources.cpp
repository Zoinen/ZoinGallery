#include "PagedImageDocument.h"

#include "HeicDecoder.h"
#include "ThumbnailLoader.h"
#include "TinyEXIF.h"
#include "WebpCodec.h"

#include <QColorSpace>
#include <QFile>
#include <QLoggingCategory>
#include <libheif/heif.h>
#include <libheif/heif_sequences.h>
#include <libraw/libraw.h>
#include <webp/demux.h>

#include <algorithm>

namespace ZoinGallery {
namespace {

Q_LOGGING_CATEGORY(lcRawPages, "zoingallery.raw.pages", QtWarningMsg)

class WebpPages final : public PagedImageDocument {
public:
    explicit WebpPages(const QString &path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly) || file.size() > 512LL * 1024 * 1024)
            return;
        _bytes = file.readAll();
        const WebPData data = webpData();
        const std::unique_ptr<WebPDemuxer, decltype(&WebPDemuxDelete)> demux(
            WebPDemux(&data), &WebPDemuxDelete);
        if (!demux)
            return;
        _canvas = QSize(int(WebPDemuxGetI(demux.get(), WEBP_FF_CANVAS_WIDTH)),
                        int(WebPDemuxGetI(demux.get(), WEBP_FF_CANVAS_HEIGHT)));
        if (!validPageRaster(_canvas))
            return;
        _count = int(WebPDemuxGetI(demux.get(), WEBP_FF_FRAME_COUNT));
        const auto passes = WebPDemuxGetI(demux.get(), WEBP_FF_LOOP_COUNT);
        _repeats = passes == 0 ? -1 : int(passes) - 1;
        WebPChunkIterator chunk;
        if (WebPDemuxGetChunk(demux.get(), "ICCP", 1, &chunk)) {
            _colorSpace = QColorSpace::fromIccProfile(QByteArray::fromRawData(
                reinterpret_cast<const char *>(chunk.chunk.bytes), qsizetype(chunk.chunk.size)));
            WebPDemuxReleaseChunkIterator(&chunk);
        }
        if (WebPDemuxGetChunk(demux.get(), "EXIF", 1, &chunk)) {
            constexpr char prefix[] = "Exif\0";
            const QByteArrayView exif(reinterpret_cast<const char *>(chunk.chunk.bytes),
                                      qsizetype(chunk.chunk.size));
            const QByteArray bytes = exif.startsWith(QByteArrayView(prefix, sizeof(prefix)))
                ? exif.toByteArray() : QByteArray(prefix, sizeof(prefix)).append(exif);
            TinyEXIF::EXIFInfo info;
            if (info.parseFromEXIFSegment(reinterpret_cast<const uint8_t *>(bytes.constData()),
                                          unsigned(bytes.size())) == TinyEXIF::PARSE_SUCCESS
                && info.Orientation >= 1 && info.Orientation <= 8)
                _orientation = ExifOrientation(info.Orientation);
            WebPDemuxReleaseChunkIterator(&chunk);
        }
    }

    int pageCount() const override { return _count; }
    bool animated() const override { return _count > 1; }
    int repeats() const override { return _repeats; }
    QSizeF pageSize(int) override { return rotateToOrientation(_canvas, _orientation); }

    ImagePage render(int index, QSize target, QRect, const Canceled &canceled) override
    {
        const QSize page = pageSize(index).toSize();
        if (index < 0 || index >= _count || canceled())
            return {{}, page, 0, PageError::CannotRead};
        int delay = 0;
        QImage image;
        if (!animated()) {
            image = WebpCodec::decode(_bytes, {});
        } else {
            if (!_decoder || index < _lastIndex) {
                const WebPData data = webpData();
                _decoder.reset(WebPAnimDecoderNew(&data, nullptr));
                _lastIndex = -1;
                _timestamp = 0;
                _delay = 0;
                _frame = nullptr;
            }
            if (!_decoder)
                return {{}, page, 0, PageError::CannotRead};
            while (_lastIndex < index && !canceled()) {
                int timestamp = 0;
                if (!WebPAnimDecoderGetNext(_decoder.get(), &_frame, &timestamp))
                    return {{}, page, 0, PageError::CannotRead};
                _delay = timestamp - _timestamp;
                _timestamp = timestamp;
                ++_lastIndex;
            }
            if (!_frame || canceled())
                return {{}, page, 0, PageError::CannotRead};
            image = QImage(_frame, _canvas.width(), _canvas.height(),
                           _canvas.width() * 4, QImage::Format_RGBA8888).copy();
            delay = _delay;
        }
        image.setColorSpace(_colorSpace);
        image = ThumbnailLoader::rotateAndFlip(image, _orientation);
        return {ThumbnailLoader::createViewerImage(image, boundedPageRaster(page, target, false)),
                page, delay, image.isNull() ? PageError::CannotRead : PageError::None};
    }

private:
    WebPData webpData() const
    {
        return {reinterpret_cast<const uint8_t *>(_bytes.constData()), size_t(_bytes.size())};
    }

    QByteArray _bytes;
    QSize _canvas;
    QColorSpace _colorSpace;
    ExifOrientation _orientation = Horizontal;
    std::unique_ptr<WebPAnimDecoder, decltype(&WebPAnimDecoderDelete)> _decoder{
        nullptr, &WebPAnimDecoderDelete};
    uint8_t *_frame = nullptr;
    int _count = 0;
    int _repeats = 0;
    int _lastIndex = -1;
    int _timestamp = 0;
    int _delay = 0;
};

const heif_reader HeifFileReader{
    1,
    [](void *reader) -> int64_t { return static_cast<QFile *>(reader)->pos(); },
    [](void *buffer, size_t size, void *reader) {
        return static_cast<QFile *>(reader)->read(static_cast<char *>(buffer), qint64(size))
            == qint64(size) ? 0 : 1;
    },
    [](int64_t position, void *reader) { return static_cast<QFile *>(reader)->seek(position) ? 0 : 1; },
    [](int64_t size, void *reader) {
        return size <= static_cast<QFile *>(reader)->size() ? heif_reader_grow_status_size_reached
                                                           : heif_reader_grow_status_size_beyond_eof;
    },
};

class HeifPages final : public PagedImageDocument {
public:
    explicit HeifPages(const QString &path) : _file(path)
    {
        if (!_context || !_file.open(QIODevice::ReadOnly)
            || heif_context_read_from_reader(_context.get(), &HeifFileReader, &_file, nullptr)
                .code != heif_error_Ok)
            return;
        const int tracks = heif_context_number_of_sequence_tracks(_context.get());
        if (tracks > 0) {
            QList<uint32_t> trackIds(tracks);
            heif_context_get_track_ids(_context.get(), trackIds.data());
            for (const auto id : trackIds) {
                _track.reset(heif_context_get_track(_context.get(), id));
                if (_track && heif_track_get_track_handler_type(_track.get())
                        == heif_track_type_image_sequence
                    && heif_track_get_image_resolution(_track.get(), &_trackWidth, &_trackHeight)
                        .code == heif_error_Ok) {
                    _trackId = id;
                    _total = -1;
                    return;
                }
            }
            _track.reset();
        }
        const int count = heif_context_get_number_of_top_level_images(_context.get());
        if (count <= 0)
            return;
        _ids.resize(count);
        _ids.resize(heif_context_get_list_of_top_level_image_IDs(_context.get(), _ids.data(), count));
        heif_item_id primary = 0;
        if (heif_context_get_primary_image_ID(_context.get(), &primary).code == heif_error_Ok) {
            const int index = int(_ids.indexOf(primary));
            if (index > 0)
                _ids.move(index, 0);
        }
        _total = int(_ids.size());
    }

    int pageCount() const override { return _total; }
    bool animated() const override { return _trackId != 0 && _total != 1; }

    QSizeF pageSize(int index) override
    {
        if (_trackId)
            return QSize(_trackWidth, _trackHeight);
        const auto handle = imageHandle(index);
        return handle ? QSize(heif_image_handle_get_width(handle.get()),
                              heif_image_handle_get_height(handle.get())) : QSize{};
    }

    ImagePage render(int index, QSize target, QRect, const Canceled &canceled) override
    {
        if (_total >= 0 && index >= _total)
            return {{}, {}, 0, PageError::EndOfSequence};
        if (_trackId)
            return renderSequence(index, target, canceled);
        const auto handle = imageHandle(index);
        if (!handle || !validPageRaster(pageSize(index).toSize()) || canceled())
            return {{}, {}, 0, PageError::CannotRead};
        QImage image = decodeHeifHandle(handle.get());
        return fit(image, target, 0);
    }

private:
    using HeifHandle = std::unique_ptr<heif_image_handle, decltype(&heif_image_handle_release)>;
    HeifHandle imageHandle(int index)
    {
        heif_image_handle *handle = nullptr;
        if (index >= 0 && index < _ids.size())
            heif_context_get_image_handle(_context.get(), _ids[index], &handle);
        return {handle, &heif_image_handle_release};
    }

    ImagePage renderSequence(int index, QSize target, const Canceled &canceled)
    {
        if (!validPageRaster(QSize(_trackWidth, _trackHeight)) || canceled())
            return {{}, {}, 0, PageError::CannotRead};
        if (index < _current) {
            // The track's read cursor belongs to the context, not its handle.
            _track.reset();
            _context.reset(heif_context_alloc());
            if (!_context || !_file.seek(0)
                || heif_context_read_from_reader(_context.get(), &HeifFileReader, &_file, nullptr)
                    .code != heif_error_Ok)
                return {{}, {}, 0, PageError::CannotRead};
            _track.reset(heif_context_get_track(_context.get(), _trackId));
            _current = -1;
            _last = {};
        }
        if (!_track)
            return {{}, {}, 0, PageError::CannotRead};
        const uint32_t timescale = heif_track_get_timescale(_track.get());
        while (_current < index && !canceled()) {
            heif_image *decoded = nullptr;
            const heif_error error = heif_track_decode_next_image(
                _track.get(), &decoded, heif_colorspace_RGB, heif_chroma_interleaved_RGBA, nullptr);
            const std::unique_ptr<heif_image, decltype(&heif_image_release)> frame(decoded, &heif_image_release);
            if (error.code == heif_error_End_of_sequence) {
                _total = _current + 1;
                return {{}, {}, 0, PageError::EndOfSequence};
            }
            if (error.code != heif_error_Ok || !frame)
                return {{}, {}, 0, PageError::CannotRead};
            _last = copyHeifPixels(frame.get(), true);
            if (_last.isNull())
                return {{}, {}, 0, PageError::CannotRead};
            _delay = timescale == 0 ? 100 : int(qMin<qint64>(INT_MAX,
                qint64(heif_image_get_duration(frame.get())) * 1000 / timescale));
            ++_current;
        }
        return canceled() ? ImagePage{{}, {}, 0, PageError::CannotRead} : fit(_last, target, _delay);
    }

    static ImagePage fit(const QImage &image, QSize target, int delay)
    {
        if (image.isNull())
            return {{}, {}, 0, PageError::CannotRead};
        const QSize size = image.size();
        return {ThumbnailLoader::createViewerImage(image, boundedPageRaster(size, target, false)),
                size, delay};
    }

    QFile _file;
    std::unique_ptr<heif_context, decltype(&heif_context_free)> _context{
        heif_context_alloc(), &heif_context_free};
    std::unique_ptr<heif_track, decltype(&heif_track_release)> _track{nullptr, &heif_track_release};
    QList<heif_item_id> _ids;
    uint32_t _trackId = 0;
    uint16_t _trackWidth = 0;
    uint16_t _trackHeight = 0;
    int _total = 0;
    int _current = -1;
    int _delay = 0;
    QImage _last;
};

class RawPages final : public PagedImageDocument {
public:
    explicit RawPages(const QString &path) : _path(path)
    {
        // LibRaw is too large for the default macOS page worker stack.
        const auto reader = std::make_unique<LibRaw>();
        if (open(*reader, 0)) {
            _count = int(qMin(reader->imgdata.idata.raw_count, unsigned(INT_MAX)));
            _firstSize = dimensions(*reader);
        }
    }

    int pageCount() const override { return _count; }
    QSizeF pageSize(int index) override
    {
        if (index == 0)
            return _firstSize;
        const auto reader = std::make_unique<LibRaw>();
        return open(*reader, index) ? dimensions(*reader) : QSize{};
    }

    ImagePage render(int index, QSize target, QRect, const Canceled &canceled) override
    {
        if (canceled())
            return {{}, {}, 0, PageError::CannotRead};
        const auto reader = std::make_unique<LibRaw>();
        if (!open(*reader, index) || !validPageRaster(dimensions(*reader))
            || reader->unpack() != LIBRAW_SUCCESS || canceled() || reader->dcraw_process() != LIBRAW_SUCCESS)
            return {{}, {}, 0, PageError::CannotRead};
        const std::unique_ptr<libraw_processed_image_t, decltype(&LibRaw::dcraw_clear_mem)>
            pixels(reader->dcraw_make_mem_image(), &LibRaw::dcraw_clear_mem);
        if (!pixels || pixels->type != LIBRAW_IMAGE_BITMAP || pixels->colors != 3 || pixels->bits != 8)
            return {{}, {}, 0, PageError::CannotRead};
        const QSize size(pixels->width, pixels->height);
        if (!validPageRaster(size) || qint64(size.width()) * size.height() * 3 > pixels->data_size)
            return {{}, {}, 0, PageError::CannotRead};
        const QImage image(pixels->data, size.width(), size.height(), size.width() * 3, QImage::Format_RGB888);
        return {ThumbnailLoader::createViewerImage(image, boundedPageRaster(size, target, false)).copy(), size};
    }

private:
    bool open(LibRaw &reader, int index) const
    {
        reader.imgdata.rawparams.shot_select = unsigned(index);
#ifdef Q_OS_WIN
        const int status = reader.open_file(reinterpret_cast<const wchar_t *>(_path.utf16()));
#else
        const int status = reader.open_file(QFile::encodeName(_path).constData());
#endif
        qCDebug(lcRawPages) << "[FIX] RAW page" << index << "open result" << LibRaw::strerror(status);
        return status == LIBRAW_SUCCESS;
    }

    static QSize dimensions(const LibRaw &reader)
    {
        const auto &size = reader.imgdata.sizes;
        return size.flip & 4 ? QSize(size.height, size.width) : QSize(size.width, size.height);
    }

    QString _path;
    QSize _firstSize;
    int _count = 0;
};

} // namespace

std::unique_ptr<PagedImageDocument> openWebpPages(const QString &path)
{
    return std::make_unique<WebpPages>(path);
}

std::unique_ptr<PagedImageDocument> openHeifPages(const QString &path)
{
    return std::make_unique<HeifPages>(path);
}

std::unique_ptr<PagedImageDocument> openRawPages(const QString &path)
{
    return std::make_unique<RawPages>(path);
}

} // namespace ZoinGallery
