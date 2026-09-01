#ifndef GALLERYLAYOUTENGINE_H
#define GALLERYLAYOUTENGINE_H

#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QVector>

namespace ZoinGallery {

enum class GalleryPresentationMode {
    Masonry = 0,
    Columns,
    Details,
    Grid,
    Icons,
};

struct GalleryInsets {
    qreal left = 0;
    qreal right = 0;
    qreal top = 0;
    qreal bottom = 0;
};

struct GalleryLayoutRequest {
    GalleryPresentationMode mode = GalleryPresentationMode::Masonry;
    QSizeF viewportSize;
    GalleryInsets insets;
    qreal density = 150;
    qreal spacing = 0;
    int columnCount = 2;
    qreal devicePixelRatio = 1;
    bool lastRowMatchesPrevious = false;
    bool singleRow = false;
};

struct GalleryLayoutEntry {
    QSizeF originalSize;
    bool temporaryLineBreakAfter = false;
    bool lineBreakAfter = false;
    QString displayLabel;
    qreal labelHeight = 0;
};

struct GalleryLayoutCell {
    QRectF geometry;
    QRectF previewGeometry;
    int row = -1;
    int column = -1;
    QString displayLabel;
};

struct GalleryLayoutBand {
    int row = -1;
    qreal top = 0;
    qreal bottom = 0;
    QVector<int> indexes;
};

struct GalleryLayoutResult {
    QVector<GalleryLayoutCell> cells;
    QVector<GalleryLayoutBand> bands;
    qreal contentExtent = 0;
};

// An immutable analytical description of a fixed presentation. It contains
// no per-row state, so a 30K-row catalog costs exactly the same as an empty
// one until the viewport materializer asks for concrete row geometry.
struct GalleryFixedLayoutPlan {
    GalleryPresentationMode mode = GalleryPresentationMode::Details;
    int entryCount = 0;
    int columns = 1;
    int rowsPerColumn = 1;
    qreal canvasWidth = 0;
    qreal extent = 1;
    qreal cellWidth = 0;
    qreal contentExtent = 0;
    GalleryInsets insets;
    qreal spacing = 0;

    [[nodiscard]] bool horizontal() const;
    [[nodiscard]] QRectF geometryFor(int index) const;
    [[nodiscard]] QRectF previewGeometryFor(int index) const;
    [[nodiscard]] QVector<int> indexesIntersecting(qreal start,
                                                   qreal end) const;
};

class GalleryDensityPolicy final {
public:
    [[nodiscard]] static qreal normalized(
        GalleryPresentationMode mode, qreal density);
};

class ColumnMajorStrategy final {
public:
    [[nodiscard]] static GalleryFixedLayoutPlan plan(
        const GalleryLayoutRequest &request, int entryCount);
};

class DetailsStrategy final {
public:
    [[nodiscard]] static GalleryFixedLayoutPlan plan(
        const GalleryLayoutRequest &request, int entryCount);
};

enum class UniformGridPolicy {
    Grid,
    Icons,
};

class UniformGridStrategy final {
public:
    [[nodiscard]] static GalleryFixedLayoutPlan analyticalPlan(
        const GalleryLayoutRequest &request, int entryCount);
    [[nodiscard]] static GalleryLayoutResult layout(
        const GalleryLayoutRequest &request,
        const QVector<GalleryLayoutEntry> &entries,
        UniformGridPolicy policy,
        qreal oneLineHeight = 0);
};

class JustifiedMasonryStrategy final {
public:
    [[nodiscard]] static GalleryLayoutResult layout(
        const GalleryLayoutRequest &request,
        const QVector<GalleryLayoutEntry> &entries);
};

class GalleryLayoutEngine final {
public:
    [[nodiscard]] static GalleryFixedLayoutPlan fixedPlan(
        const GalleryLayoutRequest &request, int entryCount);
    [[nodiscard]] static GalleryLayoutResult layout(
        const GalleryLayoutRequest &request,
        const QVector<GalleryLayoutEntry> &entries,
        qreal oneLineHeight = 0);
};

} // namespace ZoinGallery

#endif // GALLERYLAYOUTENGINE_H
