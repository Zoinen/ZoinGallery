#ifndef GALLERYICONTEXTMEASURER_H
#define GALLERYICONTEXTMEASURER_H

#include <QFontMetricsF>
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
    explicit GalleryIconTextMeasurer(const QFont &font, bool wordWrap = false);

    [[nodiscard]] qreal lineHeight() const;
    [[nodiscard]] int lineCount(
        const QString &text, qreal width, int maximumLines = 4) const;
    [[nodiscard]] GalleryIconLabelLayout layout(
        const QString &sourceText, qreal width, int maximumLines = 4) const;

private:
    int wrappedLineCount(const QString &text, qreal width, int limit) const;

    QFont _font;
    bool _wordWrap = false;
    QFontMetricsF _metrics;
    qreal _lineHeight = 0;
};

} // namespace ZoinGallery

#endif // GALLERYICONTEXTMEASURER_H
