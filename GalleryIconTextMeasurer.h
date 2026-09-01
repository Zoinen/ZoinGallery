#ifndef GALLERYICONTEXTMEASURER_H
#define GALLERYICONTEXTMEASURER_H

#include <QFontMetricsF>
#include <QHash>
#include <QString>

namespace ZoinGallery {

struct GalleryIconLabelLayout {
    QString text;
    int lineCount = 1;
    qreal height = 0;
};

// Shared text-measurement policy for Icons geometry. Keeping this separate
// guarantees the analytical geometry path and full masonry rewrap use the
// same wrapping and middle-elision decisions.
class GalleryIconTextMeasurer final {
public:
    explicit GalleryIconTextMeasurer(const QFont &font);

    [[nodiscard]] qreal lineHeight() const;
    [[nodiscard]] GalleryIconLabelLayout layout(
        const QString &sourceText, qreal width);

private:
    qreal glyphWidth(QChar character);
    int wrappedLineCount(const QString &text, qreal width);

    QFontMetricsF _metrics;
    qreal _lineHeight = 0;
    QHash<QChar, qreal> _glyphWidths;
};

} // namespace ZoinGallery

#endif // GALLERYICONTEXTMEASURER_H
