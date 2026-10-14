#include "GalleryIconTextMeasurer.h"

#include <QtMath>
#include <QTextLayout>
#include <QTextBoundaryFinder>

namespace ZoinGallery {

GalleryIconTextMeasurer::GalleryIconTextMeasurer(const QFont &font, bool wordWrap)
    : _font(font), _wordWrap(wordWrap), _metrics(font),
      _lineHeight(static_cast<qreal>(qCeil(_metrics.height()))) {
    // QTextLine rounds font metrics differently from QFontMetricsF. Use the
    // same line extent as Qt Quick instead of clipping wrapped captions.
    QTextLayout probe(QStringLiteral("Ag"), _font);
    probe.beginLayout();
    QTextLine line = probe.createLine();
    if (line.isValid()) {
        line.setLineWidth(1024);
        _lineHeight = line.height();
    }
    probe.endLayout();
}

qreal GalleryIconTextMeasurer::lineHeight() const {
    return _lineHeight;
}

GalleryIconLabelLayout GalleryIconTextMeasurer::layout(
    const QString &sourceText, qreal width, int maximumLines) const {
    width = qMax<qreal>(1, width);
    maximumLines = qMax(1, maximumLines);
    const int sourceLines = wrappedLineCount(sourceText, width, maximumLines + 1);
    if (sourceLines <= maximumLines) {
        return {sourceText, sourceLines, sourceLines * _lineHeight};
    }

    const QString ellipsis(QChar(0x2026));
    QVector<int> boundaries{0};
    QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, sourceText);
    while (finder.toNextBoundary() >= 0)
        boundaries.append(finder.position());
    const int characters = boundaries.size() - 1;
    const auto candidateForKeptCharacters = [&](int kept) {
        const int prefixLength = (kept + 1) / 2;
        const int suffixLength = kept / 2;
        return sourceText.left(boundaries.at(prefixLength)) + ellipsis
            + sourceText.mid(boundaries.at(characters - suffixLength));
    };
    QString best = ellipsis;
    int bestLines = 1;
    int low = 0;
    int high = qMax(0, characters - 1);
    while (low <= high) {
        const int middle = low + (high - low) / 2;
        const QString candidate = candidateForKeptCharacters(middle);
        const int lines = wrappedLineCount(candidate, width, maximumLines + 1);
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

int GalleryIconTextMeasurer::lineCount(
    const QString &text, qreal width, int maximumLines) const {
    return wrappedLineCount(text, qMax<qreal>(1, width), qMax(1, maximumLines));
}

int GalleryIconTextMeasurer::wrappedLineCount(
    const QString &text, qreal width, int limit) const {
    if (text.isEmpty()) {
        return 1;
    }
    int lines = 0;
    for (const QString &paragraph : text.split(QChar::LineFeed)) {
        QTextLayout layout(paragraph, _font);
        QTextOption option;
        option.setWrapMode(_wordWrap ? QTextOption::WrapAtWordBoundaryOrAnywhere
                                    : QTextOption::WrapAnywhere);
        layout.setTextOption(option);
        layout.beginLayout();
        int paragraphLines = 0;
        while (lines + paragraphLines < limit) {
            QTextLine line = layout.createLine();
            if (!line.isValid()) break;
            line.setLineWidth(width);
            ++paragraphLines;
        }
        layout.endLayout();
        lines += qMax(1, paragraphLines);
        if (lines >= limit) return limit;
    }
    return lines;
}

} // namespace ZoinGallery
