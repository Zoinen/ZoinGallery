#ifndef ZOINGALLERY_GALLERYICONRESOLVER_H
#define ZOINGALLERY_GALLERYICONRESOLVER_H

#include <QObject>
#include <QString>

namespace ZoinGallery {

// Resolves semantic icon keys at the embedding boundary. ZoinGallery itself
// knows only its own generic fallbacks and never names a host resource root.
class GalleryIconResolver : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString compactPrefix READ compactPrefix WRITE setCompactPrefix
               NOTIFY prefixesChanged)
    Q_PROPERTY(QString largePrefix READ largePrefix WRITE setLargePrefix
               NOTIFY prefixesChanged)

public:
    explicit GalleryIconResolver(QObject *parent = nullptr);

    QString compactPrefix() const;
    void setCompactPrefix(const QString &prefix);
    QString largePrefix() const;
    void setLargePrefix(const QString &prefix);

    Q_INVOKABLE QString keyFromSource(const QString &source) const;
    Q_INVOKABLE QString resolve(const QString &semanticKey,
                                const QString &source,
                                bool largePresentation,
                                bool folder,
                                bool image,
                                bool parentEntry) const;
    Q_INVOKABLE QString fallbackSource(bool folder, bool image,
                                       bool parentEntry) const;
    Q_INVOKABLE bool isMonochrome(const QString &semanticKey,
                                  const QString &source) const;
    Q_INVOKABLE bool isSystemFileSource(const QString &source) const;
    Q_INVOKABLE QString retargetProviderSource(
        const QString &source, int logicalSize, qreal dpr,
        const QString &tint, bool monochrome) const;

signals:
    void prefixesChanged();

private:
    static QString normalizedPrefix(QString prefix);
    static QString normalizedKey(QString key);

    QString _compactPrefix;
    QString _largePrefix;
};

} // namespace ZoinGallery

#endif // ZOINGALLERY_GALLERYICONRESOLVER_H
