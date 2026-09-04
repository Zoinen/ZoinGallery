#include <ZoinGallery/GalleryIconResolver.h>

#include <QUrl>
#include <QUrlQuery>

#include <utility>

namespace ZoinGallery {

GalleryIconResolver::GalleryIconResolver(QObject *parent)
    : QObject(parent) {}

QString GalleryIconResolver::compactPrefix() const {
    return _compactPrefix;
}

void GalleryIconResolver::setCompactPrefix(const QString &prefix) {
    const QString normalized = normalizedPrefix(prefix);
    if (_compactPrefix == normalized) {
        return;
    }
    _compactPrefix = normalized;
    emit prefixesChanged();
}

QString GalleryIconResolver::largePrefix() const {
    return _largePrefix;
}

void GalleryIconResolver::setLargePrefix(const QString &prefix) {
    const QString normalized = normalizedPrefix(prefix);
    if (_largePrefix == normalized) {
        return;
    }
    _largePrefix = normalized;
    emit prefixesChanged();
}

QString GalleryIconResolver::keyFromSource(const QString &source) const {
    const QString normalized = source.left(source.indexOf(QLatin1Char('?')) >= 0
        ? source.indexOf(QLatin1Char('?')) : source.size());
    const qsizetype lucide = normalized.lastIndexOf(
        QStringLiteral("/lucide/"));
    const qsizetype gallery = normalized.lastIndexOf(
        QStringLiteral("/lucide-gallery/"));
    const qsizetype marker = qMax(lucide, gallery);
    if (marker < 0) {
        return {};
    }
    const qsizetype slash = normalized.lastIndexOf(QLatin1Char('/'));
    const qsizetype dot = normalized.lastIndexOf(QLatin1Char('.'));
    if (slash < marker) {
        return {};
    }
    if (dot > slash) {
        return normalizedKey(
            normalized.sliced(slash + 1, dot - slash - 1));
    }
    if (QUrl(source).scheme() == QStringLiteral("image")) {
        return normalizedKey(normalized.sliced(slash + 1));
    }
    return {};
}

QString GalleryIconResolver::resolve(
    const QString &semanticKey, const QString &source,
    bool largePresentation, bool folder, bool image,
    bool parentEntry) const {
    QString key = normalizedKey(semanticKey);
    // Provider URLs are already size/DPR/revision-aware render requests. They
    // may still be identified as monochrome by keyFromSource(), but source
    // inference must not replace them with a static host qrc asset.
    if (key.isEmpty()
        && QUrl(source).scheme() != QStringLiteral("image")) {
        key = keyFromSource(source);
    }
    const QString prefix = largePresentation && !_largePrefix.isEmpty()
        ? _largePrefix : _compactPrefix;
    if (!key.isEmpty() && !prefix.isEmpty()) {
        return prefix + key + QStringLiteral(".svg");
    }
    // image:// URLs are rendered by a registered provider.  The provider has
    // one logical lucide route whose size/DPR query is part of the request;
    // rewriting it to the static "lucide-gallery" route makes the provider
    // return no image in large presentations (Grid, Icons and Masonry).
    if (largePresentation
        && QUrl(source).scheme() != QStringLiteral("image")
        && source.contains(QStringLiteral("/lucide/"))) {
        QString largeSource = source;
        largeSource.replace(QStringLiteral("/lucide/"),
                            QStringLiteral("/lucide-gallery/"));
        return largeSource;
    }
    return source.isEmpty()
        ? fallbackSource(folder, image, parentEntry) : source;
}

QString GalleryIconResolver::fallbackSource(
    bool folder, bool image, bool parentEntry) const {
    Q_UNUSED(parentEntry)
    return folder
        ? QStringLiteral("qrc:/ZoinGallery/resources/FolderIcon.svg")
        : image
            ? QStringLiteral("qrc:/ZoinGallery/resources/ImageIcon.svg")
            : QStringLiteral("qrc:/ZoinGallery/resources/FileIcon.svg");
}

bool GalleryIconResolver::isMonochrome(
    const QString &semanticKey, const QString &source) const {
    return !normalizedKey(semanticKey).isEmpty()
        || !keyFromSource(source).isEmpty()
        || source.startsWith(QStringLiteral("qrc:/ZoinGallery/resources/"));
}

bool GalleryIconResolver::isSystemFileSource(const QString &source) const {
    return source.startsWith(QStringLiteral("image://"))
        && source.contains(QStringLiteral("/file/"));
}

QString GalleryIconResolver::retargetProviderSource(
    const QString &source, int logicalSize, qreal dpr,
    const QString &tint, bool monochrome) const {
    QUrl url(source);
    if (url.scheme() != QStringLiteral("image")) {
        return source;
    }
    const QString path = url.path();
    if ((!path.contains(QStringLiteral("/file/"))
         && !path.contains(QStringLiteral("/lucide/")))
        || !QUrlQuery(url).hasQueryItem(QStringLiteral("size"))) {
        return source;
    }

    QUrlQuery query(url);
    query.removeAllQueryItems(QStringLiteral("size"));
    query.addQueryItem(QStringLiteral("size"),
                       QString::number(qMax(1, logicalSize)));
    query.removeAllQueryItems(QStringLiteral("dpr"));
    query.addQueryItem(QStringLiteral("dpr"),
                       QString::number(qMax<qreal>(0.5, dpr)));
    if (monochrome && !tint.isEmpty()) {
        query.removeAllQueryItems(QStringLiteral("color"));
        query.addQueryItem(QStringLiteral("color"), tint);
    }
    url.setQuery(query);
    return url.toString(QUrl::FullyEncoded);
}

QString GalleryIconResolver::normalizedPrefix(QString prefix) {
    prefix = prefix.trimmed();
    if (!prefix.isEmpty() && !prefix.endsWith(QLatin1Char('/'))) {
        prefix.append(QLatin1Char('/'));
    }
    return prefix;
}

QString GalleryIconResolver::normalizedKey(QString key) {
    key = key.trimmed();
    if (key.endsWith(QStringLiteral(".svg"), Qt::CaseInsensitive)) {
        key.chop(4);
    }
    for (const QChar character : std::as_const(key)) {
        if (!character.isLetterOrNumber() && character != QLatin1Char('-')
            && character != QLatin1Char('_')) {
            return {};
        }
    }
    return key;
}

} // namespace ZoinGallery
