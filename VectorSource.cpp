#include "VectorSource.h"
#include <QFileInfo>
#include <QSvgRenderer>
#include <QUrlQuery>
#include <QMutex>
#include <QMutexLocker>
#include <QHash>

namespace {
QMutex sourceMutex;
QHash<QUrl, QWeakPointer<VectorSource>> sourceHandles;
}


bool isVectorSource(const ImageInfo &info) {
    const QString suffix = QFileInfo(info.formatHint()).suffix().toLower();
    return suffix == QStringLiteral("svg") || suffix == QStringLiteral("svgz")
        || info.source.mimeType.section(';', 0, 0).trimmed()
            == QStringLiteral("image/svg+xml");
}

QString vectorRevisionKey(const ImageInfo &info) {
    return info.source.isValid() ? info.source.accessKey()
        : info.path + QChar(0x1f) + info.sourceVersionToken + QChar(0x1f)
          + QString::number(info.lastModified.toMSecsSinceEpoch())
          + QChar(0x1f) + QString::number(info.fileSize);
}

VectorSourcePtr acquireVectorSource(const ImageInfo &info,
    const QSharedPointer<ZoinGallery::ImageSourceLease> &lease) {
    const QString path = lease ? lease->localPath() : info.path;
    // Parsing on the read worker validates XML, compressed SVG and dimensions.
    // No QImage or painter is created here.
    QSvgRenderer document(path);
    if (!document.isValid() || document.defaultSize().isEmpty()) return {};
    auto source = VectorSourcePtr::create();
    source->size = document.defaultSize();
    source->lease = lease;
    source->url = QUrl::fromLocalFile(path);
    QUrlQuery revision;
    revision.addQueryItem(QStringLiteral("vector"), QStringLiteral("1"));
    revision.addQueryItem(QStringLiteral("revision"), vectorRevisionKey(info));
    source->url.setQuery(revision);
    {
        QMutexLocker lock(&sourceMutex);
        for (auto it = sourceHandles.begin(); it != sourceHandles.end();) {
            if (it.value().isNull()) it = sourceHandles.erase(it);
            else ++it;
        }
        sourceHandles.insert(source->url, source.toWeakRef());
    }
    return source;
}

void VectorSourcePin::setSource(const QUrl &source) {
    if (_source == source) return;
    {
        QMutexLocker lock(&sourceMutex);
        _pin = sourceHandles.value(source).toStrongRef();
    }
    _source = source;
    emit sourceChanged();
}
