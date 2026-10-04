#include "ImageSourceRangeDevice.h"

#include <QUrl>

#include <algorithm>
#include <cstring>

namespace ZoinGallery {
namespace {
constexpr qint64 ReadChunkSize = 1024 * 1024;
}

ImageSourceRangeDevice::ImageSourceRangeDevice(
    QSharedPointer<ImageSourceProvider> provider,
    ImageSourceDescriptor source,
    QSharedPointer<ImageSourceCancellation> cancellation)
    : m_provider(std::move(provider)),
      m_source(std::move(source)),
      m_cancellation(std::move(cancellation)) {
    open(QIODevice::ReadOnly | QIODevice::Unbuffered);
}

bool ImageSourceRangeDevice::seek(qint64 position) {
    if (position < 0 || (m_source.size >= 0 && position > m_source.size)) {
        return false;
    }
    if (!QIODevice::seek(position)) {
        return false;
    }
    if (position < m_prefetchOffset
        || position >= m_prefetchOffset + m_prefetched.size()) {
        clearPrefetch();
    }
    return true;
}

qint64 ImageSourceRangeDevice::readData(char *data, qint64 maxlen) {
    if (!m_provider || maxlen <= 0) {
        return 0;
    }
    if (m_cancellation && m_cancellation->isCanceled()) {
        setErrorString(QStringLiteral("video source read cancelled"));
        return -1;
    }

    const qint64 offset = pos();
    if (offset < 0 || (m_source.size >= 0 && offset >= m_source.size)) {
        return 0;
    }
    const qint64 prefetchEnd = m_prefetchOffset >= 0
        ? m_prefetchOffset + m_prefetched.size() : -1;
    if (m_prefetchOffset < 0 || offset < m_prefetchOffset
        || offset >= prefetchEnd) {
        if (m_prefetchEndOfFile && offset >= prefetchEnd) {
            return 0;
        }
        const qint64 requested = m_source.size >= 0
            ? std::min(ReadChunkSize, m_source.size - offset)
            : ReadChunkSize;
        if (requested <= 0) {
            return 0;
        }

        const ImageSourceReadResult result = m_provider->readRange(
            m_source, offset, requested, m_cancellation);
        if (!result.succeeded()) {
            setErrorString(result.errorString);
            return -1;
        }
        if (result.data.isEmpty()) {
            if (result.endOfFile) {
                m_prefetchOffset = offset;
                m_prefetched.clear();
                m_prefetchEndOfFile = true;
                return 0;
            }
            setErrorString(QStringLiteral("video source returned no data"));
            return -1;
        }
        m_prefetchOffset = offset;
        m_prefetched = result.data;
        m_prefetchEndOfFile = result.endOfFile;
    }

    const qint64 relative = offset - m_prefetchOffset;
    const qint64 available = std::min<qint64>(
        maxlen, static_cast<qint64>(m_prefetched.size()) - relative);
    if (available <= 0) {
        return 0;
    }
    std::memcpy(data, m_prefetched.constData() + relative,
                static_cast<size_t>(available));
    return available;
}

qint64 ImageSourceRangeDevice::writeData(const char *, qint64) {
    return -1;
}

void ImageSourceRangeDevice::clearPrefetch() {
    m_prefetchOffset = -1;
    m_prefetched.clear();
    m_prefetchEndOfFile = false;
}

bool imageSourceSupportsRangeReads(
    const ImageSourceDescriptor &source,
    const QSharedPointer<ImageSourceProvider> &provider) {
    if (!provider || !source.isValid() || source.size <= 0
        || source.storageClass == QStringLiteral("local")) {
        return false;
    }
    return source.accessProfile == QStringLiteral("nativeRange")
        || source.accessProfile == QStringLiteral("hybridRange");
}

QUrl imageSourceUrlHint(const ImageSourceDescriptor &source) {
    const QString name = source.displayName.trimmed().isEmpty()
        ? QStringLiteral("f4-video") : source.displayName.trimmed();
    return QUrl::fromLocalFile(name);
}

} // namespace ZoinGallery
