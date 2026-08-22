#ifndef ZOINGALLERY_EMBEDDEDIMAGEPROBE_H
#define ZOINGALLERY_EMBEDDEDIMAGEPROBE_H

#include <QByteArray>
#include <QSize>
#include <QString>

#include <functional>

namespace ZoinGallery {

// The first gallery pass is intentionally independent of a requested tile
// size.  A tiny embedded image is useful on an expensive source even when it
// would not cover the final masonry or viewer target.
struct EmbeddedProbeLimits {
    qsizetype initialRangeBytes = 32 * 1024;
    qsizetype maximumHeaderBytes = 256 * 1024;
    int maximumRangeRequests = 8;
};

struct EmbeddedRangeResult {
    QByteArray bytes;
    bool endOfFile = false;
    QString error;

    bool succeeded() const { return error.isEmpty(); }
};

using EmbeddedRangeReader =
    std::function<EmbeddedRangeResult(qint64 offset, qsizetype length)>;

struct EmbeddedProbeResult {
    enum class Outcome {
        Found,
        NotFound,
        Unsupported,
        ReadFailed,
    };

    Outcome outcome = Outcome::Unsupported;
    QByteArray encodedPreview;
    QString previewMimeType;
    QSize previewSize;
    QSize sourceSize;
    // Exif values 1..8.  Keeping this independent of ImageFile.h makes the
    // range parser usable by the source layer before an ImageInfo exists.
    int orientation = 1;
    qint64 sourceBytesRead = 0;
    int rangeRequests = 0;
    QString diagnostic;

    bool found() const {
        return outcome == Outcome::Found && !encodedPreview.isEmpty();
    }
};

// Performs a bounded, no-full-decode probe.  Unsupported formats are a
// terminal first-pass outcome; callers must not turn that into readAll here.
EmbeddedProbeResult probeEmbeddedImage(
    const QString &sourceName, const EmbeddedRangeReader &reader,
    const EmbeddedProbeLimits &limits = EmbeddedProbeLimits(),
    const QString &sourceMimeType = QString());

} // namespace ZoinGallery

#endif // ZOINGALLERY_EMBEDDEDIMAGEPROBE_H
