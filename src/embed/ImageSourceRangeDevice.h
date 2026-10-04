#ifndef ZOINGALLERY_IMAGESOURCERANGEDEVICE_H
#define ZOINGALLERY_IMAGESOURCERANGEDEVICE_H

#include <ZoinGallery/ImageSourceProvider.h>

#include <QIODevice>
#include <QUrl>

namespace ZoinGallery {

// Seekable, bounded adapter used by both video playback and video thumbnails.
// Reads are deliberately synchronous: callers must own a worker thread.
class ImageSourceRangeDevice final : public QIODevice {
public:
    ImageSourceRangeDevice(
        QSharedPointer<ImageSourceProvider> provider,
        ImageSourceDescriptor source,
        QSharedPointer<ImageSourceCancellation> cancellation);

    bool isSequential() const override { return false; }
    qint64 size() const override { return m_source.size; }
    bool seek(qint64 position) override;

protected:
    qint64 readData(char *data, qint64 maxlen) override;
    qint64 writeData(const char *data, qint64 len) override;

private:
    void clearPrefetch();

    QSharedPointer<ImageSourceProvider> m_provider;
    ImageSourceDescriptor m_source;
    QSharedPointer<ImageSourceCancellation> m_cancellation;
    QByteArray m_prefetched;
    qint64 m_prefetchOffset = -1;
    bool m_prefetchEndOfFile = false;
};

bool imageSourceSupportsRangeReads(
    const ImageSourceDescriptor &source,
    const QSharedPointer<ImageSourceProvider> &provider);
QUrl imageSourceUrlHint(const ImageSourceDescriptor &source);

} // namespace ZoinGallery

#endif // ZOINGALLERY_IMAGESOURCERANGEDEVICE_H
