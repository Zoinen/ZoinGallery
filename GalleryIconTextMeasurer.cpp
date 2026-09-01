#include "GalleryIconTextMeasurer.h"

#include <QtMath>

namespace ZoinGallery {

GalleryIconTextMeasurer::GalleryIconTextMeasurer(const QFont &font)
    : _metrics(font),
      _lineHeight(static_cast<qreal>(qCeil(_metrics.height()))) {}

qreal GalleryIconTextMeasurer::lineHeight() const {
    return _lineHeight;
}

GalleryIconLabelLayout GalleryIconTextMeasurer::layout(
    const QString &sourceText, qreal width) {
    constexpr int maximumLines = 4;
    width = qMax<qreal>(1, width - 1);
    const int sourceLines = wrappedLineCount(sourceText, width);
    if (sourceLines <= maximumLines) {
        return {sourceText, sourceLines, sourceLines * _lineHeight};
    }

    const QString ellipsis(QChar(0x2026));
    const auto candidateForKeptCharacters = [&](int kept) {
        const int prefixLength = (kept + 1) / 2;
        const int suffixLength = kept / 2;
        return sourceText.left(prefixLength) + ellipsis
            + sourceText.right(suffixLength);
    };
    QString best = ellipsis;
    int bestLines = 1;
    int low = 0;
    int high = qMax(0, sourceText.size() - 1);
    while (low <= high) {
        const int middle = low + (high - low) / 2;
        const QString candidate = candidateForKeptCharacters(middle);
        const int lines = wrappedLineCount(candidate, width);
        if (lines <= maximumLines) {
            best = candidate;
            bestLines = lines;
            low = middle + 1;
        } else {
            high = middle - 1;
        }
    }
    return {best, bestLines, bestLines * _lineHeight};
}

qreal GalleryIconTextMeasurer::glyphWidth(QChar character) {
    const auto found = _glyphWidths.constFind(character);
    if (found != _glyphWidths.constEnd()) {
        return *found;
    }
    return *_glyphWidths.insert(
        character, _metrics.horizontalAdvance(QString(character)));
}

int GalleryIconTextMeasurer::wrappedLineCount(
    const QString &text, qreal width) {
    if (text.isEmpty()) {
        return 1;
    }
    int lines = 1;
    qreal currentWidth = 0;
    for (const QChar character : text) {
        if (character == QChar::LineFeed) {
            ++lines;
            currentWidth = 0;
            continue;
        }
        const qreal advance = glyphWidth(character);
        if (currentWidth > 0 && currentWidth + advance > width) {
            ++lines;
            currentWidth = 0;
        }
        currentWidth += advance;
    }
    return lines;
}

} // namespace ZoinGallery
