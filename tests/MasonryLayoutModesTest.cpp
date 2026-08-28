#include "FileListModel.h"
#include "DecodeManager.h"
#include "MasonryLayout.h"

#include <ZoinGallery/GalleryRuntime.h>
#include <ZoinGallery/GallerySession.h>

#include "src/embed/ExternalCatalogModel.h"

#include <QCoreApplication>
#include <QColor>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QImage>
#include <QKeyEvent>
#include <QElapsedTimer>
#include <QEvent>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickImageProvider>
#include <QQuickView>
#include <QSGRendererInterface>
#include <QTemporaryDir>
#include <QUrlQuery>
#include <QtTest>

#include <cmath>
#include <limits>
#include <tuple>

namespace {

QVariantMap catalogEntry(int index, const QString &path = {}) {
    const QFileInfo info(path);
    return {
        {QStringLiteral("entryId"),
         QStringLiteral("layout-entry-%1").arg(index)},
        {QStringLiteral("index"), index},
        {QStringLiteral("name"), path.isEmpty()
             ? QStringLiteral("entry-%1.txt").arg(index)
             : info.fileName()},
        {QStringLiteral("localPath"), path},
        {QStringLiteral("isDir"), false},
        {QStringLiteral("isImage"), !path.isEmpty()},
        {QStringLiteral("selected"), false},
        {QStringLiteral("mtimeNs"), path.isEmpty()
             ? qint64(index + 1)
             : info.lastModified().toMSecsSinceEpoch() * 1'000'000},
        {QStringLiteral("size"), path.isEmpty()
             ? qint64(index * 17 + 1) : info.size()},
    };
}

QVariantList plainCatalog(int count) {
    QVariantList result;
    result.reserve(count);
    for (int index = 0; index < count; ++index) {
        result.append(catalogEntry(index));
    }
    return result;
}

QVariantList prefixedCatalog(const QString &prefix, int count) {
    QVariantList result;
    result.reserve(count);
    for (int index = 0; index < count; ++index) {
        QVariantMap entry = catalogEntry(index);
        entry[QStringLiteral("entryId")] = QStringLiteral("%1-entry-%2")
            .arg(prefix).arg(index);
        const bool image = index % 4 == 0;
        entry[QStringLiteral("name")] = QStringLiteral("%1-%2.%3")
            .arg(prefix).arg(index).arg(
                image ? QStringLiteral("png") : QStringLiteral("txt"));
        entry[QStringLiteral("localPath")] = QStringLiteral(
            "D:/synthetic/%1/%2.%3").arg(prefix).arg(index).arg(
                image ? QStringLiteral("png") : QStringLiteral("txt"));
        entry[QStringLiteral("isImage")] = image;
        result.append(std::move(entry));
    }
    return result;
}

class CompactIconProvider final : public QQuickImageProvider {
public:
    CompactIconProvider()
        : QQuickImageProvider(QQuickImageProvider::Image) {}

    QImage requestImage(const QString &,
                        QSize *size,
                        const QSize &requestedSize) override {
        const QSize imageSize = requestedSize.isValid()
            ? requestedSize : QSize(16, 16);
        if (size) {
            *size = imageSize;
        }
        QImage image(imageSize, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        return image;
    }
};

QObject *createPanel(QQuickView &view, QObject *session,
                     const QString &contextName,
                     const QString &initialPresentationMode =
                         QStringLiteral("masonry")) {
    view.engine()->addImportPath(QStringLiteral(ZOIN_TEST_QML_IMPORT_PATH));
    view.engine()->rootContext()->setContextProperty(contextName, session);
    auto *component = new QQmlComponent(view.engine(), &view);
    const QByteArray source = QString(R"QML(
        import QtQuick
        import ZoinGallery 1.0
        GalleryPanel {
            objectName: "modesTestPanel"
            width: 640
            height: 360
            session: %1
            presentationMode: "%2"
            autoFocus: false
            devicePixelRatio: 1
        }
    )QML").arg(contextName, initialPresentationMode).toUtf8();
    component->setData(source, QUrl(QStringLiteral("inline:ModesPanel.qml")));
    if (component->isLoading()) {
        QSignalSpy statusSpy(component, &QQmlComponent::statusChanged);
        statusSpy.wait(5000);
    }
    if (!component->isReady()) {
        qWarning().noquote() << component->errorString();
        return nullptr;
    }
    QObject *root = component->create(view.engine()->rootContext());
    if (!root) {
        qWarning().noquote() << component->errorString();
        return nullptr;
    }
    view.setContent(QUrl(QStringLiteral("inline:ModesPanel.qml")),
                    component, root);
    view.show();
    return root;
}

bool rectInside(const QRectF &outer, const QRectF &inner) {
    constexpr qreal epsilon = 0.51;
    return outer.isValid() && !outer.isEmpty() &&
        inner.isValid() && !inner.isEmpty() &&
        inner.left() >= outer.left() - epsilon &&
        inner.top() >= outer.top() - epsilon &&
        inner.right() <= outer.right() + epsilon &&
        inner.bottom() <= outer.bottom() + epsilon;
}

QQuickItem *findVisualItem(QQuickItem *root, const QString &objectName) {
    if (!root) {
        return nullptr;
    }
    QList<QQuickItem *> pending{root};
    while (!pending.isEmpty()) {
        QQuickItem *item = pending.takeLast();
        if (item->objectName() == objectName) {
            return item;
        }
        pending.append(item->childItems());
    }
    return nullptr;
}

bool invokeEnsureCurrentVisible(
        QObject *panel, bool animate = false,
        const QVariant &keyboardRevealDirection = QVariant()) {
    return QMetaObject::invokeMethod(
        panel, "ensureCurrentVisible", Qt::DirectConnection,
        Q_ARG(QVariant, QVariant(animate)),
        Q_ARG(QVariant, keyboardRevealDirection));
}

bool indexIntersectsViewport(const MasonryLayout *layout, int index) {
    const QRectF geometry = layout->indexGeometry(index);
    if (!geometry.isValid() || geometry.isEmpty()) {
        return false;
    }
    constexpr qreal epsilon = 0.51;
    if (layout->presentationMode() == MasonryLayout::Columns) {
        return geometry.right() >= layout->contentY() - epsilon &&
            geometry.left() <= layout->contentY() + layout->width() + epsilon;
    }
    return geometry.bottom() >= layout->contentY() - epsilon &&
        geometry.top() <= layout->contentY() + layout->height() + epsilon;
}

bool indexHasPaintedAreaInViewport(const MasonryLayout *layout, int index) {
    const QRectF geometry = layout->indexGeometry(index);
    if (!geometry.isValid() || geometry.isEmpty()) {
        return false;
    }
    if (layout->presentationMode() == MasonryLayout::Columns) {
        const qreal left = layout->contentY();
        const qreal right = left + layout->width();
        return geometry.left() < right && geometry.right() > left;
    }
    const qreal top = layout->contentY();
    const qreal bottom = top + layout->height();
    return geometry.top() < bottom && geometry.bottom() > top;
}

} // namespace

class MasonryLayoutModesTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    }

    void panelThemeColorsUpdateLiveAfterConstruction() {
        QQuickView view;
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        auto *runtime = ZoinGallery::GalleryRuntime::install(
            view.engine(), options);
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(
            QStringLiteral("live-panel-theme"));
        QVERIFY(session);
        QVERIFY(session->applyExternalCatalog(plainCatalog(80), 1));
        session->setCurrentIndex(1);

        QObject *panel = createPanel(
            view, session, QStringLiteral("livePanelThemeSession"));
        QVERIFY(panel);
        auto *layout = panel->findChild<MasonryLayout *>(
            QStringLiteral("galleryMasonryLayout"));
        QVERIFY(layout);
        QTRY_COMPARE_WITH_TIMEOUT(layout->count(), 80, 3000);

        const QVariantMap firstTheme{
            {QStringLiteral("panelBackground"),
             QStringLiteral("#101112")},
            {QStringLiteral("text"), QStringLiteral("#202122")},
            {QStringLiteral("cursor"), QStringLiteral("#303132")},
            {QStringLiteral("cardCursorBorder"),
             QStringLiteral("#404142")},
            {QStringLiteral("itemBackground"),
             QStringLiteral("#505152")},
            {QStringLiteral("directoryBackground"),
             QStringLiteral("#606162")},
            {QStringLiteral("itemHover"), QStringLiteral("#707172")},
            {QStringLiteral("labelBackground"),
             QStringLiteral("#80737475")},
            {QStringLiteral("previewBackdrop"),
             QStringLiteral("#90767778")},
            {QStringLiteral("scrollBarHandle"),
             QStringLiteral("#808182")},
            {QStringLiteral("scrollBarTrackHovered"),
             QStringLiteral("#909192")},
        };
        QVERIFY(panel->setProperty("theme", firstTheme));

        QQuickItem *cursorSurface = nullptr;
        QQuickItem *plainSurface = nullptr;
        QQuickItem *previewBackdrop = nullptr;
        QQuickItem *scrollBar = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT(
            (cursorSurface = panel->findChild<QQuickItem *>(
                 QStringLiteral("gallerySelectionSurface-0"))),
            3000);
        QTRY_VERIFY_WITH_TIMEOUT(
            (plainSurface = panel->findChild<QQuickItem *>(
                 QStringLiteral("gallerySelectionSurface-1"))),
            3000);
        QTRY_VERIFY_WITH_TIMEOUT(
            (previewBackdrop = panel->findChild<QQuickItem *>(
                 QStringLiteral("galleryThumbnailBackdrop-0"))),
            3000);
        QTRY_VERIFY_WITH_TIMEOUT(
            (scrollBar = panel->findChild<QQuickItem *>(
                 QStringLiteral("galleryPanelScrollBar"))),
            3000);

        QCOMPARE(panel->property("backgroundColor").value<QColor>(),
                 QColor(QStringLiteral("#101112")));
        QCOMPARE(panel->property("foregroundColor").value<QColor>(),
                 QColor(QStringLiteral("#202122")));
        QCOMPARE(cursorSurface->property("color").value<QColor>(),
                 QColor(QStringLiteral("#303132")));
        QCOMPARE(cursorSurface->property("visualBorderColor").value<QColor>(),
                 QColor(QStringLiteral("#404142")));
        QCOMPARE(plainSurface->property("color").value<QColor>(),
                 QColor(QStringLiteral("#505152")));
        QCOMPARE(previewBackdrop->property("color").value<QColor>(),
                 QColor(QStringLiteral("#90767778")));
        QCOMPARE(scrollBar->property("handleColor").value<QColor>(),
                 QColor(QStringLiteral("#808182")));
        QCOMPARE(scrollBar->property("trackHoveredColor").value<QColor>(),
                 QColor(QStringLiteral("#909192")));

        const QVariantMap secondTheme{
            {QStringLiteral("panelBackground"),
             QStringLiteral("#111213")},
            {QStringLiteral("text"), QStringLiteral("#212223")},
            {QStringLiteral("cursor"), QStringLiteral("#313233")},
            {QStringLiteral("cardCursorBorder"),
             QStringLiteral("#414243")},
            {QStringLiteral("itemBackground"),
             QStringLiteral("#515253")},
            {QStringLiteral("directoryBackground"),
             QStringLiteral("#616263")},
            {QStringLiteral("itemHover"), QStringLiteral("#717273")},
            {QStringLiteral("labelBackground"),
             QStringLiteral("#81747576")},
            {QStringLiteral("previewBackdrop"),
             QStringLiteral("#91777879")},
            {QStringLiteral("scrollBarHandle"),
             QStringLiteral("#818283")},
            {QStringLiteral("scrollBarTrackHovered"),
             QStringLiteral("#919293")},
        };
        QVERIFY(panel->setProperty("theme", secondTheme));

        QTRY_COMPARE_WITH_TIMEOUT(
            cursorSurface->property("color").value<QColor>(),
            QColor(QStringLiteral("#313233")), 3000);
        QCOMPARE(cursorSurface->property("visualBorderColor").value<QColor>(),
                 QColor(QStringLiteral("#414243")));
        QCOMPARE(plainSurface->property("color").value<QColor>(),
                 QColor(QStringLiteral("#515253")));
        QCOMPARE(previewBackdrop->property("color").value<QColor>(),
                 QColor(QStringLiteral("#91777879")));
        QCOMPARE(scrollBar->property("handleColor").value<QColor>(),
                 QColor(QStringLiteral("#818283")));
        QCOMPARE(scrollBar->property("trackHoveredColor").value<QColor>(),
                 QColor(QStringLiteral("#919293")));
        QCOMPARE(panel->findChild<QQuickItem *>(
                     QStringLiteral("gallerySelectionSurface-0")),
                 cursorSurface);
        QCOMPARE(panel->findChild<QQuickItem *>(
                     QStringLiteral("galleryPanelScrollBar")),
                 scrollBar);
    }

    void geometryAndNavigationAreModeOwnedAndDeterministic() {
        QQuickView view;
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        options.maxDecodeThreads = 2;
        auto *runtime = ZoinGallery::GalleryRuntime::install(
            view.engine(), options);
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(
            QStringLiteral("layout-mode-correctness"));
        QVERIFY(session);
        constexpr int entryCount = 140;
        QVERIFY(session->applyExternalCatalog(plainCatalog(entryCount), 1));
        session->setCurrentIndex(37);

        QObject *panel = createPanel(view, session,
                                     QStringLiteral("modesSession"));
        QVERIFY(panel);
        auto *layout = panel->findChild<MasonryLayout *>(
            QStringLiteral("galleryMasonryLayout"));
        QVERIFY(layout);
        QTRY_COMPARE_WITH_TIMEOUT(layout->count(), entryCount, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(layout->width() > 100 &&
                                 layout->height() > 100, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(layout->currentIndex(), 37, 5000);

        const QList<QPair<QString, MasonryLayout::PresentationMode>> modes{
            {QStringLiteral("masonry"), MasonryLayout::Masonry},
            {QStringLiteral("columns"), MasonryLayout::Columns},
            {QStringLiteral("details"), MasonryLayout::Details},
            {QStringLiteral("grid"), MasonryLayout::Grid},
            {QStringLiteral("icons"), MasonryLayout::Icons},
        };
        for (const auto &[name, mode] : modes) {
            panel->setProperty("presentationMode", name);
            QTRY_COMPARE_WITH_TIMEOUT(layout->presentationMode(), mode, 3000);
            if (mode == MasonryLayout::Columns) {
                layout->setWindowTopIndex(0);
            }
            QVERIFY(invokeEnsureCurrentVisible(panel));
            QCoreApplication::processEvents();
            QTRY_VERIFY_WITH_TIMEOUT(!layout->visibleIndexes().isEmpty(), 3000);
            const int previewIndex =
                layout->visibleIndexes().constFirst().toInt();
            const QRectF cell = layout->indexGeometry(previewIndex);
            const QRectF preview =
                layout->indexPreviewGeometry(previewIndex);
            QVERIFY2(rectInside(cell, preview),
                     qPrintable(QStringLiteral(
                         "%1 preview %2,%3 %4x%5 outside cell %6,%7 %8x%9")
                         .arg(name)
                         .arg(preview.x()).arg(preview.y())
                         .arg(preview.width()).arg(preview.height())
                         .arg(cell.x()).arg(cell.y())
                         .arg(cell.width()).arg(cell.height())));
            QCOMPARE(layout->currentIndex(), 37);
            QTRY_VERIFY_WITH_TIMEOUT(
                indexIntersectsViewport(layout, layout->currentIndex()),
                3000);
            QVERIFY(layout->contentY() >= 0);
            QVERIFY(layout->contentY() <=
                    qMax<qreal>(0, layout->contentHeight() - layout->height())
                        + 0.51);
            const QVariantList visible = layout->visibleIndexes();
            QVERIFY(!visible.isEmpty());
            for (const QVariant &value : visible) {
                QVERIFY(value.toInt() >= 0 && value.toInt() < entryCount);
            }
        }

        // Resizing must keep the same cursor identity anchored in the newly
        // constrained viewport instead of retaining a stale pixel offset
        // from the preceding geometry strategy.
        panel->setProperty("presentationMode", QStringLiteral("grid"));
        QTRY_COMPARE_WITH_TIMEOUT(layout->presentationMode(),
                                  MasonryLayout::Grid, 3000);
        panel->setProperty("width", 456);
        panel->setProperty("height", 238);
        QVERIFY(invokeEnsureCurrentVisible(panel));
        QCoreApplication::processEvents();
        QCOMPARE(layout->currentIndex(), 37);
        QTRY_VERIFY_WITH_TIMEOUT(indexIntersectsViewport(layout, 37), 3000);
        QVERIFY(layout->contentY() >= 0);
        QVERIFY(layout->contentY() <=
                qMax<qreal>(0, layout->contentHeight() - layout->height())
                    + 0.51);

        // Columns now owns continuous geometry across viewport-sized pages.
        // A stable-ID cursor restored outside the current page therefore has
        // geometry already, while ensure-visible moves only the viewport and
        // keeps live delegate materialization bounded.
        panel->setProperty("presentationMode", QStringLiteral("columns"));
        QTRY_COMPARE_WITH_TIMEOUT(layout->presentationMode(),
                                  MasonryLayout::Columns, 3000);
        layout->setWindowTopIndex(0);
        session->setCurrentIndex(entryCount - 1);
        QTRY_COMPARE_WITH_TIMEOUT(layout->currentIndex(), entryCount - 1,
                                  3000);
        QVERIFY(!layout->indexGeometry(entryCount - 1).isEmpty());
        QVERIFY(invokeEnsureCurrentVisible(panel));
        QTRY_VERIFY_WITH_TIMEOUT(
            !layout->indexGeometry(entryCount - 1).isEmpty(), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(
            indexIntersectsViewport(layout, entryCount - 1), 3000);
        QCOMPARE(layout->windowTopIndex(),
                 layout->windowTopIndexForIndex(entryCount - 1));
        session->setCurrentIndex(37);
        QVERIFY(invokeEnsureCurrentVisible(panel));
        QTRY_COMPARE_WITH_TIMEOUT(layout->currentIndex(), 37, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(indexIntersectsViewport(layout, 37), 3000);

        panel->setProperty("presentationMode", QStringLiteral("columns"));
        layout->setDensity(30);
        layout->setColumnCount(2);
        layout->setWindowTopIndex(0);
        const int columnRows = qMax(
            1, int(std::floor(layout->height() / layout->density())));
        const int columnCapacity = columnRows * layout->columnCount();
        QCOMPARE(layout->indexGeometry(0).y(),
                 layout->indexGeometry(columnRows).y());
        QVERIFY(layout->indexGeometry(columnRows).x() >
                layout->indexGeometry(0).x());

        // Columns is one column-major horizontal strip. A vertical wheel
        // gesture changes the horizontal content offset without relaying out
        // either the outgoing or incoming column. Repeat at both viewport
        // column counts because their distributed cell widths differ.
        for (const int columns : {2, 3}) {
            layout->setColumnCount(columns);
            layout->setWindowTopIndex(0);
            const int rows = qMax(
                1, int(std::floor((layout->height()
                    - layout->paddingTop() - layout->paddingBottom())
                    / layout->density())));
            const int capacity = rows * columns;
            const qreal cellWidth = (layout->width()
                    - layout->paddingLeft() - layout->paddingRight()) / columns;
            const int outgoing = rows - 1;
            const int incoming = rows;
            QVERIFY(incoming < entryCount);
            const QRectF outgoingGeometry =
                layout->indexGeometry(outgoing);
            const QRectF incomingGeometry =
                layout->indexGeometry(incoming);
            QCOMPARE(incomingGeometry.top(), layout->paddingTop());
            QCOMPARE(incomingGeometry.left(), cellWidth);

            const qreal beforeBoundary = cellWidth / 2.0;
            layout->setContentY(beforeBoundary);
            QCOMPARE(layout->indexGeometry(outgoing), outgoingGeometry);
            QCOMPARE(layout->indexGeometry(incoming), incomingGeometry);
            QVERIFY(indexIntersectsViewport(layout, outgoing));
            QVERIFY(indexIntersectsViewport(layout, incoming));
            QCOMPARE(layout->windowTopIndex(), 0);

            const qreal afterBoundary = cellWidth + cellWidth / 2.0;
            layout->setContentY(afterBoundary);
            QCOMPARE(layout->indexGeometry(outgoing), outgoingGeometry);
            QCOMPARE(layout->indexGeometry(incoming), incomingGeometry);
            QCOMPARE((incomingGeometry.left() - afterBoundary)
                         - (incomingGeometry.left() - beforeBoundary),
                     beforeBoundary - afterBoundary);
            QCOMPARE(layout->windowTopIndex(), rows);
        }

        layout->setColumnCount(2);
        layout->setWindowTopIndex(0);

        QVariantMap navigation = layout->navigationTarget(
            0, MasonryLayout::NavigateLeft);
        QCOMPARE(navigation.value(QStringLiteral("targetIndex")).toInt(), 0);
        navigation = layout->navigationTarget(
            qMin(5, columnRows - 1), MasonryLayout::NavigateLeft);
        QCOMPARE(navigation.value(QStringLiteral("targetIndex")).toInt(), 0);
        navigation = layout->navigationTarget(
            1, MasonryLayout::NavigateRight);
        QCOMPARE(navigation.value(QStringLiteral("targetIndex")).toInt(),
                 1 + columnRows);
        navigation = layout->navigationTarget(
            columnRows - 1, MasonryLayout::NavigateDown);
        QCOMPARE(navigation.value(QStringLiteral("targetIndex")).toInt(),
                 columnRows);
        QCOMPARE(navigation.value(QStringLiteral("windowTopIndex")).toInt(),
                 0);
        navigation = layout->navigationTarget(
            columnRows, MasonryLayout::NavigateUp);
        QCOMPARE(navigation.value(QStringLiteral("targetIndex")).toInt(),
                 columnRows - 1);
        QCOMPARE(navigation.value(QStringLiteral("windowTopIndex")).toInt(),
                 0);
        navigation = layout->navigationTarget(
            columnCapacity - 1, MasonryLayout::NavigateRight);
        QCOMPARE(navigation.value(QStringLiteral("windowTopIndex")).toInt(),
                 0);
        QCOMPARE(navigation.value(QStringLiteral("targetIndex")).toInt(),
                 qMin(entryCount - 1,
                      columnCapacity - 1 + columnRows));
        const int maximumTop = layout->windowTopIndexForIndex(entryCount - 1);
        QCOMPARE(layout->windowTopIndexForIndex(entryCount - 1),
                 maximumTop);
        layout->setWindowTopIndex(maximumTop);
        QCOMPARE(layout->windowTopIndexForIndex(0), 0);
        navigation = layout->navigationTarget(
            entryCount - 1, MasonryLayout::NavigateRight);
        QCOMPARE(navigation.value(QStringLiteral("targetIndex")).toInt(),
                 entryCount - 1);
        QCOMPARE(navigation.value(QStringLiteral("windowTopIndex")).toInt(),
                 maximumTop);
        navigation = layout->navigationTarget(
            entryCount - 1, MasonryLayout::NavigateDown);
        QCOMPARE(navigation.value(QStringLiteral("targetIndex")).toInt(),
                 entryCount - 1);

        panel->setProperty("presentationMode", QStringLiteral("details"));
        layout->setDensity(30);
        const int detailPage = qMax(
            1, int(std::floor(layout->height() / layout->density())));
        navigation = layout->navigationTarget(
            detailPage + 2, MasonryLayout::NavigateLeft);
        QCOMPARE(navigation.value(QStringLiteral("targetIndex")).toInt(), 2);
        navigation = layout->navigationTarget(
            detailPage + 2, MasonryLayout::NavigateDown);
        QCOMPARE(navigation.value(QStringLiteral("targetIndex")).toInt(),
                 detailPage + 3);
        navigation = layout->navigationTarget(
            detailPage + 2, MasonryLayout::NavigateUp, true);
        QCOMPARE(navigation.value(QStringLiteral("targetIndex")).toInt(), 2);

        panel->setProperty("presentationMode", QStringLiteral("grid"));
        layout->setDensity(120);
        const int gridColumns = qMax(
            1, int(std::floor(layout->width() / layout->density())));
        const int gridRows = qMax(
            1, int(std::floor(layout->height() / layout->density())));
        const int gridPage = gridColumns * gridRows;
        navigation = layout->navigationTarget(
            gridColumns + 2, MasonryLayout::NavigateUp);
        QCOMPARE(navigation.value(QStringLiteral("targetIndex")).toInt(), 2);
        navigation = layout->navigationTarget(
            gridColumns + 2, MasonryLayout::NavigateDown);
        QCOMPARE(navigation.value(QStringLiteral("targetIndex")).toInt(),
                 gridColumns * 2 + 2);
        navigation = layout->navigationTarget(
            2, MasonryLayout::NavigateDown, true);
        QCOMPARE(navigation.value(QStringLiteral("targetIndex")).toInt(),
                 qMin(entryCount - 1, 2 + gridPage));

        panel->setProperty("presentationMode", QStringLiteral("icons"));
        layout->setDensity(128);
        const int iconColumns = qMax(
            1, int(std::floor(layout->width() / layout->density())));
        navigation = layout->navigationTarget(
            iconColumns + 1, MasonryLayout::NavigateUp);
        QCOMPARE(navigation.value(QStringLiteral("targetIndex")).toInt(), 1);
        navigation = layout->navigationTarget(
            entryCount - 1, MasonryLayout::NavigateRight);
        QCOMPARE(navigation.value(QStringLiteral("targetIndex")).toInt(),
                 entryCount - 1);

        panel->setProperty("presentationMode", QStringLiteral("masonry"));
        navigation = layout->navigationTarget(
            12, MasonryLayout::NavigateLeft);
        QCOMPARE(navigation.value(QStringLiteral("targetIndex")).toInt(), 11);
        navigation = layout->navigationTarget(
            12, MasonryLayout::NavigateRight);
        QCOMPARE(navigation.value(QStringLiteral("targetIndex")).toInt(), 13);
    }

    void keyboardRevealKeepsCompactLeadingEdgesAligned() {
        QQuickView view;
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        options.maxDecodeThreads = 2;
        auto *runtime = ZoinGallery::GalleryRuntime::install(
            view.engine(), options);
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(
            QStringLiteral("keyboard-leading-edge-alignment"));
        QVERIFY(session);
        constexpr int entryCount = 240;
        QVERIFY(session->applyExternalCatalog(plainCatalog(entryCount), 1));
        session->setCurrentIndex(0);

        QObject *panel = createPanel(
            view, session, QStringLiteral("keyboardAlignmentSession"),
            QStringLiteral("columns"));
        QVERIFY(panel);
        auto *panelItem = qobject_cast<QQuickItem *>(panel);
        auto *layout = panel->findChild<MasonryLayout *>(
            QStringLiteral("galleryMasonryLayout"));
        QVERIFY(panelItem && layout);

        // At 1.75 DPR the resulting 631.5 logical-pixel canvas is 1105.125
        // physical pixels wide. Its integral 1105-pixel span leaves exactly
        // one physical remainder pixel for both two and three columns.
        panelItem->setWidth(643.5);
        panelItem->setHeight(360);
        QTRY_COMPARE_WITH_TIMEOUT(layout->count(), entryCount, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(layout->presentationMode(),
                                  MasonryLayout::Columns, 3000);
        constexpr qreal testDpr = 1.75;
        layout->setDevicePixelRatio(testDpr);

        panelItem->forceActiveFocus();
        view.requestActivate();
        QVERIFY(panelItem->hasActiveFocus());

        const auto delegateForIndex = [&](int index) -> QQuickItem * {
            const auto *surface = findVisualItem(
                panelItem,
                QStringLiteral("gallerySelectionSurface-%1").arg(index));
            return surface ? surface->parentItem() : nullptr;
        };
        const auto sendKey = [&](Qt::Key key) {
            QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
            QCoreApplication::sendEvent(&view, &press);
            QVERIFY(press.isAccepted());
            QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
            QCoreApplication::sendEvent(&view, &release);
            QVERIFY(release.isAccepted());
            QCoreApplication::processEvents();
        };

        // The viewport width is intentionally not divisible by either two or
        // three, and the fractional DPR makes logical-pixel rounding
        // insufficient. Every delegate and every scroll step must share one
        // exact physical-pixel stride.
        for (const int columns : {2, 3}) {
            panel->setProperty("presentationMode", QStringLiteral("columns"));
            layout->setDensity(30);
            layout->setColumnCount(columns);
            layout->setWindowTopIndex(0);
            layout->setContentY(0);
            session->setCurrentIndex(0);
            QCoreApplication::processEvents();
            QTRY_COMPARE_WITH_TIMEOUT(layout->presentationMode(),
                                      MasonryLayout::Columns, 3000);
            QTRY_VERIFY_WITH_TIMEOUT(layout->contentY() < 0.01, 3000);

            const int rows = qMax(
                1, int(std::floor((layout->height()
                    - layout->paddingTop() - layout->paddingBottom())
                    / layout->density())));
            QVERIFY(rows > 1);
            const qreal stride = layout->columnStride();
            QVERIFY(stride > 0);
            QVERIFY(qAbs(stride * testDpr
                         - qRound(stride * testDpr)) < 0.0001);
            for (int column = 0; column < columns; ++column) {
                QQuickItem *columnDelegate = nullptr;
                QTRY_VERIFY_WITH_TIMEOUT(
                    (columnDelegate = delegateForIndex(column * rows))
                        != nullptr,
                    3000);
                QVERIFY(qAbs(columnDelegate->x() - column * stride)
                        < 0.0001);
                QVERIFY(qAbs(columnDelegate->width() - stride) < 0.0001);
            }
            QQuickItem *first = nullptr;
            QTRY_VERIFY_WITH_TIMEOUT(
                (first = delegateForIndex(0)) != nullptr, 3000);
            const qreal firstColumnX = first->x() - layout->contentY();

            // Move into the first column which was not initially visible.
            // The new leftmost column is index rows after the viewport moves
            // by one column.
            for (int step = 0; step < columns; ++step)
                sendKey(Qt::Key_Right);
            QCOMPARE(session->currentIndex(), columns * rows);
            QQuickItem *newLeft = nullptr;
            QTRY_VERIFY_WITH_TIMEOUT(
                (newLeft = delegateForIndex(rows)) != nullptr, 3000);
            QVERIFY(qAbs(newLeft->width() - stride) < 0.0001);
            QVERIFY2(
                qAbs(newLeft->x() - layout->contentY() - firstColumnX)
                    < 0.01,
                qPrintable(QStringLiteral(
                    "%1 columns: left edge drifted: first=%2 new=%3 "
                    "contentY=%4")
                    .arg(columns)
                    .arg(firstColumnX)
                    .arg(newLeft->x() - layout->contentY())
                    .arg(layout->contentY())));

            // The reverse keyboard reveal must return to the same origin.
            for (int step = 0; step < columns; ++step)
                sendKey(Qt::Key_Left);
            QVERIFY(qAbs(layout->contentY()) < 0.01);
            QQuickItem *returnedFirst = nullptr;
            QTRY_VERIFY_WITH_TIMEOUT(
                (returnedFirst = delegateForIndex(0)) != nullptr, 3000);
            QVERIFY(qAbs(returnedFirst->x() - layout->contentY()
                         - firstColumnX) < 0.01);

            // The terminal clamp must stay on the same column lattice too.
            // contentHeight used to omit the viewport's trailing remainder,
            // making the last screen stop one physical pixel before the exact
            // stride and shifting every visible delegate to the right.
            const qreal physicalCanvas = std::floor(
                (layout->width() - layout->paddingLeft()
                 - layout->paddingRight()) * testDpr + 0.000001);
            const qreal physicalRemainder = physicalCanvas
                - columns * stride * testDpr;
            QVERIFY(physicalRemainder > 0.5);
            const int totalColumns = (entryCount + rows - 1) / rows;
            const int terminalLeftColumn = qMax(0, totalColumns - columns);
            const int terminalLeftIndex = terminalLeftColumn * rows;

            sendKey(Qt::Key_End);
            QCOMPARE(session->currentIndex(), entryCount - 1);
            QQuickItem *terminalLeft = nullptr;
            QTRY_VERIFY_WITH_TIMEOUT(
                (terminalLeft = delegateForIndex(terminalLeftIndex)) != nullptr,
                3000);
            QVERIFY2(qAbs(layout->contentY()
                          - terminalLeftColumn * stride) < 0.0001,
                     qPrintable(QStringLiteral(
                         "%1 columns: terminal offset left the lattice: "
                         "contentY=%2 expected=%3 remainderPhysical=%4")
                         .arg(columns)
                         .arg(layout->contentY())
                         .arg(terminalLeftColumn * stride)
                         .arg(physicalRemainder)));
            QVERIFY2(qAbs(terminalLeft->x() - layout->contentY()
                          - firstColumnX) < 0.0001,
                     qPrintable(QStringLiteral(
                         "%1 columns: terminal left edge drifted: first=%2 "
                         "terminal=%3")
                         .arg(columns)
                         .arg(firstColumnX)
                         .arg(terminalLeft->x() - layout->contentY())));

            sendKey(Qt::Key_Home);
            QCOMPARE(session->currentIndex(), 0);
            QVERIFY(qAbs(layout->contentY()) < 0.0001);
        }

        // Details keeps its native fractional row extent. Keyboard reveal
        // must move by complete row pitches so the new top row has exactly
        // the same screen Y as row zero at the top of the list.
        view.hide();
        QQuickView detailsView;
        auto *detailsRuntime = ZoinGallery::GalleryRuntime::install(
            detailsView.engine(), options);
        QVERIFY(detailsRuntime);
        auto *detailsSession = detailsRuntime->createExternalSession(
            QStringLiteral("keyboard-leading-edge-details"));
        QVERIFY(detailsSession);
        QVERIFY(detailsSession->applyExternalCatalog(
            plainCatalog(entryCount), 1));
        detailsSession->setCurrentIndex(0);
        QObject *detailsPanel = createPanel(
            detailsView, detailsSession,
            QStringLiteral("keyboardAlignmentDetailsSession"),
            QStringLiteral("details"));
        QVERIFY(detailsPanel);
        auto *detailsPanelItem = qobject_cast<QQuickItem *>(detailsPanel);
        auto *detailsLayout = detailsPanel->findChild<MasonryLayout *>(
            QStringLiteral("galleryMasonryLayout"));
        QVERIFY(detailsPanelItem && detailsLayout);
        detailsPanelItem->setWidth(641);
        detailsPanelItem->setHeight(360);
        detailsPanel->setProperty("showDetailsHeader", false);
        detailsLayout->setDensity(24.2);
        detailsLayout->setPaddingTop(2.75);
        detailsLayout->setPaddingBottom(1.5);
        QTRY_COMPARE_WITH_TIMEOUT(detailsLayout->presentationMode(),
                                  MasonryLayout::Details, 3000);
        detailsPanelItem->forceActiveFocus();
        detailsView.requestActivate();
        QTRY_VERIFY_WITH_TIMEOUT(detailsPanelItem->hasActiveFocus(), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(detailsLayout->contentY() < 0.01, 3000);
        const auto detailsDelegateForIndex = [&](int index) -> QQuickItem * {
            const auto *surface = findVisualItem(
                detailsPanelItem,
                QStringLiteral("gallerySelectionSurface-%1").arg(index));
            return surface ? surface->parentItem() : nullptr;
        };
        const auto sendDetailsKey = [&](Qt::Key key) {
            QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
            QCoreApplication::sendEvent(&detailsView, &press);
            QVERIFY(press.isAccepted());
            QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
            QCoreApplication::sendEvent(&detailsView, &release);
            QVERIFY(release.isAccepted());
            QCoreApplication::processEvents();
        };
        QQuickItem *firstRow = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT(
            (firstRow = detailsDelegateForIndex(0)) != nullptr, 3000);
        const qreal firstRowY = firstRow->y() - detailsLayout->contentY();

        int firstOffscreenRow = 1;
        while (firstOffscreenRow < entryCount
               && detailsLayout->indexGeometry(firstOffscreenRow).bottom()
                      <= detailsLayout->height() + 0.01)
            ++firstOffscreenRow;
        QVERIFY(firstOffscreenRow > 1);
        for (int step = 0; step < firstOffscreenRow; ++step)
            sendDetailsKey(Qt::Key_Down);
        QCOMPARE(detailsSession->currentIndex(), firstOffscreenRow);

        QQuickItem *newTopRow = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT(
            (newTopRow = detailsDelegateForIndex(1)) != nullptr, 3000);
        QVERIFY2(
            qAbs(newTopRow->y() - detailsLayout->contentY() - firstRowY)
                < 0.01,
            qPrintable(QStringLiteral(
                "details: top row drifted: first=%1 new=%2 contentY=%3")
                .arg(firstRowY)
                .arg(newTopRow->y() - detailsLayout->contentY())
                .arg(detailsLayout->contentY())));

        for (int step = 0; step < firstOffscreenRow; ++step)
            sendDetailsKey(Qt::Key_Up);
        QVERIFY(qAbs(detailsLayout->contentY()) < 0.01);
        QQuickItem *returnedFirstRow = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT(
            (returnedFirstRow = detailsDelegateForIndex(0)) != nullptr, 3000);
        QVERIFY(qAbs(returnedFirstRow->y() - detailsLayout->contentY()
                     - firstRowY) < 0.01);

        // The terminal clamp must land on the row lattice as well.  A
        // fractional viewport remainder used to shorten maximum contentY,
        // leaving the top row of the final screen lower than row zero.
        const qreal usableDetailsHeight = detailsLayout->height()
            - detailsLayout->paddingTop() - detailsLayout->paddingBottom();
        const int fullVisibleRows = qMax(
            1, int(std::floor(usableDetailsHeight
                              / detailsLayout->density())));
        const qreal trailingRowRemainder = usableDetailsHeight
            - fullVisibleRows * detailsLayout->density();
        QVERIFY(trailingRowRemainder > 0.01);
        const int terminalTopIndex = qMax(0, entryCount - fullVisibleRows);

        sendDetailsKey(Qt::Key_End);
        QCOMPARE(detailsSession->currentIndex(), entryCount - 1);
        QQuickItem *terminalTopRow = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT(
            (terminalTopRow = detailsDelegateForIndex(terminalTopIndex))
                != nullptr,
            3000);
        QVERIFY2(
            qAbs(detailsLayout->contentY()
                 - terminalTopIndex * detailsLayout->density()) < 0.0001,
            qPrintable(QStringLiteral(
                "details: terminal offset left the row lattice: "
                "contentY=%1 expected=%2 remainder=%3")
                .arg(detailsLayout->contentY())
                .arg(terminalTopIndex * detailsLayout->density())
                .arg(trailingRowRemainder)));
        QVERIFY2(
            qAbs(terminalTopRow->y() - detailsLayout->contentY()
                 - firstRowY) < 0.0001,
            qPrintable(QStringLiteral(
                "details: terminal top row drifted: first=%1 terminal=%2")
                .arg(firstRowY)
                .arg(terminalTopRow->y() - detailsLayout->contentY())));

        sendDetailsKey(Qt::Key_Home);
        QCOMPARE(detailsSession->currentIndex(), 0);
        QVERIFY(qAbs(detailsLayout->contentY()) < 0.0001);

        detailsRuntime->shutdown();
        runtime->shutdown();
    }

    void iconRowsGrowForWrappedFourLineMiddleElidedLabels() {
        QVariantList catalog;
        const QString longName = QStringLiteral(
            "Beginning-of-a-very-long-file-name-with-many-descriptive-"
            "words-and-a-middle-section-that-must-not-all-be-visible-"
            "while-the-final-part-remains-important.extension");
        for (int index = 0; index < 12; ++index) {
            QVariantMap entry = catalogEntry(index);
            entry[QStringLiteral("name")] = index == 2
                ? longName
                : QStringLiteral("short-%1.txt").arg(index);
            catalog.append(entry);
        }

        QQuickView view;
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        options.maxDecodeThreads = 2;
        auto *runtime = ZoinGallery::GalleryRuntime::install(
            view.engine(), options);
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(
            QStringLiteral("variable-icon-row-labels"));
        QVERIFY(session);
        QVERIFY(session->applyExternalCatalog(catalog, 1));

        QObject *panel = createPanel(
            view, session, QStringLiteral("variableIconRowsSession"),
            QStringLiteral("icons"));
        QVERIFY(panel);
        panel->setProperty("density", 96.0);
        auto *layout = panel->findChild<MasonryLayout *>(
            QStringLiteral("galleryMasonryLayout"));
        QVERIFY(layout);
        QTRY_COMPARE_WITH_TIMEOUT(layout->presentationMode(),
                                  MasonryLayout::Icons, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(layout->count(), 12, 3000);

        const int columns = qMax(
            1, int(std::floor(layout->width() / layout->density())));
        QVERIFY(columns >= 2);
        const int secondRow = columns;
        QVERIFY(secondRow < layout->count());
        const QRectF firstShort = layout->indexGeometry(0);
        const QRectF firstLong = layout->indexGeometry(2);
        const QRectF secondShort = layout->indexGeometry(secondRow);
        QCOMPARE(firstShort.height(), firstLong.height());
        QVERIFY2(firstLong.height() > secondShort.height(),
                 qPrintable(QStringLiteral("long row %1, short row %2")
                                .arg(firstLong.height())
                                .arg(secondShort.height())));
        QCOMPARE(secondShort.height(), secondShort.width());
        QCOMPARE(secondShort.top(), firstShort.bottom());

        // Filename wrapping changes only the row/cell height. Preview size is
        // invariant across short and long labels and across physical rows.
        const QRectF shortPreview = layout->indexPreviewGeometry(0);
        const QRectF longPreview = layout->indexPreviewGeometry(2);
        const QRectF secondPreview =
            layout->indexPreviewGeometry(secondRow);
        QCOMPARE(shortPreview.size(), longPreview.size());
        QCOMPARE(shortPreview.size(), secondPreview.size());
        QCOMPARE(shortPreview.left(), firstShort.left());
        QCOMPARE(shortPreview.top(), firstShort.top());
        QCOMPARE(shortPreview.width(), firstShort.width());
        QCOMPARE(secondPreview.left(), secondShort.left());
        QCOMPARE(secondPreview.top(), secondShort.top());

        auto *fallbackIcon = panel->findChild<QQuickItem *>(
            QStringLiteral("galleryFallbackIcon-0"));
        auto *backdrop = panel->findChild<QQuickItem *>(
            QStringLiteral("galleryThumbnailBackdrop-0"));
        auto *shader = panel->findChild<QQuickItem *>(
            QStringLiteral("galleryThumbnailShader-0"));
        QVERIFY(fallbackIcon);
        QVERIFY(backdrop);
        QVERIFY(shader);
        QTRY_VERIFY_WITH_TIMEOUT(
            qAbs(fallbackIcon->width()
                 - qMin(shortPreview.width(), shortPreview.height())) <= 0.51,
            3000);
        QVERIFY(!backdrop->property("enabledForPresentation").toBool());
        QVERIFY(!backdrop->isVisible());
        QVERIFY(!shader->property("showCheckerboard").toBool());

        auto *longLabel = panel->findChild<QQuickItem *>(
            QStringLiteral("galleryIconsLabel-2"));
        auto *shortLabel = panel->findChild<QQuickItem *>(
            QStringLiteral("galleryIconsLabel-%1").arg(secondRow));
        QVERIFY(longLabel);
        QVERIFY(shortLabel);
        QTRY_VERIFY_WITH_TIMEOUT(longLabel->isVisible(), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(
            qAbs(shortLabel->parentItem()->width()
                 - secondShort.width()) <= 0.51
                && qAbs(shortLabel->parentItem()->height()
                        - secondShort.height()) <= 0.51,
            3000);
        const QString displayed = longLabel->property("text").toString();
        QVERIFY(displayed != longName);
        QVERIFY(displayed.contains(QChar(0x2026)));
        QVERIFY(displayed.startsWith(QStringLiteral("Beginning")));
        QVERIFY(displayed.endsWith(QStringLiteral("extension")));
        QCOMPARE(longLabel->property("maximumLineCount").toInt(), 4);
        QVERIFY(longLabel->property("lineCount").toInt() <= 4);
        QCOMPARE(shortLabel->property("lineCount").toInt(), 1);
        QVERIFY(qAbs(shortLabel->height()
                     - shortLabel->implicitHeight()) <= 1.0);

        auto *longSelection = panel->findChild<QQuickItem *>(
            QStringLiteral("gallerySelectionSurface-2"));
        auto *shortSelection = panel->findChild<QQuickItem *>(
            QStringLiteral("gallerySelectionSurface-%1").arg(secondRow));
        auto *sameRowShortSelection = panel->findChild<QQuickItem *>(
            QStringLiteral("gallerySelectionSurface-0"));
        auto *sameRowShortLabel = panel->findChild<QQuickItem *>(
            QStringLiteral("galleryIconsLabel-0"));
        QVERIFY(longSelection);
        QVERIFY(shortSelection);
        QVERIFY(sameRowShortSelection);
        QVERIFY(sameRowShortLabel);
        QTRY_VERIFY_WITH_TIMEOUT(
            longSelection->height() > sameRowShortSelection->height(), 3000);
        QVERIFY2(sameRowShortSelection->height()
                     < firstShort.height() - 1,
                 "single-line selection must not consume its row's unused "
                 "multi-line label space");
        QVERIFY(shortSelection->height() <= secondShort.height());
        QVERIFY(sameRowShortSelection->height() < firstShort.height() - 1);
        QVERIFY(longSelection->height() <= firstLong.height());
        QCOMPARE(layout->contentHeight(),
                 layout->indexGeometry(layout->count() - 1).bottom());

        // Grid remains the equal-height, one-line presentation.
        panel->setProperty("presentationMode", QStringLiteral("grid"));
        QTRY_COMPARE_WITH_TIMEOUT(layout->presentationMode(),
                                  MasonryLayout::Grid, 3000);
        QCOMPARE(layout->indexGeometry(0).height(), layout->density());
        QCOMPARE(layout->indexGeometry(2).height(), layout->density());

        runtime->shutdown();
    }

    void presentationSwitchInstantlyRevealsStableCurrentItem() {
        QQuickView view;
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        options.maxDecodeThreads = 2;
        auto *runtime = ZoinGallery::GalleryRuntime::install(
            view.engine(), options);
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(
            QStringLiteral("layout-mode-switch-reveal"));
        QVERIFY(session);
        constexpr int entryCount = 240;
        constexpr int selectedIndex = 137;
        QVERIFY(session->applyExternalCatalog(plainCatalog(entryCount), 1));
        session->setCurrentIndex(selectedIndex);

        QObject *panel = createPanel(
            view, session, QStringLiteral("modeSwitchSession"),
            QStringLiteral("icons"));
        QVERIFY(panel);
        auto *layout = panel->findChild<MasonryLayout *>(
            QStringLiteral("galleryMasonryLayout"));
        auto *scrollAnimation = panel->findChild<QObject *>(
            QStringLiteral("galleryPanelScrollAnimation"));
        QVERIFY(layout);
        QVERIFY(scrollAnimation);
        QTRY_COMPARE_WITH_TIMEOUT(layout->count(), entryCount, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(layout->presentationMode(),
                                  MasonryLayout::Icons, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(
            indexIntersectsViewport(layout, selectedIndex), 3000);
        QVERIFY(!scrollAnimation->property("running").toBool());

        // Put the cursor at an unambiguous interior viewport Y. The old Icons
        // contentY addresses a completely different Grid row, but the cursor
        // itself should remain at this screen coordinate after rewrap.
        const QRectF initialIconGeometry =
            layout->indexGeometry(selectedIndex);
        constexpr qreal requestedViewportY = 80.0;
        layout->setContentY(initialIconGeometry.top() - requestedViewportY);
        session->setPanelScrollOffset(layout->contentY());
        const qreal iconCursorViewportY =
            layout->indexGeometry(selectedIndex).top() - layout->contentY();
        QVERIFY(qAbs(iconCursorViewportY - requestedViewportY) < 0.51);

        // The old Icons contentY addresses a completely different Grid row.
        // Switching must not restore that raw number or animate toward the
        // selected item after the new mode has already painted.
        const qreal iconsContentY = layout->contentY();
        panel->setProperty("presentationMode", QStringLiteral("grid"));
        // The host applies the newly selected mode and its saved zoom in one
        // transaction. Neither rewrap may animate recycled delegate geometry.
        panel->setProperty("density", 176.0);
        QTRY_COMPARE_WITH_TIMEOUT(layout->presentationMode(),
                                  MasonryLayout::Grid, 3000);
        QCoreApplication::processEvents();
        QVERIFY(indexIntersectsViewport(layout, selectedIndex));
        QVERIFY(!scrollAnimation->property("running").toBool());
        QVERIFY(qAbs(layout->contentY() - iconsContentY) > 0.5);
        const qreal gridCursorViewportY =
            layout->indexGeometry(selectedIndex).top() - layout->contentY();
        QVERIFY(qAbs(gridCursorViewportY - iconCursorViewportY) < 0.51);
        QCOMPARE(session->currentIndex(), selectedIndex);
        QCOMPARE(layout->currentIndex(), selectedIndex);
        QQuickItem *selectedDelegate = nullptr;
        for (QQuickItem *candidate :
             layout->findChildren<QQuickItem *>()) {
            if (candidate->property("viewIndex").toInt() == selectedIndex) {
                selectedDelegate = candidate;
                break;
            }
        }
        QVERIFY(selectedDelegate);
        const auto delegateGeometry = [selectedDelegate] {
            return QRectF(selectedDelegate->x(), selectedDelegate->y(),
                          selectedDelegate->width(),
                          selectedDelegate->height());
        };
        QCOMPARE(delegateGeometry(), layout->indexGeometry(selectedIndex));
        const QRectF settledGridGeometry = delegateGeometry();
        QTest::qWait(80);
        QCOMPARE(delegateGeometry(), settledGridGeometry);
        QTRY_VERIFY_WITH_TIMEOUT(
            qAbs(session->panelScrollOffset() - layout->contentY()) < 0.51,
            1000);

        const qreal gridContentY = layout->contentY();
        panel->setProperty("presentationMode", QStringLiteral("icons"));
        QTRY_COMPARE_WITH_TIMEOUT(layout->presentationMode(),
                                  MasonryLayout::Icons, 3000);
        QCoreApplication::processEvents();
        QVERIFY(indexIntersectsViewport(layout, selectedIndex));
        QVERIFY(!scrollAnimation->property("running").toBool());
        QVERIFY(qAbs(layout->contentY() - gridContentY) > 0.5);
        const qreal restoredIconCursorViewportY =
            layout->indexGeometry(selectedIndex).top() - layout->contentY();
        QVERIFY(qAbs(restoredIconCursorViewportY
                     - gridCursorViewportY) < 0.51);
        QCOMPARE(session->currentIndex(), selectedIndex);
    }

    void shiftRangeNavigationMatchesOriginalInEveryMode() {
        const QStringList modes = {
            QStringLiteral("masonry"), QStringLiteral("grid"),
            QStringLiteral("icons"), QStringLiteral("details"),
            QStringLiteral("columns")};
        const QList<Qt::Key> keys = {
            Qt::Key_Left, Qt::Key_Right, Qt::Key_Up, Qt::Key_Down,
            Qt::Key_PageUp, Qt::Key_PageDown, Qt::Key_Home, Qt::Key_End};

        for (const QString &mode : modes) {
            QQuickView view;
            ZoinGallery::RuntimeOptions options;
            options.persistentCache = false;
            auto *runtime = ZoinGallery::GalleryRuntime::install(
                view.engine(), options);
            QVERIFY(runtime);
            auto *session = runtime->createExternalSession(
                QStringLiteral("shift-range-%1").arg(mode));
            QVERIFY(session);
            QVERIFY(session->applyExternalCatalog(plainCatalog(80), 1));
            session->setCurrentIndex(40);
            QObject *panel = createPanel(
                view, session, QStringLiteral("shiftRangeSession"), mode);
            QVERIFY(panel);
            auto *panelItem = qobject_cast<QQuickItem *>(panel);
            auto *layout = panel->findChild<MasonryLayout *>(
                QStringLiteral("galleryMasonryLayout"));
            QVERIFY(panelItem);
            QVERIFY(layout);
            QTRY_COMPARE_WITH_TIMEOUT(layout->count(), 80, 3000);
            panelItem->forceActiveFocus();
            view.requestActivate();
            QSignalSpy selectionSpy(
                panel, SIGNAL(selectionRequested(QString,QVariant)));
            QVERIFY(selectionSpy.isValid());

            for (Qt::Key key : keys) {
                session->setCurrentIndex(40);
                QVERIFY(invokeEnsureCurrentVisible(panel, false));
                QVERIFY(QMetaObject::invokeMethod(
                    panel, "cancelCursorChromeTransition",
                    Qt::DirectConnection));
                selectionSpy.clear();

                QKeyEvent shiftPress(QEvent::KeyPress, Qt::Key_Shift,
                                     Qt::ShiftModifier);
                QCoreApplication::sendEvent(&view, &shiftPress);
                QKeyEvent keyPress(QEvent::KeyPress, key,
                                   Qt::ShiftModifier);
                QCoreApplication::sendEvent(&view, &keyPress);
                QVERIFY2(keyPress.isAccepted(), qPrintable(mode));
                QVERIFY2(session->currentIndex() != 40,
                         qPrintable(mode + QStringLiteral(" key %1")
                                              .arg(int(key))));
                QCOMPARE(selectionSpy.size(), 0);
                QVERIFY(panel->property(
                    "keyboardShiftSelectionActive").toBool());

                QKeyEvent keyRelease(QEvent::KeyRelease, key,
                                     Qt::ShiftModifier);
                QCoreApplication::sendEvent(&view, &keyRelease);
                QCOMPARE(selectionSpy.size(), 0);
                QKeyEvent shiftRelease(QEvent::KeyRelease, Qt::Key_Shift,
                                       Qt::NoModifier);
                QCoreApplication::sendEvent(&view, &shiftRelease);
                QCOMPARE(selectionSpy.size(), 1);
                QCOMPARE(selectionSpy.constFirst().at(0).toString(),
                         QStringLiteral("add"));
                QVERIFY(!selectionSpy.constFirst().at(1).toList().isEmpty());
                QVERIFY(!panel->property(
                    "keyboardShiftSelectionActive").toBool());
            }
        }
    }

    void shiftRangeNavigationRemovesWhenAnchorIsSelectedInEveryMode() {
        const QStringList modes = {
            QStringLiteral("masonry"), QStringLiteral("grid"),
            QStringLiteral("icons"), QStringLiteral("details"),
            QStringLiteral("columns")};
        const QList<Qt::Key> keys = {
            Qt::Key_Left, Qt::Key_Right, Qt::Key_Up, Qt::Key_Down,
            Qt::Key_PageUp, Qt::Key_PageDown, Qt::Key_Home, Qt::Key_End};

        for (const QString &mode : modes) {
            QQuickView view;
            ZoinGallery::RuntimeOptions options;
            options.persistentCache = false;
            auto *runtime = ZoinGallery::GalleryRuntime::install(
                view.engine(), options);
            QVERIFY(runtime);
            auto *session = runtime->createExternalSession(
                QStringLiteral("shift-remove-range-%1").arg(mode));
            QVERIFY(session);
            QVERIFY(session->applyExternalCatalog(plainCatalog(80), 1));
            QObject *panel = createPanel(
                view, session, QStringLiteral("shiftRemoveRangeSession"), mode);
            QVERIFY(panel);
            auto *panelItem = qobject_cast<QQuickItem *>(panel);
            auto *layout = panel->findChild<MasonryLayout *>(
                QStringLiteral("galleryMasonryLayout"));
            QVERIFY(panelItem);
            QVERIFY(layout);
            QTRY_COMPARE_WITH_TIMEOUT(layout->count(), 80, 3000);
            panelItem->forceActiveFocus();
            view.requestActivate();
            QSignalSpy selectionSpy(
                panel, SIGNAL(selectionRequested(QString,QVariant)));
            QVERIFY(selectionSpy.isValid());

            qulonglong selectionRevision = 1;
            const QString anchorId = session->entryIdAt(40);
            QVERIFY(!anchorId.isEmpty());
            for (Qt::Key key : keys) {
                // The state at the start of the physical Shift hold fixes the
                // range operation. A selected anchor must make the complete
                // preview a deselection, in every layout and direction.
                QVERIFY(session->applyExternalState(
                    anchorId, 40, QStringList{anchorId},
                    ++selectionRevision));
                QVERIFY(invokeEnsureCurrentVisible(panel, false));
                QVERIFY(QMetaObject::invokeMethod(
                    panel, "cancelCursorChromeTransition",
                    Qt::DirectConnection));
                selectionSpy.clear();

                QKeyEvent shiftPress(QEvent::KeyPress, Qt::Key_Shift,
                                     Qt::ShiftModifier);
                QCoreApplication::sendEvent(&view, &shiftPress);
                QKeyEvent keyPress(QEvent::KeyPress, key,
                                   Qt::ShiftModifier);
                QCoreApplication::sendEvent(&view, &keyPress);
                QVERIFY2(keyPress.isAccepted(), qPrintable(mode));
                QVERIFY2(session->currentIndex() != 40,
                         qPrintable(mode + QStringLiteral(" key %1")
                                              .arg(int(key))));
                QCOMPARE(selectionSpy.size(), 0);
                QVERIFY(panel->property(
                    "keyboardShiftSelectionActive").toBool());
                QVERIFY(!panel->property(
                    "keyboardShiftSelectionAdds").toBool());

                QVariant anchorSelected;
                QVERIFY(QMetaObject::invokeMethod(
                    panel, "effectiveEntrySelected", Qt::DirectConnection,
                    Q_RETURN_ARG(QVariant, anchorSelected),
                    Q_ARG(QVariant, anchorId),
                    Q_ARG(QVariant, true)));
                QVERIFY2(!anchorSelected.toBool(), qPrintable(mode));

                QKeyEvent keyRelease(QEvent::KeyRelease, key,
                                     Qt::ShiftModifier);
                QCoreApplication::sendEvent(&view, &keyRelease);
                QCOMPARE(selectionSpy.size(), 0);
                QKeyEvent shiftRelease(QEvent::KeyRelease, Qt::Key_Shift,
                                       Qt::NoModifier);
                QCoreApplication::sendEvent(&view, &shiftRelease);
                QCOMPARE(selectionSpy.size(), 1);
                QCOMPARE(selectionSpy.constFirst().at(0).toString(),
                         QStringLiteral("remove"));
                const QVariantList removedIds =
                    selectionSpy.constFirst().at(1).toList();
                QVERIFY(removedIds.contains(anchorId));
                QVERIFY(!panel->property(
                    "keyboardShiftSelectionActive").toBool());

                // Acknowledge the removal so the next physical hold starts
                // from a clean authoritative selection revision.
                QVERIFY(session->applyExternalState(
                    session->cursorEntryId(), session->currentIndex(), {},
                    ++selectionRevision));
            }
        }
    }

    void masonryVerticalNavigationRetainsOriginalHorizontalAnchor() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        // At the test viewport/density these aspect ratios deterministically
        // wrap as four square tiles, one full-width panorama, then two square
        // tiles. A final panorama only closes/justifies that third row. The
        // first panorama is the important trap: using its center for the
        // second Down would lose the fourth-column anchor selected above it.
        const QList<QSize> sizes{
            QSize(200, 200), QSize(200, 200), QSize(200, 200),
            QSize(200, 200), QSize(1400, 200),
            QSize(200, 200), QSize(200, 200), QSize(1400, 200),
        };
        QVariantList catalog;
        for (int index = 0; index < sizes.size(); ++index) {
            const QString path = directory.filePath(
                QStringLiteral("anchor-%1.png").arg(index));
            QImage image(sizes.at(index), QImage::Format_RGB32);
            image.fill(QColor::fromHsv((index * 47) % 360, 180, 210));
            QVERIFY(image.save(path));
            catalog.append(catalogEntry(index, path));
        }

        QQuickView view;
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        options.maxDecodeThreads = 2;
        auto *runtime = ZoinGallery::GalleryRuntime::install(
            view.engine(), options);
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(
            QStringLiteral("masonry-horizontal-anchor"));
        QVERIFY(session);
        QVERIFY(session->applyExternalCatalog(catalog, 1));
        session->setCurrentIndex(2);

        QObject *panel = createPanel(
            view, session, QStringLiteral("anchorSession"),
            QStringLiteral("masonry"));
        QVERIFY(panel);
        auto *panelItem = qobject_cast<QQuickItem *>(panel);
        auto *layout = panel->findChild<MasonryLayout *>(
            QStringLiteral("galleryMasonryLayout"));
        QVERIFY(panelItem);
        QVERIFY(layout);
        layout->setDensity(150);
        QTRY_COMPARE_WITH_TIMEOUT(layout->count(), sizes.size(), 5000);
        for (int index = 0; index < sizes.size(); ++index) {
            QTRY_COMPARE_WITH_TIMEOUT(layout->indexOriginalSize(index),
                                      sizes.at(index), 10000);
        }

        const auto sameRow = [&](int left, int right) {
            return qAbs(layout->indexGeometry(left).y() -
                        layout->indexGeometry(right).y()) <= 0.51;
        };
        QTRY_VERIFY_WITH_TIMEOUT(sameRow(0, 1) && sameRow(1, 2) &&
                                 sameRow(2, 3), 5000);
        QVERIFY(layout->indexGeometry(4).y() >
                layout->indexGeometry(3).y() + 0.51);
        QVERIFY(layout->indexGeometry(5).y() >
                layout->indexGeometry(4).y() + 0.51);
        QVERIFY(sameRow(5, 6));

        // contentHeight used to be an int.  Keep Masonry's terminal endpoint
        // truncated exactly as before while Details alone opts into qreal row
        // phase and extent.
        const QRectF lastGeometry = layout->indexGeometry(sizes.size() - 1);
        QVERIFY(lastGeometry.isValid() && !lastGeometry.isEmpty());
        QCOMPARE(layout->contentHeight(), qreal(static_cast<int>(
            lastGeometry.y() + lastGeometry.height())));

        panelItem->forceActiveFocus();
        view.requestActivate();
        QVERIFY(panelItem->hasActiveFocus());
        const auto sendKey = [&](Qt::Key key) {
            QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
            QCoreApplication::sendEvent(&view, &press);
            QVERIFY(press.isAccepted());
            QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
            QCoreApplication::sendEvent(&view, &release);
        };

        // Explicit horizontal movement selects the fourth tile and establishes
        // the anchor that both following vertical moves must retain.
        sendKey(Qt::Key_Right);
        QCOMPARE(session->currentIndex(), 3);
        const qreal anchorX = panel->property("currentItemCenterX").toReal();
        QVERIFY(qAbs(anchorX - layout->indexGeometry(3).center().x()) <= 0.51);

        sendKey(Qt::Key_Down);
        QCOMPARE(session->currentIndex(), 4);
        QCOMPARE(panel->property("currentItemCenterX").toReal(), anchorX);

        sendKey(Qt::Key_Down);
        QCOMPARE(session->currentIndex(), 6);
        QCOMPARE(panel->property("currentItemCenterX").toReal(), anchorX);
    }

    void masonryPageNavigationMatchesStandaloneAnchorsAndEdges() {
        QQuickView view;
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        options.maxDecodeThreads = 2;
        auto *runtime = ZoinGallery::GalleryRuntime::install(
            view.engine(), options);
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(
            QStringLiteral("masonry-page-parity"));
        QVERIFY(session);

        // Twenty-nine complete four-item rows plus one final leftmost tile.
        // The missing rightmost tile gives PageDown the same deterministic
        // indexAt() miss/final-index fallback exercised by MasonryMode.
        constexpr int entryCount = 117;
        QVERIFY(session->applyExternalCatalog(plainCatalog(entryCount), 1));
        session->setCurrentIndex(2);

        QObject *panel = createPanel(
            view, session, QStringLiteral("pageParitySession"),
            QStringLiteral("masonry"));
        QVERIFY(panel);
        auto *panelItem = qobject_cast<QQuickItem *>(panel);
        auto *layout = panel->findChild<MasonryLayout *>(
            QStringLiteral("galleryMasonryLayout"));
        QObject *animation = panel->findChild<QObject *>(
            QStringLiteral("galleryPanelScrollAnimation"));
        QObject *chromeAnimation = panel->findChild<QObject *>(
            QStringLiteral("galleryCursorChromeGeometryAnimation"));
        QVERIFY(panelItem);
        QVERIFY(layout);
        QVERIFY(animation);
        QVERIFY(chromeAnimation);
        layout->setDensity(150);
        QTRY_COMPARE_WITH_TIMEOUT(layout->count(), entryCount, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(layout->contentHeight() > layout->height() * 4,
                                 5000);

        panelItem->forceActiveFocus();
        view.requestActivate();
        QVERIFY(panelItem->hasActiveFocus());
        QSignalSpy cursorSpy(panel, SIGNAL(cursorRequested(QString,int,bool)));
        QSignalSpy selectionSpy(panel, SIGNAL(selectionRequested(QString,QVariant)));
        QVERIFY(cursorSpy.isValid());
        QVERIFY(selectionSpy.isValid());

        const auto finalCommitCount = [&]() {
            int count = 0;
            for (const QList<QVariant> &arguments : cursorSpy) {
                if (!arguments.at(2).toBool()) {
                    ++count;
                }
            }
            return count;
        };
        const auto sendPress = [&](Qt::Key key,
                                   Qt::KeyboardModifiers modifiers =
                                       Qt::NoModifier,
                                   bool autoRepeat = false) {
            QKeyEvent event(QEvent::KeyPress, key, modifiers, QString(),
                            autoRepeat, 1);
            QCoreApplication::sendEvent(&view, &event);
            QVERIFY(event.isAccepted());
        };
        const auto sendRelease = [&](Qt::Key key,
                                     Qt::KeyboardModifiers modifiers =
                                         Qt::NoModifier) {
            QKeyEvent event(QEvent::KeyRelease, key, modifiers);
            QCoreApplication::sendEvent(&view, &event);
            QVERIFY(event.isAccepted());
        };
        const auto waitForScrollAndCommit = [&]() {
            QTRY_VERIFY_WITH_TIMEOUT(
                !animation->property("running").toBool(), 1000);
            QTRY_COMPARE_WITH_TIMEOUT(finalCommitCount(), 1, 1000);
        };

        layout->setContentY(0);
        QCoreApplication::processEvents();
        // Horizontal movement adopts both anchors exactly like
        // MasonryMode.setCurrentIndex(..., false, false).
        sendPress(Qt::Key_Right);
        sendRelease(Qt::Key_Right);
        QCOMPARE(session->currentIndex(), 3);
        cursorSpy.clear();
        const qreal anchorX = panel->property("currentItemCenterX").toReal();
        const qreal anchorY = panel->property("currentItemCenterY").toReal();
        QVERIFY(qAbs(anchorX - layout->indexGeometry(3).center().x()) <= 0.51);
        QVERIFY(qAbs(anchorY - layout->indexGeometry(3).center().y()) <= 0.51);

        // An interior page retains both physical anchors and chooses the real
        // Masonry row boundary nearest seven eighths of a page. Native repeats
        // start from animation.to rather than an intermediate rendered frame.
        const qreal deltaY = layout->height() - layout->height() / 8.0;
        const qreal maximum = qMax<qreal>(
            0, layout->contentHeight() - layout->height());
        const QVariantMap firstPlan = layout->masonryPagePlan(
            3, anchorX, anchorY, 0,
            std::numeric_limits<qreal>::quiet_NaN(), 1, deltaY);
        QVERIFY(firstPlan.value(QStringLiteral("valid")).toBool());
        const qreal rowViewportY =
            firstPlan.value(QStringLiteral("rowViewportY")).toReal();
        const qreal expectedScroll =
            firstPlan.value(QStringLiteral("contentY")).toReal();
        const int expectedIndex =
            firstPlan.value(QStringLiteral("targetIndex")).toInt();
        QVERIFY(expectedIndex > 3 && expectedIndex < entryCount - 1);
        sendPress(Qt::Key_PageDown);
        QCOMPARE(session->currentIndex(), expectedIndex);
        QVERIFY(qAbs(panel->property("currentItemCenterX").toReal() -
                     anchorX) <= 0.01);
        QVERIFY(qAbs(panel->property("currentItemCenterY").toReal() -
                     anchorY) <= 0.01);
        QCOMPARE(animation->property("duration").toInt(), 150);
        QVERIFY(animation->property("running").toBool());
        QVERIFY(qAbs(animation->property("to").toReal() - expectedScroll)
                <= 0.51);
        QCOMPARE(finalCommitCount(), 0);
        const QVariantMap repeatedPlan = layout->masonryPagePlan(
            expectedIndex, anchorX, anchorY, expectedScroll,
            rowViewportY, 1, deltaY);
        QVERIFY(repeatedPlan.value(QStringLiteral("valid")).toBool());
        const qreal repeatedScroll =
            repeatedPlan.value(QStringLiteral("contentY")).toReal();
        const int repeatedIndex =
            repeatedPlan.value(QStringLiteral("targetIndex")).toInt();
        QVERIFY(repeatedIndex > expectedIndex);
        QVERIFY(repeatedIndex < entryCount - 1);
        sendPress(Qt::Key_PageDown, Qt::NoModifier, true);
        QCOMPARE(session->currentIndex(), repeatedIndex);
        QVERIFY(qAbs(panel->property("currentItemCenterX").toReal() -
                     anchorX) <= 0.01);
        QVERIFY(qAbs(panel->property("currentItemCenterY").toReal() -
                     anchorY) <= 0.01);
        QVERIFY(qAbs(animation->property("to").toReal() - repeatedScroll)
                <= 0.51);
        QCOMPARE(finalCommitCount(), 0);
        sendRelease(Qt::Key_PageDown);
        QCOMPARE(finalCommitCount(), 0);
        waitForScrollAndCommit();
        QVERIFY(qAbs(layout->contentY() - repeatedScroll) <= 0.51);

        // Shift keeps the same page geometry and paints its toggle locally.
        // Selection and cursor are each committed once on the physical key
        // release, never once per autorepeat.
        cursorSpy.clear();
        selectionSpy.clear();
        const int selectedBeforeShift = session->currentIndex();
        const qreal beforeShiftX =
            panel->property("currentItemCenterX").toReal();
        const qreal beforeShiftY =
            panel->property("currentItemCenterY").toReal();
        sendPress(Qt::Key_PageDown, Qt::ShiftModifier);
        QVERIFY(session->currentIndex() > selectedBeforeShift);
        QCOMPARE(selectionSpy.size(), 0);
        QCOMPARE(finalCommitCount(), 0);
        sendPress(Qt::Key_PageDown, Qt::ShiftModifier, true);
        QCOMPARE(selectionSpy.size(), 0);
        QCOMPARE(finalCommitCount(), 0);
        sendRelease(Qt::Key_PageDown, Qt::ShiftModifier);
        QCOMPARE(selectionSpy.size(), 0);
        sendRelease(Qt::Key_Shift);
        QCOMPARE(selectionSpy.size(), 1);
        QCOMPARE(selectionSpy.at(0).at(0).toString(), QStringLiteral("add"));
        QVERIFY(selectionSpy.at(0).at(1).toList().size() >= 2);
        QCOMPARE(selectionSpy.at(0).at(1).toList().constFirst(),
                 QVariant(QStringLiteral("layout-entry-%1")
                              .arg(selectedBeforeShift)));
        QVERIFY(qAbs(panel->property("currentItemCenterX").toReal() -
                     beforeShiftX) <= 0.01);
        QVERIFY(qAbs(panel->property("currentItemCenterY").toReal() -
                     beforeShiftY) <= 0.01);
        QCOMPARE(finalCommitCount(), 0);
        QTRY_VERIFY_WITH_TIMEOUT(
            !animation->property("running").toBool(), 1000);
        QTRY_COMPARE_WITH_TIMEOUT(finalCommitCount(), 1, 1000);

        // Scrolling alone does not change the cursor anchors. From a viewport
        // near the bottom the first probe still lands in the last complete row
        // at the retained X. Once that row is current, a second PageDown uses
        // standalone's same-item terminal fallback: the right-column probe
        // misses the partial final row, so the last item is selected and its
        // center becomes the new anchor.
        cursorSpy.clear();
        const qreal retainedX =
            panel->property("currentItemCenterX").toReal();
        layout->setContentY(qMax<qreal>(0, maximum - deltaY / 2));
        QCoreApplication::processEvents();
        QCOMPARE(panel->property("currentItemCenterX").toReal(), retainedX);
        sendPress(Qt::Key_PageDown);
        const int lastCompleteRowIndex = session->currentIndex();
        QVERIFY(lastCompleteRowIndex > 0);
        QVERIFY(lastCompleteRowIndex < entryCount - 1);
        QVERIFY(qAbs(panel->property("currentItemCenterX").toReal() -
                     retainedX) <= 0.01);
        sendRelease(Qt::Key_PageDown);
        waitForScrollAndCommit();
        cursorSpy.clear();
        sendPress(Qt::Key_PageDown);
        QCOMPARE(session->currentIndex(), entryCount - 1);
        const int requestsAtTerminal = cursorSpy.size();
        sendPress(Qt::Key_PageDown, Qt::NoModifier, true);
        QCOMPARE(session->currentIndex(), entryCount - 1);
        QCOMPARE(cursorSpy.size(), requestsAtTerminal + 1);
        QVERIFY(cursorSpy.constLast().at(2).toBool());
        const QRectF lastGeometry = layout->indexGeometry(entryCount - 1);
        QVERIFY(qAbs(panel->property("currentItemCenterX").toReal() -
                     lastGeometry.center().x()) <= 0.51);
        const qreal plannedBottom = animation->property("running").toBool()
            ? animation->property("to").toReal() : layout->contentY();
        QVERIFY(qAbs(panel->property("currentItemCenterY").toReal() -
                     (lastGeometry.center().y() - plannedBottom)) <= 0.51);
        sendRelease(Qt::Key_PageDown);
        waitForScrollAndCommit();
        QVERIFY(qAbs(layout->contentY() - maximum) <= 0.51);

        // At the top, PageUp's same-item fallback probes y=1 and deliberately
        // adopts the first item rather than retaining an obsolete page anchor.
        cursorSpy.clear();
        session->setCurrentIndex(0);
        QTRY_COMPARE(layout->currentIndex(), 0);
        layout->setContentY(0);
        QCoreApplication::processEvents();
        for (int index = 0; index < 4; ++index) {
            sendPress(Qt::Key_Right);
            sendRelease(Qt::Key_Right);
        }
        QCOMPARE(session->currentIndex(), 4);
        cursorSpy.clear();
        sendPress(Qt::Key_PageUp);
        QCOMPARE(session->currentIndex(), 0);
        const QRectF firstGeometry = layout->indexGeometry(0);
        QVERIFY(qAbs(panel->property("currentItemCenterX").toReal() -
                     firstGeometry.center().x()) <= 0.51);
        QVERIFY(qAbs(panel->property("currentItemCenterY").toReal() -
                     firstGeometry.center().y()) <= 0.51);
        sendRelease(Qt::Key_PageUp);
        QTRY_COMPARE_WITH_TIMEOUT(finalCommitCount(), 1, 1000);

        runtime->shutdown();
    }

    void masonryShortCatalogPagesBetweenTerminalItemsWithoutScrollHistory() {
        QQuickView view;
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        options.maxDecodeThreads = 2;
        auto *runtime = ZoinGallery::GalleryRuntime::install(
            view.engine(), options);
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(
            QStringLiteral("masonry-short-terminal-pages"));
        QVERIFY(session);
        QVERIFY(session->applyExternalCatalog(plainCatalog(4), 1));
        session->setCurrentIndex(1);

        QObject *panel = createPanel(
            view, session, QStringLiteral("shortPageSession"),
            QStringLiteral("masonry"));
        QVERIFY(panel);
        auto *panelItem = qobject_cast<QQuickItem *>(panel);
        auto *layout = panel->findChild<MasonryLayout *>(
            QStringLiteral("galleryMasonryLayout"));
        QVERIFY(panelItem);
        QVERIFY(layout);
        layout->setDensity(150);
        QTRY_COMPARE_WITH_TIMEOUT(layout->count(), 4, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(layout->contentHeight() > 0, 5000);
        QVERIFY2(layout->contentHeight() <= layout->height(),
                 qPrintable(QStringLiteral("short catalog unexpectedly scrolls: %1 > %2")
                                .arg(layout->contentHeight())
                                .arg(layout->height())));
        QCOMPARE(layout->contentY(), 0.0);

        panelItem->forceActiveFocus();
        view.requestActivate();
        QVERIFY(panelItem->hasActiveFocus());
        const auto clickPage = [&](Qt::Key key) {
            QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
            QCoreApplication::sendEvent(&view, &press);
            QVERIFY(press.isAccepted());
            QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
            QCoreApplication::sendEvent(&view, &release);
            QVERIFY(release.isAccepted());
        };

        // With no scrollable viewport, Page keys retain the original terminal
        // semantics. The cursor-only PageUp must not create a reversible page
        // history node that turns the following PageDown into 0 -> 1.
        clickPage(Qt::Key_PageUp);
        QCOMPARE(session->currentIndex(), 0);
        QCOMPARE(layout->contentY(), 0.0);
        clickPage(Qt::Key_PageDown);
        QCOMPARE(session->currentIndex(), 3);
        QCOMPARE(layout->contentY(), 0.0);
        clickPage(Qt::Key_PageUp);
        QCOMPARE(session->currentIndex(), 0);
        QCOMPARE(layout->contentY(), 0.0);

        runtime->shutdown();
    }

    void masonryPageNavigationAlignsVariableRowBands() {
        QQuickView view;
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        options.maxDecodeThreads = 2;
        auto *runtime = ZoinGallery::GalleryRuntime::install(
            view.engine(), options);
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(
            QStringLiteral("masonry-variable-page-bands"));
        QVERIFY(session);

        constexpr int entryCount = 180;
        QVERIFY(session->applyExternalCatalog(plainCatalog(entryCount), 1));
        const QList<QSize> aspectCycle{
            QSize(1800, 180), QSize(220, 900), QSize(640, 640),
            QSize(1200, 320), QSize(260, 1000), QSize(900, 500),
            QSize(1600, 260), QSize(480, 900), QSize(700, 700),
            QSize(1100, 240), QSize(300, 1200), QSize(800, 460),
        };
        for (int index = 0; index < entryCount; ++index) {
            auto *image = session->model()
                ->data(session->model()->index(index, 0),
                       FileListModel::ImageFileRole)
                .value<ImageFile *>();
            QVERIFY(image);
            image->setFullSize(aspectCycle.at(index % aspectCycle.size()));
        }
        session->setCurrentIndex(0);

        QObject *panel = createPanel(
            view, session, QStringLiteral("variableBandSession"),
            QStringLiteral("masonry"));
        QVERIFY(panel);
        auto *panelItem = qobject_cast<QQuickItem *>(panel);
        auto *layout = panel->findChild<MasonryLayout *>(
            QStringLiteral("galleryMasonryLayout"));
        QObject *animation = panel->findChild<QObject *>(
            QStringLiteral("galleryPanelScrollAnimation"));
        QObject *chromeAnimation = panel->findChild<QObject *>(
            QStringLiteral("galleryCursorChromeGeometryAnimation"));
        QVERIFY(panelItem && layout && animation && chromeAnimation);
        panelItem->setWidth(711.5);
        panelItem->setHeight(433.25);
        layout->setDensity(142);
        layout->setSpacing(9);
        layout->setPaddingTop(3.25);
        layout->setPaddingBottom(2.5);
        QTRY_COMPARE_WITH_TIMEOUT(layout->count(), entryCount, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(layout->layoutBands().size() > 25, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(
            layout->contentHeight() > layout->height() * 6, 5000);
        // Width changes schedule one bounded thumbnail-tier refresh. Let its
        // deliberate layoutReset complete before starting a paging sequence.
        QTest::qWait(400);
        QCoreApplication::processEvents();

        panelItem->forceActiveFocus();
        view.requestActivate();
        QVERIFY(panelItem->hasActiveFocus());

        const auto sendPress = [&](Qt::Key key, bool autoRepeat = false) {
            QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier,
                            QString(), autoRepeat, 1);
            QCoreApplication::sendEvent(&view, &event);
            QVERIFY(event.isAccepted());
        };
        const auto sendRelease = [&](Qt::Key key,
                                     bool autoRepeat = false) {
            QKeyEvent event(QEvent::KeyRelease, key, Qt::NoModifier,
                            QString(), autoRepeat, 1);
            QCoreApplication::sendEvent(&view, &event);
            QVERIFY(event.isAccepted());
        };
        const auto waitSettled = [&]() {
            QTRY_VERIFY_WITH_TIMEOUT(
                !animation->property("running").toBool() &&
                    !chromeAnimation->property("running").toBool(), 1500);
            QTRY_VERIFY_WITH_TIMEOUT(
                !panel->property("cursorChromeTransitionActive").toBool(),
                1500);
        };
        const auto setFixture = [&](qreal contentY, int index) {
            session->setCurrentIndex(index);
            QCoreApplication::processEvents();
            QVERIFY(QMetaObject::invokeMethod(
                panel, "setPanelContentY", Qt::DirectConnection,
                Q_ARG(QVariant, QVariant(contentY)),
                Q_ARG(QVariant, QVariant(false))));
            panel->setProperty("pendingVisualCursorIndex", -1);
            panel->setProperty("visualCursorIndex", index);
            QVERIFY(QMetaObject::invokeMethod(
                panel, "resetCurrentItemCenter", Qt::DirectConnection,
                Q_ARG(QVariant, QVariant(index))));
            QCoreApplication::processEvents();
        };
        const auto bandMap = [layout](int bandIndex) {
            return layout->layoutBands().at(bandIndex).toMap();
        };
        const auto firstIndexInBand = [&](int bandIndex) {
            const QVariantList indexes =
                bandMap(bandIndex).value(QStringLiteral("indexes")).toList();
            return indexes.at(indexes.size() / 2).toInt();
        };

        // Align the first actual row top with viewport y=0. The selected
        // destination is the strict later row whose boundary displacement is
        // nearest the established 7/8-page pace, and that row top must also
        // land at exactly viewport y=0.
        const QVariantMap firstBand = bandMap(0);
        const qreal startY = firstBand.value(QStringLiteral("top")).toReal();
        const int startIndex = firstIndexInBand(0);
        setFixture(startY, startIndex);
        const qreal anchorX = panel->property("currentItemCenterX").toReal();
        const qreal anchorY = panel->property("currentItemCenterY").toReal();
        const qreal nominalDistance = layout->height() * 7.0 / 8.0;
        const QVariantMap firstPlan = layout->masonryPagePlan(
            startIndex, anchorX, anchorY, startY,
            std::numeric_limits<qreal>::quiet_NaN(), 1,
            nominalDistance);
        QVERIFY(firstPlan.value(QStringLiteral("valid")).toBool());
        QCOMPARE(firstPlan.value(QStringLiteral("rowViewportY")).toReal(),
                 0.0);
        const int targetBand =
            firstPlan.value(QStringLiteral("targetBandIndex")).toInt();
        QVERIFY(targetBand > 0);
        const qreal targetBandTop =
            firstPlan.value(QStringLiteral("targetBandTop")).toReal();
        const qreal plannedDown =
            firstPlan.value(QStringLiteral("contentY")).toReal();
        QVERIFY(qAbs(targetBandTop - plannedDown) < 1e-6);
        const qreal chosenError =
            qAbs((targetBandTop - startY) - nominalDistance);
        for (int band = 1; band < layout->layoutBands().size(); ++band) {
            const qreal candidateTop =
                bandMap(band).value(QStringLiteral("top")).toReal();
            QVERIFY(chosenError <=
                    qAbs((candidateTop - startY) - nominalDistance) + 1e-6);
        }

        sendPress(Qt::Key_PageDown);
        QCOMPARE(session->currentIndex(),
                 firstPlan.value(QStringLiteral("targetIndex")).toInt());
        QVERIFY(qAbs(animation->property("to").toReal() - plannedDown) < 1e-6);
        QVERIFY(qAbs(panel->property("masonryPageRowViewportY").toReal())
                < 1e-6);
        sendRelease(Qt::Key_PageDown);
        waitSettled();

        // Reciprocal page nodes, rather than a fresh nearest-row search, make
        // variable-height rows exactly reversible.
        sendPress(Qt::Key_PageUp);
        QCOMPARE(session->currentIndex(), startIndex);
        QVERIFY(qAbs(animation->property("to").toReal() - startY) < 1e-6);
        sendRelease(Qt::Key_PageUp);
        waitSettled();
        QVERIFY(qAbs(layout->contentY() - startY) < 1e-6);
        QVERIFY(qAbs(panel->property("currentItemCenterX").toReal() - anchorX)
                < 1e-6);
        QVERIFY(qAbs(panel->property("currentItemCenterY").toReal() - anchorY)
                < 1e-6);

        // Held repeats always start from the prior animation destination.
        // Reversing the same number of repeats restores every exact node and
        // cannot accumulate variable-row phase drift.
        setFixture(startY, startIndex);
        QList<qreal> downDestinations;
        for (int page = 0; page < 3; ++page) {
            sendPress(Qt::Key_PageDown, page > 0);
            const qreal destination =
                animation->property("to").toReal();
            QVERIFY(downDestinations.isEmpty() ||
                    destination > downDestinations.constLast());
            downDestinations.append(destination);
        }
        sendRelease(Qt::Key_PageDown);
        waitSettled();
        for (int page = 0; page < 3; ++page) {
            sendPress(Qt::Key_PageUp, page > 0);
        }
        sendRelease(Qt::Key_PageUp);
        waitSettled();
        QCOMPARE(session->currentIndex(), startIndex);
        QVERIFY(qAbs(layout->contentY() - startY) < 1e-6);

        // Reflow invalidates exact band coordinates even when contentHeight is
        // coincidentally unchanged. A live Page animation is stopped against
        // the new revision and the next sequence re-anchors to new row tops.
        setFixture(startY, startIndex);
        const quint64 revisionBeforeResize = layout->layoutRevision();
        sendPress(Qt::Key_PageDown);
        QVERIFY(animation->property("running").toBool());
        panelItem->setWidth(panelItem->width() + 83.75);
        QTRY_VERIFY_WITH_TIMEOUT(
            layout->layoutRevision() > revisionBeforeResize, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!animation->property("running").toBool(),
                                 1000);
        QCOMPARE(panel->property("masonryPageOrdinal").toInt(), 0);
        QVERIFY(!std::isfinite(
            panel->property("masonryPageRowViewportY").toReal()));
        waitSettled();
        QTest::qWait(400);
        QCoreApplication::processEvents();

        const QVariantMap resizedFirstBand = bandMap(0);
        const qreal resizedStartY =
            resizedFirstBand.value(QStringLiteral("top")).toReal();
        const int resizedStartIndex = firstIndexInBand(0);
        setFixture(resizedStartY, resizedStartIndex);
        sendPress(Qt::Key_PageDown);
        const qreal resizedDownY = animation->property("to").toReal();
        QVERIFY(resizedDownY > resizedStartY);
        sendRelease(Qt::Key_PageDown);
        waitSettled();
        sendPress(Qt::Key_PageUp);
        QCOMPARE(session->currentIndex(), resizedStartIndex);
        QVERIFY(qAbs(animation->property("to").toReal() - resizedStartY)
                < 1e-6);
        sendRelease(Qt::Key_PageUp);
        waitSettled();

        // Masonry's intentionally truncated contentHeight generally creates
        // an off-band maximum. The terminal node is explicit and reciprocal:
        // PageUp restores the exact pre-clamp viewport and cursor.
        const qreal maximum = qMax<qreal>(
            0, layout->contentHeight() - layout->height());
        const qreal beforeTerminalY = qMax<qreal>(0, maximum - 1.0);
        int beforeTerminalIndex = layout->indexAt(
            layout->width() / 2, beforeTerminalY + layout->height() / 2);
        if (beforeTerminalIndex < 0) {
            beforeTerminalIndex = layout->visibleIndexes().isEmpty()
                ? 0 : layout->visibleIndexes().constLast().toInt();
        }
        setFixture(beforeTerminalY, beforeTerminalIndex);
        const qreal terminalAnchorX =
            panel->property("currentItemCenterX").toReal();
        const qreal terminalAnchorY =
            panel->property("currentItemCenterY").toReal();
        sendPress(Qt::Key_PageDown);
        QVERIFY(qAbs(animation->property("to").toReal() - maximum) < 1e-6);
        sendRelease(Qt::Key_PageDown);
        waitSettled();
        sendPress(Qt::Key_PageUp);
        QCOMPARE(session->currentIndex(), beforeTerminalIndex);
        QVERIFY(qAbs(animation->property("to").toReal()
                     - beforeTerminalY) < 1e-6);
        sendRelease(Qt::Key_PageUp);
        waitSettled();
        QVERIFY(qAbs(panel->property("currentItemCenterX").toReal()
                     - terminalAnchorX) < 1e-6);
        QVERIFY(qAbs(panel->property("currentItemCenterY").toReal()
                     - terminalAnchorY) < 1e-6);

        runtime->shutdown();
    }

    void navigationCommitsAfterBoundaryScroll() {
        QQuickView view;
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        auto *runtime = ZoinGallery::GalleryRuntime::install(
            view.engine(), options);
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(
            QStringLiteral("layout-repeat-scroll"));
        QVERIFY(session);
        constexpr int entryCount = 240;
        QVERIFY(session->applyExternalCatalog(plainCatalog(entryCount), 1));
        session->setCurrentIndex(0);

        QObject *panel = createPanel(
            view, session, QStringLiteral("repeatScrollSession"),
            QStringLiteral("details"));
        QVERIFY(panel);
        auto *panelItem = qobject_cast<QQuickItem *>(panel);
        auto *layout = panel->findChild<MasonryLayout *>(
            QStringLiteral("galleryMasonryLayout"));
        QObject *animation = panel->findChild<QObject *>(
            QStringLiteral("galleryPanelScrollAnimation"));
        QObject *chromeAnimation = panel->findChild<QObject *>(
            QStringLiteral("galleryCursorChromeGeometryAnimation"));
        QVERIFY(panelItem);
        QVERIFY(layout);
        QVERIFY(animation);
        QVERIFY(chromeAnimation);
        QSignalSpy cursorSpy(panel, SIGNAL(cursorRequested(QString,int,bool)));
        QVERIFY(cursorSpy.isValid());
        QTRY_COMPARE_WITH_TIMEOUT(layout->count(), entryCount, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(layout->height() > 100, 5000);
        panelItem->forceActiveFocus();
        view.requestActivate();
        QVERIFY(panelItem->hasActiveFocus());

        const auto stopAnimation = [&]() {
            QVERIFY(QMetaObject::invokeMethod(
                panel, "cancelCursorChromeTransition", Qt::DirectConnection));
            QVERIFY(QMetaObject::invokeMethod(
                animation, "stop", Qt::DirectConnection));
            QCoreApplication::processEvents();
        };
        const auto sendPress = [&](Qt::Key key, bool autoRepeat) {
            QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier,
                            QString(), autoRepeat, 1);
            QCoreApplication::sendEvent(&view, &event);
            QVERIFY(event.isAccepted());
        };
        const auto sendRelease = [&](Qt::Key key, bool autoRepeat = false) {
            QKeyEvent event(QEvent::KeyRelease, key, Qt::NoModifier,
                            QString(), autoRepeat, 1);
            QCoreApplication::sendEvent(&view, &event);
        };
        const auto finalCommitCount = [&]() {
            int count = 0;
            for (const QList<QVariant> &arguments : cursorSpy) {
                if (!arguments.at(2).toBool()) {
                    ++count;
                }
            }
            return count;
        };
        const auto boundaryIndex = [&](MasonryLayout::NavigationDirection direction) {
            const qreal top = layout->contentY();
            const qreal bottom = top + layout->height();
            const QVariantList visible = layout->visibleIndexes();
            for (const QVariant &value : visible) {
                const int index = value.toInt();
                const int target = layout->neighborIndex(index, direction);
                if (target == index) {
                    continue;
                }
                const QRectF current = layout->indexGeometry(index);
                const QRectF next = layout->indexGeometry(target);
                if (direction == MasonryLayout::NavigateDown &&
                    current.bottom() <= bottom + 0.51 &&
                    next.bottom() > bottom + 0.51) {
                    return index;
                }
                if (direction == MasonryLayout::NavigateUp &&
                    current.top() >= top - 0.51 &&
                    next.top() < top - 0.51) {
                    return index;
                }
            }
            return -1;
        };

        const auto verifyMode = [&](const QString &mode,
                                    MasonryLayout::PresentationMode nativeMode,
                                    qreal density) {
            panel->setProperty("presentationMode", mode);
            QTRY_COMPARE_WITH_TIMEOUT(layout->presentationMode(), nativeMode,
                                      3000);
            layout->setDensity(density);

            // Moving wholly inside the viewport has no scroll to protect and
            // retains the established synchronous release commit contract.
            stopAnimation();
            cursorSpy.clear();
            layout->setContentY(0);
            QCoreApplication::processEvents();
            int noScrollStart = -1;
            for (const QVariant &value : layout->visibleIndexes()) {
                const int candidate = value.toInt();
                const int next = layout->neighborIndex(
                    candidate, MasonryLayout::NavigateDown);
                if (next == candidate) {
                    continue;
                }
                const QRectF nextGeometry = layout->indexGeometry(next);
                if (nextGeometry.top() >= layout->contentY() - 0.51 &&
                    nextGeometry.bottom() <=
                        layout->contentY() + layout->height() + 0.51) {
                    noScrollStart = candidate;
                    break;
                }
            }
            QVERIFY2(noScrollStart >= 0,
                     qPrintable(mode + QStringLiteral(
                         " has no wholly visible downward neighbor")));
            session->setCurrentIndex(noScrollStart);
            QVERIFY(invokeEnsureCurrentVisible(panel, false));
            sendPress(Qt::Key_Down, false);
            QVERIFY(!animation->property("running").toBool());
            QCOMPARE(finalCommitCount(), 0);
            sendRelease(Qt::Key_Down);
            QCOMPARE(finalCommitCount(), 1);

            if (nativeMode == MasonryLayout::Details) {
                // Compact row modes deliberately do not animate their
                // viewport.  Boundary and page moves must land synchronously
                // and release can commit immediately because there is no
                // in-flight scroll to protect from a host redraw.
                stopAnimation();
                cursorSpy.clear();
                layout->setContentY(0);
                QCoreApplication::processEvents();
                const int instantStart = boundaryIndex(
                    MasonryLayout::NavigateDown);
                QVERIFY(instantStart >= 0);
                session->setCurrentIndex(instantStart);
                QVERIFY(invokeEnsureCurrentVisible(panel, false));
                sendPress(Qt::Key_Down, false);
                QVERIFY(!animation->property("running").toBool());
                QVERIFY(!chromeAnimation->property("running").toBool());
                QVERIFY(!panel->property(
                             "cursorChromeTransitionActive").toBool());
                QCOMPARE(panel->property("visualCursorIndex").toInt(),
                         session->currentIndex());
                QVERIFY(indexHasPaintedAreaInViewport(
                    layout, session->currentIndex()));
                sendRelease(Qt::Key_Down);
                QTRY_COMPARE_WITH_TIMEOUT(finalCommitCount(), 1, 1000);

                stopAnimation();
                cursorSpy.clear();
                layout->setContentY(0);
                session->setCurrentIndex(0);
                QVERIFY(invokeEnsureCurrentVisible(panel, false));
                sendPress(Qt::Key_PageDown, false);
                QVERIFY(session->currentIndex() > 0);
                QVERIFY(!animation->property("running").toBool());
                QVERIFY(!chromeAnimation->property("running").toBool());
                QVERIFY(!panel->property(
                             "cursorChromeTransitionActive").toBool());
                QCOMPARE(panel->property("visualCursorIndex").toInt(),
                         session->currentIndex());
                QVERIFY(layout->contentY() > 0);
                QVERIFY(indexHasPaintedAreaInViewport(
                    layout, session->currentIndex()));
                sendRelease(Qt::Key_PageDown);
                QTRY_COMPARE_WITH_TIMEOUT(finalCommitCount(), 1, 1000);
                stopAnimation();
                return;
            }

            // A physical initial press and its repeats keep the established
            // 150 ms reveal. Native macOS repeat releases must not commit an
            // expensive semantic scene while that animation is in flight;
            // the one final non-repeat release commits after scrolling stops.
            stopAnimation();
            cursorSpy.clear();
            layout->setContentY(0);
            QCoreApplication::processEvents();
            const int downStart = boundaryIndex(
                MasonryLayout::NavigateDown);
            QVERIFY2(downStart >= 0,
                     qPrintable(mode + QStringLiteral(
                         " has no downward viewport boundary")));
            session->setCurrentIndex(downStart);
            QVERIFY(invokeEnsureCurrentVisible(panel, false));
            sendPress(Qt::Key_Down, false);
            const int firstDown = layout->neighborIndex(
                downStart, MasonryLayout::NavigateDown);
            QCOMPARE(session->currentIndex(), firstDown);
            QCOMPARE(animation->property("duration").toInt(), 150);
            QVERIFY(animation->property("running").toBool());
            QVERIFY(panel->property("cursorChromeTransitionActive").toBool());
            QVERIFY(chromeAnimation->property("running").toBool());
            sendRelease(Qt::Key_Down, true);
            QCOMPARE(finalCommitCount(), 0);
            QVERIFY(panel->property("navigationKeyHeld").toBool());
            QTest::qWait(24);
            const QRectF liveChromeBeforeRepeat = panel->property(
                "cursorChromeRect").toRectF();
            sendPress(Qt::Key_Down, true);
            QCOMPARE(session->currentIndex(), layout->neighborIndex(
                firstDown, MasonryLayout::NavigateDown));
            QCOMPARE(animation->property("duration").toInt(), 150);
            // A repeat retarget starts at the currently painted rectangle; it
            // must never snap back to the original key-down source.
            const QRectF liveChromeAfterRepeat = panel->property(
                "cursorChromeRect").toRectF();
            QVERIFY(qAbs(liveChromeAfterRepeat.x()
                         - liveChromeBeforeRepeat.x()) < 0.05);
            QVERIFY(qAbs(liveChromeAfterRepeat.y()
                         - liveChromeBeforeRepeat.y()) < 0.05);
            QVERIFY(qAbs(liveChromeAfterRepeat.width()
                         - liveChromeBeforeRepeat.width()) < 0.05);
            QVERIFY(qAbs(liveChromeAfterRepeat.height()
                         - liveChromeBeforeRepeat.height()) < 0.05);
            sendRelease(Qt::Key_Down, true);
            QCOMPARE(finalCommitCount(), 0);
            sendRelease(Qt::Key_Down);
            QCOMPARE(finalCommitCount(), 0);
            QVERIFY(panel->property("cursorCommitAfterScroll").toBool());
            QTRY_VERIFY_WITH_TIMEOUT(
                !animation->property("running").toBool()
                    && !chromeAnimation->property("running").toBool(), 1000);
            QTRY_VERIFY_WITH_TIMEOUT(
                !panel->property("cursorChromeTransitionActive").toBool(), 1000);
            QTRY_COMPARE_WITH_TIMEOUT(finalCommitCount(), 1, 1000);

            // Exercise the symmetric top-edge path from a partially scrolled
            // viewport; it used to accumulate the same 150 ms backlog.
            stopAnimation();
            cursorSpy.clear();
            const qreal maximum = qMax<qreal>(
                0, layout->contentHeight() - layout->height());
            layout->setContentY(qMin(maximum, density * 5.25));
            QCoreApplication::processEvents();
            const int upStart = boundaryIndex(MasonryLayout::NavigateUp);
            QVERIFY2(upStart >= 0,
                     qPrintable(mode + QStringLiteral(
                         " has no upward viewport boundary")));
            session->setCurrentIndex(upStart);
            QVERIFY(invokeEnsureCurrentVisible(panel, false));
            sendPress(Qt::Key_Up, false);
            const int firstUp = layout->neighborIndex(
                upStart, MasonryLayout::NavigateUp);
            QCOMPARE(session->currentIndex(), firstUp);
            QCOMPARE(animation->property("duration").toInt(), 150);
            QVERIFY(animation->property("running").toBool());
            QVERIFY(panel->property("cursorChromeTransitionActive").toBool());
            QVERIFY(chromeAnimation->property("running").toBool());
            sendRelease(Qt::Key_Up, true);
            QCOMPARE(finalCommitCount(), 0);
            sendPress(Qt::Key_Up, true);
            QCOMPARE(session->currentIndex(), layout->neighborIndex(
                firstUp, MasonryLayout::NavigateUp));
            QCOMPARE(animation->property("duration").toInt(), 150);
            sendRelease(Qt::Key_Up, true);
            QCOMPARE(finalCommitCount(), 0);
            sendRelease(Qt::Key_Up);
            QCOMPARE(finalCommitCount(), 0);
            QTRY_VERIFY_WITH_TIMEOUT(
                !animation->property("running").toBool()
                    && !chromeAnimation->property("running").toBool(), 1000);
            QTRY_VERIFY_WITH_TIMEOUT(
                !panel->property("cursorChromeTransitionActive").toBool(), 1000);
            QTRY_COMPARE_WITH_TIMEOUT(finalCommitCount(), 1, 1000);

            if (nativeMode != MasonryLayout::Masonry) {
                // Page navigation uses the same post-animation commit
                // lifecycle and, like original MasonryMode, retains the
                // viewport-relative cursor anchor while moving by 7/8 page.
                stopAnimation();
                cursorSpy.clear();
                layout->setContentY(0);
                session->setCurrentIndex(0);
                QVERIFY(invokeEnsureCurrentVisible(panel, false));
                const QRectF pageStartGeometry = layout->indexGeometry(0);
                panel->setProperty("currentItemCenterX",
                                   pageStartGeometry.center().x());
                panel->setProperty("currentItemCenterY",
                                   pageStartGeometry.center().y());
                const qreal pageAnchorX =
                    panel->property("currentItemCenterX").toReal();
                const qreal pageAnchorY =
                    panel->property("currentItemCenterY").toReal();
                sendPress(Qt::Key_PageDown, false);
                QVERIFY(session->currentIndex() > 0);
                const QRectF pageGeometry =
                    layout->indexGeometry(session->currentIndex());
                QVERIFY(qAbs(panel->property("currentItemCenterX").toReal() -
                             pageAnchorX) <= 0.51);
                QCOMPARE(animation->property("duration").toInt(), 150);
                QVERIFY(animation->property("running").toBool());
                QVERIFY(qAbs(panel->property("currentItemCenterY").toReal() -
                             pageAnchorY) <= 0.51);
                QVERIFY(qAbs(pageGeometry.center().y() -
                             animation->property("to").toReal() -
                             pageAnchorY) <= qMax<qreal>(0.51, density));
                sendRelease(Qt::Key_PageDown);
                QCOMPARE(finalCommitCount(), 0);
                QTRY_VERIFY_WITH_TIMEOUT(
                    !animation->property("running").toBool(), 1000);
                QTRY_COMPARE_WITH_TIMEOUT(finalCommitCount(), 1, 1000);
            }
        };

        verifyMode(QStringLiteral("details"), MasonryLayout::Details, 30);
        verifyMode(QStringLiteral("grid"), MasonryLayout::Grid, 120);
        verifyMode(QStringLiteral("icons"), MasonryLayout::Icons, 120);
        verifyMode(QStringLiteral("masonry"), MasonryLayout::Masonry, 120);
    }

    void gridPageNavigationPreservesFractionalRowPhase() {
        QQuickView view;
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        auto *runtime = ZoinGallery::GalleryRuntime::install(
            view.engine(), options);
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(
            QStringLiteral("grid-page-quantization"));
        QVERIFY(session);
        constexpr int entryCount = 403;
        QVERIFY(session->applyExternalCatalog(plainCatalog(entryCount), 1));

        QObject *panel = createPanel(
            view, session, QStringLiteral("gridPageSession"),
            QStringLiteral("grid"));
        QVERIFY(panel);
        auto *panelItem = qobject_cast<QQuickItem *>(panel);
        auto *layout = panel->findChild<MasonryLayout *>(
            QStringLiteral("galleryMasonryLayout"));
        QObject *scrollAnimation = panel->findChild<QObject *>(
            QStringLiteral("galleryPanelScrollAnimation"));
        QObject *chromeAnimation = panel->findChild<QObject *>(
            QStringLiteral("galleryCursorChromeGeometryAnimation"));
        QVERIFY(panelItem && layout && scrollAnimation && chromeAnimation);

        constexpr qreal density = 101.25;
        constexpr qreal paddingTop = 2.75;
        constexpr qreal paddingBottom = 1.5;
        panelItem->setWidth(713.5);
        panelItem->setHeight(447.75);
        layout->setDensity(density);
        layout->setSpacing(7);
        layout->setPaddingTop(paddingTop);
        layout->setPaddingBottom(paddingBottom);
        QTRY_COMPARE_WITH_TIMEOUT(layout->presentationMode(),
                                  MasonryLayout::Grid, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(layout->count(), entryCount, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(layout->height() > density * 3, 3000);
        panelItem->forceActiveFocus();
        view.requestActivate();
        QVERIFY(panelItem->hasActiveFocus());

        const auto columns = [&]() {
            return qMax(1, int(std::floor(
                (layout->width() - layout->paddingLeft()
                 - layout->paddingRight()) / density)));
        };
        const auto rowsPerPage = [&]() {
            const qreal usable = qMax<qreal>(
                1, layout->height() - layout->paddingTop()
                   - layout->paddingBottom());
            return qMax(1, static_cast<int>(std::floor(
                (usable * 7.0 / 8.0) / density)));
        };
        const auto maximumY = [&]() {
            return qMax<qreal>(0, layout->contentHeight() - layout->height());
        };
        const auto phase = [](qreal value, qreal origin, qreal stride) {
            qreal result = std::fmod(value - origin, stride);
            if (result < 0) {
                result += stride;
            }
            return result;
        };
        const auto setContentY = [&](qreal value) {
            QVERIFY(QMetaObject::invokeMethod(
                panel, "setPanelContentY", Qt::DirectConnection,
                Q_ARG(QVariant, QVariant(value)),
                Q_ARG(QVariant, QVariant(false))));
            QCoreApplication::processEvents();
        };
        const auto setFixture = [&](int row, int column,
                                    qreal contentY) -> int {
            const int index = qBound(
                0, row * columns() + column, layout->count() - 1);
            session->setCurrentIndex(index);
            QCoreApplication::processEvents();
            setContentY(contentY);
            panel->setProperty("pendingVisualCursorIndex", -1);
            panel->setProperty("visualCursorIndex", index);
            if (!QMetaObject::invokeMethod(
                    panel, "resetCurrentItemCenter", Qt::DirectConnection,
                    Q_ARG(QVariant, QVariant(index)))) {
                return -1;
            }
            QCoreApplication::processEvents();
            return index;
        };
        const auto sendPress = [&](Qt::Key key, bool autoRepeat = false) {
            QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier,
                            QString(), autoRepeat, 1);
            QCoreApplication::sendEvent(&view, &event);
            QVERIFY(event.isAccepted());
        };
        const auto sendRelease = [&](Qt::Key key, bool autoRepeat = false) {
            QKeyEvent event(QEvent::KeyRelease, key, Qt::NoModifier,
                            QString(), autoRepeat, 1);
            QCoreApplication::sendEvent(&view, &event);
            QVERIFY(event.isAccepted());
        };
        const auto waitSettled = [&]() {
            QTRY_VERIFY_WITH_TIMEOUT(
                !scrollAnimation->property("running").toBool()
                    && !chromeAnimation->property("running").toBool(), 1200);
            QTRY_VERIFY_WITH_TIMEOUT(
                !panel->property("cursorChromeTransitionActive").toBool(), 1200);
        };

        // The common top-aligned case is exact as well: when row zero starts
        // at viewport y=0, an interior PageDown lands the next lattice row at
        // the same viewport y and PageUp returns without a sub-pixel phase
        // change. Non-zero padding makes this a stronger check than y=0 alone.
        const qreal alignedY = paddingTop;
        const int alignedColumn = 1;
        const int alignedIndex = setFixture(0, alignedColumn, alignedY);
        QVERIFY(alignedIndex >= 0);
        const int alignedPageRows = rowsPerPage();
        const qreal alignedPageDistance = alignedPageRows * density;
        sendPress(Qt::Key_PageDown);
        const int alignedDownIndex =
            alignedPageRows * columns() + alignedColumn;
        QCOMPARE(session->currentIndex(), alignedDownIndex);
        QVERIFY(qAbs(scrollAnimation->property("to").toReal()
                     - (alignedY + alignedPageDistance)) < 1e-6);
        const QRectF alignedDownGeometry =
            layout->indexGeometry(alignedDownIndex);
        QVERIFY(qAbs(alignedDownGeometry.y()
                     - scrollAnimation->property("to").toReal()) < 1e-6);
        sendRelease(Qt::Key_PageDown);
        waitSettled();
        sendPress(Qt::Key_PageUp);
        QCOMPARE(session->currentIndex(), alignedIndex);
        QVERIFY(qAbs(scrollAnimation->property("to").toReal()
                     - alignedY) < 1e-6);
        sendRelease(Qt::Key_PageUp);
        waitSettled();

        // Start at a deliberately fractional point in the native qreal row
        // lattice. Every interior page must preserve this exact phase and keep
        // the cursor rectangle at its original viewport Y.
        const qreal initialY = paddingTop + density * 0.375;
        const int initialColumn = 1;
        const int initialRow = 2;
        const int initialIndex = setFixture(
            initialRow, initialColumn, initialY);
        QVERIFY(initialIndex >= 0);
        const int pageRows = rowsPerPage();
        const qreal pageDistance = pageRows * density;
        const QRectF initialGeometry = layout->indexGeometry(initialIndex);
        const qreal initialCursorViewportY = initialGeometry.y() - initialY;

        sendPress(Qt::Key_PageDown);
        QCOMPARE(session->currentIndex(),
                 (initialRow + pageRows) * columns() + initialColumn);
        QVERIFY(qAbs(scrollAnimation->property("to").toReal()
                     - (initialY + pageDistance)) < 1e-6);
        QVERIFY(qAbs(phase(scrollAnimation->property("to").toReal(),
                           paddingTop, density)
                     - phase(initialY, paddingTop, density)) < 1e-6);
        const QRectF downChromeTarget = panel->property(
            "cursorChromeTargetRect").toRectF();
        QVERIFY(qAbs(downChromeTarget.y()
                     - (initialCursorViewportY + 2.0)) < 1e-6);
        sendRelease(Qt::Key_PageDown);
        waitSettled();

        sendPress(Qt::Key_PageUp);
        QCOMPARE(session->currentIndex(), initialIndex);
        QVERIFY(qAbs(scrollAnimation->property("to").toReal()
                     - initialY) < 1e-6);
        sendRelease(Qt::Key_PageUp);
        waitSettled();
        QVERIFY(qAbs(layout->contentY() - initialY) < 1e-6);

        // Native repeat events must accumulate from the previous planned
        // destination, never from an in-between rendered animation frame.
        QVERIFY(setFixture(initialRow, initialColumn, initialY) >= 0);
        sendPress(Qt::Key_PageDown);
        const qreal firstPlannedY = scrollAnimation->property("to").toReal();
        sendRelease(Qt::Key_PageDown, true);
        sendPress(Qt::Key_PageDown, true);
        const qreal secondPlannedY = scrollAnimation->property("to").toReal();
        QVERIFY(qAbs(firstPlannedY - (initialY + pageDistance)) < 1e-6);
        QVERIFY(qAbs(secondPlannedY
                     - (initialY + pageDistance * 2)) < 1e-6);
        QVERIFY(qAbs(phase(secondPlannedY, paddingTop, density)
                     - phase(initialY, paddingTop, density)) < 1e-6);
        sendRelease(Qt::Key_PageDown, true);
        sendRelease(Qt::Key_PageDown);
        waitSettled();
        sendPress(Qt::Key_PageUp);
        const qreal firstRepeatedUpY =
            scrollAnimation->property("to").toReal();
        sendRelease(Qt::Key_PageUp, true);
        sendPress(Qt::Key_PageUp, true);
        const qreal secondRepeatedUpY =
            scrollAnimation->property("to").toReal();
        QVERIFY(qAbs(firstRepeatedUpY
                     - (initialY + pageDistance)) < 1e-6);
        QVERIFY(qAbs(secondRepeatedUpY - initialY) < 1e-6);
        sendRelease(Qt::Key_PageUp, true);
        sendRelease(Qt::Key_PageUp);
        waitSettled();
        QVERIFY(qAbs(layout->contentY() - initialY) < 1e-6);

        // Spacing only insets a preview. It must not leak into the Grid row
        // pitch or page destination.
        layout->setSpacing(17);
        QVERIFY(setFixture(initialRow, initialColumn, initialY) >= 0);
        sendPress(Qt::Key_PageDown);
        QVERIFY(qAbs(scrollAnimation->property("to").toReal()
                     - (initialY + pageDistance)) < 1e-6);
        sendRelease(Qt::Key_PageDown);
        waitSettled();
        layout->setSpacing(7);

        // Height resize changes the integer page-row count. Width resize may
        // change both columns and cell centers; the live native geometry must
        // replace any stale cached X anchor before probing the target.
        panelItem->setHeight(351.3);
        panelItem->setWidth(521.4);
        QCoreApplication::processEvents();
        QTRY_VERIFY_WITH_TIMEOUT(layout->height() < 400, 1000);
        const int resizedColumns = columns();
        const int resizedPageRows = rowsPerPage();
        QVERIFY(resizedColumns >= 2);
        QVERIFY(resizedPageRows != pageRows);
        const int resizedColumn = qMin(1, resizedColumns - 1);
        const int resizedIndex = setFixture(
            initialRow, resizedColumn, initialY);
        QVERIFY(resizedIndex >= 0);
        panel->setProperty("currentItemCenterX", 9999.0);
        sendPress(Qt::Key_PageDown);
        QCOMPARE(session->currentIndex(),
                 (initialRow + resizedPageRows) * resizedColumns
                     + resizedColumn);
        QVERIFY(qAbs(scrollAnimation->property("to").toReal()
                     - (initialY + resizedPageRows * density)) < 1e-6);
        const QRectF resizedSource = layout->indexGeometry(resizedIndex);
        const QRectF resizedChromeTarget = panel->property(
            "cursorChromeTargetRect").toRectF();
        QVERIFY(qAbs(resizedChromeTarget.x()
                     - (layout->paddingLeft() + resizedSource.x() + 2.0))
                < 1e-6);
        sendRelease(Qt::Key_PageDown);
        waitSettled();

        // The final viewport clamp is allowed to leave the row lattice. A
        // PageUp from it first chooses a full page from the last aligned base,
        // then all following pages are phase-stable again. The partial final
        // row retains the column when present and otherwise uses the last item.
        const int gridColumns = columns();
        const int totalRows = (layout->count() + gridColumns - 1) / gridColumns;
        const int terminalColumn = gridColumns - 1;
        const qreal maxY = maximumY();
        const qreal nearEndY = qMax<qreal>(0, maxY - density * 1.25);
        QVERIFY(setFixture(qMax(0, totalRows - 2), terminalColumn,
                           nearEndY) >= 0);
        sendPress(Qt::Key_PageDown);
        QVERIFY(qAbs(scrollAnimation->property("to").toReal() - maxY) < 1e-6);
        const int lastRowStart = (totalRows - 1) * gridColumns;
        const int expectedTerminalIndex = qMin(
            layout->count() - 1, lastRowStart + terminalColumn);
        QCOMPARE(session->currentIndex(), expectedTerminalIndex);
        sendRelease(Qt::Key_PageDown);
        waitSettled();

        const qreal latticePhase = phase(nearEndY, paddingTop, density);
        const qreal latticeOrigin = paddingTop + latticePhase;
        const qreal alignedBase = latticeOrigin + std::floor(
            (maxY - latticeOrigin + 0.01) / density) * density;
        const qreal expectedTerminalUp = qMax<qreal>(
            0, alignedBase - resizedPageRows * density);
        sendPress(Qt::Key_PageUp);
        QVERIFY(qAbs(scrollAnimation->property("to").toReal()
                     - expectedTerminalUp) < 1e-6);
        QVERIFY(qAbs(phase(scrollAnimation->property("to").toReal(),
                           paddingTop, density) - latticePhase) < 1e-6);
        sendRelease(Qt::Key_PageUp);
        waitSettled();

        // A short catalog has no scroll range and Page keys remain exact no-ops.
        QVERIFY(session->applyExternalCatalog(plainCatalog(2), 2));
        QTRY_COMPARE_WITH_TIMEOUT(layout->count(), 2, 3000);
        QVERIFY(setFixture(0, 0, 0) >= 0);
        QCOMPARE(maximumY(), 0.0);
        sendPress(Qt::Key_PageDown);
        QCOMPARE(scrollAnimation->property("to").toReal(), 0.0);
        sendRelease(Qt::Key_PageDown);
        waitSettled();

        runtime->shutdown();
    }

    void settledCursorUsesPhysicalPixelGridButAnimationsStaySmooth() {
        QQuickView view;
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        options.maxDecodeThreads = 2;
        auto *runtime = ZoinGallery::GalleryRuntime::install(
            view.engine(), options);
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(
            QStringLiteral("cursor-physical-pixel-grid"));
        QVERIFY(session);
        QVERIFY(session->applyExternalCatalog(plainCatalog(80), 1));
        session->setCurrentIndex(0);

        QObject *panel = createPanel(
            view, session, QStringLiteral("pixelGridSession"),
            QStringLiteral("masonry"));
        QVERIFY(panel);
        auto *panelItem = qobject_cast<QQuickItem *>(panel);
        auto *layout = panel->findChild<MasonryLayout *>(
            QStringLiteral("galleryMasonryLayout"));
        QVERIFY(panelItem);
        QVERIFY(layout);
        panel->setProperty("devicePixelRatio", 1.75);
        // Include a fractional ancestor offset: snapping only the delegate's
        // local coordinates would still leave its rendered edges between
        // physical pixels in this case.
        panelItem->setX(0.2);
        panelItem->setY(0.3);
        QTRY_COMPARE_WITH_TIMEOUT(layout->count(), 80, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(
            panel->property("visualCursorIndex").toInt(), 0, 5000);

        auto *surface = findVisualItem(
            panelItem, QStringLiteral("gallerySelectionSurface-0"));
        QTRY_VERIFY_WITH_TIMEOUT(surface, 5000);
        auto *brick = qobject_cast<BrickItem *>(surface->parentItem());
        QVERIFY(brick);

        constexpr qreal dpr = 1.75;
        const auto onPhysicalPixel = [dpr](qreal logicalCoordinate) {
            return qAbs(logicalCoordinate * dpr
                        - qRound(logicalCoordinate * dpr)) < 0.001;
        };
        const auto cursorEdgesAreAligned = [&]() {
            const QPointF topLeft = surface->mapToScene(QPointF(0, 0));
            const QPointF bottomRight = surface->mapToScene(
                QPointF(surface->width(), surface->height()));
            return onPhysicalPixel(topLeft.x())
                && onPhysicalPixel(topLeft.y())
                && onPhysicalPixel(bottomRight.x())
                && onPhysicalPixel(bottomRight.y());
        };

        QTRY_VERIFY_WITH_TIMEOUT(
            surface->property("pixelAlignedCursorGeometry").toBool(), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(cursorEdgesAreAligned(), 3000);
        QVERIFY(surface->property("visualBorderPixelAligned").toBool());
        QVERIFY(surface->antialiasing());
        QCOMPARE(surface->property("visualBorderWidth").toReal(), 1.0);
        QVERIFY(qAbs(surface->x() - 2.0) > 0.01
                || qAbs(surface->y() - 2.0) > 0.01);

        QObject *chromeAnimation = panel->findChild<QObject *>(
            QStringLiteral("galleryCursorChromeGeometryAnimation"));
        QVERIFY(chromeAnimation);
        const QRectF startRect(11.13, 14.27, 100.37, 80.19);
        const QRectF targetRect(74.61, 63.44, 117.83, 92.57);
        QVariant started;
        QVERIFY(QMetaObject::invokeMethod(
            panel, "startCursorChromeGeometry", Qt::DirectConnection,
            Q_RETURN_ARG(QVariant, started),
            Q_ARG(QVariant, QVariant::fromValue(startRect)),
            Q_ARG(QVariant, QVariant::fromValue(targetRect)),
            Q_ARG(QVariant, QVariant(0))));
        QVERIFY(started.toBool());
        QTRY_VERIFY_WITH_TIMEOUT(chromeAnimation->property("running").toBool(),
                                 1000);
        QTRY_VERIFY_WITH_TIMEOUT(
            !surface->property("pixelAlignedCursorGeometry").toBool(), 1000);
        QVERIFY(surface->property("visualBorderPixelAligned").toBool());
        QVERIFY(surface->antialiasing());
        QVERIFY(qAbs(surface->x() - 2.0) < 0.001);
        QVERIFY(qAbs(surface->y() - 2.0) < 0.001);

        auto *animatedBorder = findVisualItem(
            panelItem, QStringLiteral("galleryCursorChromeBorder"));
        QVERIFY(animatedBorder);
        QVERIFY(animatedBorder->property(
                    "visualBorderPixelAligned").toBool());
        QVERIFY(animatedBorder->antialiasing());

        // The independent animation keeps its exact fractional endpoints;
        // only the delegate that replaces it after settling is quantized.
        QCOMPARE(panel->property("cursorChromeRect").toRectF(), startRect);
        QCOMPARE(panel->property("cursorChromeTargetRect").toRectF(),
                 targetRect);
        const QPointF animatedTopLeft = layout->mapToScene(
            startRect.topLeft());
        QVERIFY(!onPhysicalPixel(animatedTopLeft.x())
                || !onPhysicalPixel(animatedTopLeft.y()));

        QVERIFY(QMetaObject::invokeMethod(
            panel, "cancelCursorChromeTransition", Qt::DirectConnection));
        QTRY_VERIFY_WITH_TIMEOUT(
            surface->property("pixelAlignedCursorGeometry").toBool(), 1000);
        QTRY_VERIFY_WITH_TIMEOUT(cursorEdgesAreAligned(), 1000);
        QVERIFY(surface->property("visualBorderPixelAligned").toBool());
        QVERIFY(surface->antialiasing());

        const QRectF animatedBrickGeometry(
            brick->x() + 13.37, brick->y() + 7.19,
            brick->width() + 4.25, brick->height() + 3.75);
        brick->setGeometry(animatedBrickGeometry, true, false);
        QTRY_VERIFY_WITH_TIMEOUT(brick->geometryAnimationRunning(), 1000);
        QTRY_VERIFY_WITH_TIMEOUT(
            !surface->property("pixelAlignedCursorGeometry").toBool(), 1000);
        QVERIFY(surface->property("visualBorderPixelAligned").toBool());
        QVERIFY(surface->antialiasing());
        QVERIFY(qAbs(surface->x() - 2.0) < 0.001);
        QCOMPARE(surface->property("visualBorderWidth").toReal(), 1.0);
        QTRY_VERIFY_WITH_TIMEOUT(!brick->geometryAnimationRunning(), 1500);
        QTRY_VERIFY_WITH_TIMEOUT(
            surface->property("pixelAlignedCursorGeometry").toBool(), 1000);
        QTRY_VERIFY_WITH_TIMEOUT(cursorEdgesAreAligned(), 1000);
        QVERIFY(surface->property("visualBorderPixelAligned").toBool());
        QVERIFY(surface->antialiasing());

        runtime->shutdown();
    }

    void settledDetailsCursorRendersSolidPhysicalBorderPixels() {
        QQuickView view;
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        options.maxDecodeThreads = 2;
        auto *runtime = ZoinGallery::GalleryRuntime::install(
            view.engine(), options);
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(
            QStringLiteral("details-cursor-raster-grid"));
        QVERIFY(session);
        QVERIFY(session->applyExternalCatalog(plainCatalog(20), 1));
        session->setCurrentIndex(1);

        QObject *panel = createPanel(
            view, session, QStringLiteral("detailsCursorRasterSession"),
            QStringLiteral("details"));
        QVERIFY(panel);
        auto *panelItem = qobject_cast<QQuickItem *>(panel);
        auto *layout = panel->findChild<MasonryLayout *>(
            QStringLiteral("galleryMasonryLayout"));
        QVERIFY(panelItem);
        QVERIFY(layout);
        const qreal renderDpr = view.devicePixelRatio();
        QVERIFY(renderDpr > 0);
        panel->setProperty("devicePixelRatio", renderDpr);
        // Keep the sampled vertical edge away from the window/panel clip so
        // antialiasing outside the Rectangle cannot be discarded by clipping.
        layout->setPaddingLeft(24.0);
        layout->setPaddingRight(24.0);
        panel->setProperty("showDetailsHeader", false);
        panel->setProperty("theme", QVariantMap{
            {QStringLiteral("cursorBackground"),
             QStringLiteral("#18456e")},
            {QStringLiteral("cursorBorder"), QStringLiteral("#1d5888")},
        });
        panelItem->setX(0.2);
        panelItem->setY(0.3);
        QTRY_COMPARE_WITH_TIMEOUT(layout->count(), 20, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(
            panel->property("visualCursorIndex").toInt(), 1, 5000);

        auto *surface = findVisualItem(
            panelItem, QStringLiteral("gallerySelectionSurface-1"));
        QTRY_VERIFY_WITH_TIMEOUT(surface, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(
            surface->property("pixelAlignedCursorGeometry").toBool(), 3000);
        QVERIFY(surface->property("visualBorderPixelAligned").toBool());
        QVERIFY(surface->antialiasing());
        QTRY_VERIFY_WITH_TIMEOUT(surface->width() > 20
                                 && surface->height() > 8, 3000);

        view.update();
        QTest::qWait(50);
        const QImage raster = view.grabWindow().convertToFormat(
            QImage::Format_RGBA8888);
        QVERIFY(!raster.isNull());
        QVERIFY(raster.width() > 20 && raster.height() > 8);
        const qreal rasterScale = qreal(raster.width()) / qreal(view.width());
        QVERIFY2(qAbs(rasterScale - renderDpr) < 0.01,
                 qPrintable(QStringLiteral(
                     "grab=%1x%2 view=%3x%4 scale=%5 windowDpr=%6")
                     .arg(raster.width()).arg(raster.height())
                     .arg(view.width()).arg(view.height())
                     .arg(rasterScale).arg(view.devicePixelRatio())));

        const QColor border = surface->property(
            "visualBorderColor").value<QColor>();
        const QColor fill = surface->property("color").value<QColor>();
        QVERIFY(border.isValid());
        QVERIFY(fill.isValid());
        QVERIFY(border != fill);
        const auto closeColor = [](const QColor &actual,
                                   const QColor &expected) {
            constexpr int tolerance = 2;
            return qAbs(actual.red() - expected.red()) <= tolerance
                && qAbs(actual.green() - expected.green()) <= tolerance
                && qAbs(actual.blue() - expected.blue()) <= tolerance
                && qAbs(actual.alpha() - expected.alpha()) <= tolerance;
        };
        const QPointF surfaceTopLeft = surface->mapToScene(QPointF(0, 0));
        const int surfaceTop = qRound(surfaceTopLeft.y() * rasterScale);
        const int sampleX = qRound(
            (surfaceTopLeft.x() + surface->width() / 2) * rasterScale);
        const int borderPixels = qMax(
            1, qRound(surface->property("visualBorderWidth").toReal()
                      * renderDpr));

        const int surfaceLeft = qRound(surfaceTopLeft.x() * rasterScale);
        const int sampleY = qRound(
            (surfaceTopLeft.y() + surface->height() / 2) * rasterScale);
        QVERIFY(surfaceLeft >= 0
                && surfaceLeft + borderPixels < raster.width());
        QVERIFY(sampleY >= 0 && sampleY < raster.height());
        for (int x = 0; x < borderPixels; ++x) {
            const QColor actual = raster.pixelColor(surfaceLeft + x, sampleY);
            QVERIFY2(closeColor(actual, border),
                     qPrintable(QStringLiteral(
                         "left border pixel %1 is %2, expected solid %3")
                         .arg(x).arg(actual.name(QColor::HexArgb),
                                     border.name(QColor::HexArgb))));
        }
        const QColor firstHorizontalFill = raster.pixelColor(
            surfaceLeft + borderPixels, sampleY);
        QVERIFY2(closeColor(firstHorizontalFill, fill),
                 qPrintable(QStringLiteral(
                     "first horizontal fill pixel is %1, expected solid %2")
                     .arg(firstHorizontalFill.name(QColor::HexArgb),
                          fill.name(QColor::HexArgb))));

        QVERIFY(surfaceTop >= 0
                && surfaceTop + borderPixels < raster.height());
        QVERIFY(sampleX >= 0 && sampleX < raster.width());
        for (int y = 0; y < borderPixels; ++y) {
            const QColor actual = raster.pixelColor(sampleX, surfaceTop + y);
            QVERIFY2(closeColor(actual, border),
                     qPrintable(QStringLiteral(
                         "border pixel %1 is %2, expected solid %3")
                         .arg(y).arg(actual.name(QColor::HexArgb),
                                     border.name(QColor::HexArgb))));
        }
        const QColor firstFill = raster.pixelColor(
            sampleX, surfaceTop + borderPixels);
        QVERIFY2(closeColor(firstFill, fill),
                 qPrintable(QStringLiteral(
                     "first fill pixel is %1, expected solid %2")
                     .arg(firstFill.name(QColor::HexArgb),
                          fill.name(QColor::HexArgb))));

        runtime->shutdown();
    }

    void cursorHighlightRemainsVisibleDuringRevealNavigation() {
        QQuickView view;
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        auto *runtime = ZoinGallery::GalleryRuntime::install(
            view.engine(), options);
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(
            QStringLiteral("cursor-reveal-visibility"));
        QVERIFY(session);
        constexpr int entryCount = 260;
        QVariantList catalog = plainCatalog(entryCount);
        QStringList selectedIds;
        QVariantList appearance;
        selectedIds.reserve(entryCount);
        appearance.reserve(entryCount);
        const QVariantMap cursorStyle{
            {QStringLiteral("normal"), QVariantMap{
                 {QStringLiteral("background"), QStringLiteral("#102030")}}},
            {QStringLiteral("selected"), QVariantMap{
                 {QStringLiteral("background"), QStringLiteral("#405060")}}},
            {QStringLiteral("cursor"), QVariantMap{
                 {QStringLiteral("background"), QStringLiteral("#203040")}}},
            {QStringLiteral("selectedCursor"), QVariantMap{
                 {QStringLiteral("background"), QStringLiteral("#304050")}}},
        };
        for (int index = 0; index < catalog.size(); ++index) {
            QVariantMap entry = catalog.at(index).toMap();
            entry.insert(QStringLiteral("selected"), true);
            catalog[index] = entry;
            selectedIds.append(entry.value(QStringLiteral("entryId")).toString());
            appearance.append(QVariantMap{
                {QStringLiteral("entryId"),
                 entry.value(QStringLiteral("entryId"))},
                {QStringLiteral("highlightStyle"), cursorStyle},
            });
        }
        QVERIFY(session->applyExternalCatalog(catalog, 1));
        QVERIFY(session->applyExternalAppearance(appearance, 1));
        QVERIFY(session->applyExternalState(QStringLiteral("layout-entry-0"),
                                            0, selectedIds, 1));
        session->setCurrentIndex(0);

        QObject *panel = createPanel(
            view, session, QStringLiteral("cursorRevealSession"),
            QStringLiteral("details"));
        QVERIFY(panel);
        auto *panelItem = qobject_cast<QQuickItem *>(panel);
        auto *layout = panel->findChild<MasonryLayout *>(
            QStringLiteral("galleryMasonryLayout"));
        QObject *animation = panel->findChild<QObject *>(
            QStringLiteral("galleryPanelScrollAnimation"));
        QObject *chromeAnimation = panel->findChild<QObject *>(
            QStringLiteral("galleryCursorChromeGeometryAnimation"));
        QVERIFY(panelItem);
        QVERIFY(layout);
        QVERIFY(animation);
        QVERIFY(chromeAnimation);
        panel->setProperty("theme", QVariantMap{
            {QStringLiteral("cursorBackground"), QStringLiteral("#18456e")},
            {QStringLiteral("cursorBorder"), QStringLiteral("#1d5888")},
            {QStringLiteral("markedBackground"), QStringLiteral("#4f5037")},
        });
        QSignalSpy selectionSpy(
            panel, SIGNAL(selectionRequested(QString,QVariant)));
        QVERIFY(selectionSpy.isValid());
        panel->setProperty("showDetailsHeader", false);
        QTRY_COMPARE_WITH_TIMEOUT(layout->count(), entryCount, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(layout->height() > 100, 5000);
        panelItem->forceActiveFocus();
        view.requestActivate();
        QVERIFY(panelItem->hasActiveFocus());

        const auto stopAnimation = [&]() {
            QVERIFY(QMetaObject::invokeMethod(
                panel, "cancelCursorChromeTransition", Qt::DirectConnection));
            QVERIFY(QMetaObject::invokeMethod(
                animation, "stop", Qt::DirectConnection));
            QCoreApplication::processEvents();
        };
        const auto sendKey = [&](QEvent::Type type, Qt::Key key,
                                 Qt::KeyboardModifiers modifiers =
                                     Qt::NoModifier) {
            QKeyEvent event(type, key, modifiers);
            QCoreApplication::sendEvent(&view, &event);
            QVERIFY(event.isAccepted());
        };
        const auto assertPaintedCursorVisible = [&](const QString &context) {
            const int visual = panel->property("visualCursorIndex").toInt();
            QVERIFY2(visual >= 0 && visual < layout->count(),
                     qPrintable(context + QStringLiteral(
                         ": invalid visual cursor %1").arg(visual)));
            QVERIFY2(indexHasPaintedAreaInViewport(layout, visual),
                     qPrintable(context + QStringLiteral(
                         ": visual cursor %1 at %2,%3 %4x%5 outside %6..%7")
                         .arg(visual)
                         .arg(layout->indexGeometry(visual).x())
                         .arg(layout->indexGeometry(visual).y())
                         .arg(layout->indexGeometry(visual).width())
                         .arg(layout->indexGeometry(visual).height())
                         .arg(layout->contentY())
                         .arg(layout->contentY() + layout->height())));
            auto *surface = findVisualItem(
                panelItem,
                QStringLiteral("gallerySelectionSurface-%1").arg(visual));
            QVERIFY2(surface,
                     qPrintable(context + QStringLiteral(
                         ": no delegate selection surface for visual cursor %1")
                         .arg(visual)));
            QVERIFY(surface->isVisible());
            QVERIFY(surface->parentItem());
            QVERIFY2(surface->parentItem()->property("current").toBool(),
                     qPrintable(context + QStringLiteral(
                         ": delegate %1 is not painted as current")
                         .arg(visual)));
        };
        const auto waitForCoordinatedReveal = [&](int logicalTarget,
                                                  const QString &context) {
            QVERIFY2(panel->property(
                         "cursorChromeTransitionActive").toBool(),
                     qPrintable(context + QStringLiteral(
                         ": independent cursor transition did not start")));
            const QRectF sourceRect = panel->property(
                "cursorChromeRect").toRectF();
            const QRectF targetRect = panel->property(
                "cursorChromeTargetRect").toRectF();
            QVERIFY(sourceRect.isValid() && !sourceRect.isEmpty());
            QVERIFY(targetRect.isValid() && !targetRect.isEmpty());
            const int mode = layout->presentationMode();
            const qreal margin = mode == MasonryLayout::Details ? 0.0
                : (mode == MasonryLayout::Columns ? 1.0 : 2.0);
            const qreal plannedContentY = animation->property(
                    "running").toBool()
                ? animation->property("to").toReal() : layout->contentY();
            const QRectF targetGeometry = layout->indexGeometry(logicalTarget);
            const QRectF expectedTarget(
                layout->property("paddingLeft").toReal()
                    + targetGeometry.x() + margin,
                targetGeometry.y() - plannedContentY + margin,
                targetGeometry.width() - margin * 2,
                targetGeometry.height() - margin * 2);
            QVERIFY(qAbs(targetRect.x() - expectedTarget.x()) < 0.05);
            QVERIFY(qAbs(targetRect.y() - expectedTarget.y()) < 0.05);
            QVERIFY(qAbs(targetRect.width() - expectedTarget.width()) < 0.05);
            QVERIFY(qAbs(targetRect.height() - expectedTarget.height()) < 0.05);
            QCOMPARE(panel->property("cursorChromeRadius").toReal(),
                     mode == MasonryLayout::Details
                             || mode == MasonryLayout::Columns ? 4.0 : 6.0);
            QCOMPARE(panel->property("cursorChromeBorderWidth").toReal(), 1.0);
            if (mode == MasonryLayout::Details) {
                QCOMPARE(panel->property("cursorChromeFillColor")
                             .value<QColor>(), QColor(QStringLiteral("#304050")));
                QCOMPARE(panel->property("cursorChromeBorderColor")
                             .value<QColor>(), QColor(QStringLiteral("#1d5888")));
            } else {
                QCOMPARE(panel->property("cursorChromeFillColor")
                             .value<QColor>(),
                         panel->property("cursorColor").value<QColor>());
            }
            auto *underlay = findVisualItem(
                panelItem, QStringLiteral("galleryCursorChromeUnderlay"));
            auto *border = findVisualItem(
                panelItem, QStringLiteral("galleryCursorChromeBorder"));
            QVERIFY(underlay && border);

            const auto between = [](qreal value, qreal from, qreal to) {
                return value >= qMin(from, to) - 0.05
                    && value <= qMax(from, to) + 0.05;
            };
            QElapsedTimer elapsed;
            elapsed.start();
            int sampledFrames = 0;
            while ((animation->property("running").toBool()
                    || chromeAnimation->property("running").toBool())
                   && elapsed.elapsed() < 1200) {
                QTest::qWait(4);
                assertPaintedCursorVisible(context);
                if (panel->property(
                        "cursorChromeTransitionActive").toBool()) {
                    const QRectF rect = panel->property(
                        "cursorChromeRect").toRectF();
                    QVERIFY(between(rect.x(), sourceRect.x(), targetRect.x()));
                    QVERIFY(between(rect.y(), sourceRect.y(), targetRect.y()));
                    QVERIFY(between(rect.width(), sourceRect.width(),
                                    targetRect.width()));
                    QVERIFY(between(rect.height(), sourceRect.height(),
                                    targetRect.height()));
                    QVERIFY(qAbs(underlay->x() - rect.x()) < 0.001);
                    QVERIFY(qAbs(underlay->y() - rect.y()) < 0.001);
                    QVERIFY(qAbs(underlay->width() - rect.width()) < 0.001);
                    QVERIFY(qAbs(underlay->height() - rect.height()) < 0.001);
                    QVERIFY(qAbs(border->x() - rect.x()) < 0.001);
                    QVERIFY(qAbs(border->y() - rect.y()) < 0.001);
                    QVERIFY(underlay->parentItem()->isVisible());
                    QVERIFY(border->parentItem()->isVisible());

                    const int visual = panel->property(
                        "visualCursorIndex").toInt();
                    auto *rowSurface = findVisualItem(
                        panelItem, QStringLiteral("gallerySelectionSurface-%1")
                                       .arg(visual));
                    QVERIFY(rowSurface);
                    QVERIFY(rowSurface->parentItem()->property(
                                "cursorChromeSuppressed").toBool());
                    const int covered = panel->property(
                        "cursorChromeCoveredIndex").toInt();
                    auto *coveredSurface = findVisualItem(
                        panelItem, QStringLiteral("gallerySelectionSurface-%1")
                                       .arg(covered));
                    if (coveredSurface) {
                        QCOMPARE(coveredSurface->property(
                                     "visualBorderWidth").toReal(), 0.0);
                        QCOMPARE(coveredSurface->property(
                                     "color").value<QColor>(),
                                 QColor(Qt::transparent));
                    }
                }
                ++sampledFrames;
            }
            QVERIFY2(sampledFrames > 1,
                     qPrintable(context + QStringLiteral(
                         ": reveal did not produce animation frames")));
            QVERIFY2(!animation->property("running").toBool()
                         && !chromeAnimation->property("running").toBool(),
                     qPrintable(context + QStringLiteral(
                         ": coordinated animations did not settle")));
            QTRY_VERIFY_WITH_TIMEOUT(
                !panel->property("cursorChromeTransitionActive").toBool(), 1000);
            assertPaintedCursorVisible(context + QStringLiteral(" final"));
            QCOMPARE(panel->property("visualCursorIndex").toInt(),
                     logicalTarget);
            auto *surface = findVisualItem(
                panelItem, QStringLiteral("gallerySelectionSurface-%1")
                               .arg(logicalTarget));
            QVERIFY(surface);
            QVERIFY(!surface->parentItem()->property(
                         "cursorChromeSuppressed").toBool());
        };

        const QList<std::tuple<QString, MasonryLayout::PresentationMode,
                               qreal>> modes{
            {QStringLiteral("grid"), MasonryLayout::Grid, 120},
            {QStringLiteral("icons"), MasonryLayout::Icons, 120},
            {QStringLiteral("masonry"), MasonryLayout::Masonry, 120},
        };
        for (const auto &[modeName, nativeMode, density] : modes) {
            panel->setProperty("presentationMode", modeName);
            QTRY_COMPARE_WITH_TIMEOUT(layout->presentationMode(), nativeMode,
                                      3000);
            layout->setDensity(density);
            stopAnimation();

            // Align a complete current row exactly with the viewport bottom.
            // Its downward neighbour is then wholly clipped at key-down time,
            // which deterministically reproduces the old disappearing cursor.
            int boundary = -1;
            int downTarget = -1;
            for (int candidate = 1; candidate < entryCount - 1; ++candidate) {
                const int next = layout->neighborIndex(
                    candidate, MasonryLayout::NavigateDown);
                if (next == candidate) {
                    continue;
                }
                const QRectF current = layout->indexGeometry(candidate);
                const QRectF target = layout->indexGeometry(next);
                const qreal desiredY = current.bottom() - layout->height();
                const qreal maximumY = qMax<qreal>(
                    0, layout->contentHeight() - layout->height());
                if (current.isEmpty() || target.isEmpty() || desiredY <= 0
                    || desiredY >= maximumY
                    || target.top() + 0.01 < current.bottom()) {
                    continue;
                }
                layout->setContentY(desiredY);
                session->setCurrentIndex(candidate);
                QVERIFY(invokeEnsureCurrentVisible(panel, false));
                QCoreApplication::processEvents();
                if (indexHasPaintedAreaInViewport(layout, candidate)
                    && !indexHasPaintedAreaInViewport(layout, next)) {
                    boundary = candidate;
                    downTarget = next;
                    break;
                }
            }
            QVERIFY2(boundary >= 0,
                     qPrintable(modeName + QStringLiteral(
                         ": no exact bottom-edge navigation fixture")));
            // Fixture construction moves content and the session directly,
            // bypassing GalleryPanel's normal navigation function. Clear any
            // pending hand-off left by the preceding mode before exercising
            // the actual key path under test.
            panel->setProperty("pendingVisualCursorIndex", -1);
            panel->setProperty("visualCursorIndex", boundary);
            QCoreApplication::processEvents();
            QVERIFY2(panel->property("visualCursorIndex").toInt() == boundary,
                     qPrintable(modeName + QStringLiteral(
                         ": fixture visual cursor changed from %1 to %2, "
                         "pending %3, contentY %4")
                         .arg(boundary)
                         .arg(panel->property("visualCursorIndex").toInt())
                         .arg(panel->property(
                                  "pendingVisualCursorIndex").toInt())
                         .arg(layout->contentY())));
            assertPaintedCursorVisible(modeName + QStringLiteral(" Down start"));

            const Qt::KeyboardModifiers navigationModifiers = Qt::NoModifier;
            sendKey(QEvent::KeyPress, Qt::Key_Down, navigationModifiers);
            QCOMPARE(session->currentIndex(), downTarget);
            // The authoritative target is still outside on this synchronous
            // press, proving that painting it directly would leave no cursor.
            QVERIFY(!indexHasPaintedAreaInViewport(layout, downTarget));
            QVERIFY(panel->property("visualCursorIndex").toInt()
                    != downTarget);
            assertPaintedCursorVisible(modeName + QStringLiteral(" Down press"));
            sendKey(QEvent::KeyRelease, Qt::Key_Down, navigationModifiers);
            waitForCoordinatedReveal(
                downTarget, modeName + QStringLiteral(" Down reveal"));

            // Mirror the fixture at the viewport top. The logical Up target
            // starts wholly clipped, so the visual cursor must traverse the
            // same preserved anchor in the opposite direction.
            stopAnimation();
            int topBoundary = -1;
            int upTarget = -1;
            for (int candidate = entryCount - 2; candidate > 0; --candidate) {
                const int previous = layout->neighborIndex(
                    candidate, MasonryLayout::NavigateUp);
                if (previous == candidate) {
                    continue;
                }
                const QRectF current = layout->indexGeometry(candidate);
                const QRectF target = layout->indexGeometry(previous);
                const qreal desiredY = current.top();
                const qreal maximumY = qMax<qreal>(
                    0, layout->contentHeight() - layout->height());
                if (current.isEmpty() || target.isEmpty() || desiredY <= 0
                    || desiredY >= maximumY
                    || target.bottom() - 0.01 > current.top()) {
                    continue;
                }
                layout->setContentY(desiredY);
                session->setCurrentIndex(candidate);
                QVERIFY(invokeEnsureCurrentVisible(panel, false));
                QCoreApplication::processEvents();
                if (indexHasPaintedAreaInViewport(layout, candidate)
                    && !indexHasPaintedAreaInViewport(layout, previous)) {
                    topBoundary = candidate;
                    upTarget = previous;
                    break;
                }
            }
            QVERIFY2(topBoundary >= 0,
                     qPrintable(modeName + QStringLiteral(
                         ": no exact top-edge navigation fixture")));
            panel->setProperty("pendingVisualCursorIndex", -1);
            panel->setProperty("visualCursorIndex", topBoundary);
            QCoreApplication::processEvents();
            assertPaintedCursorVisible(modeName + QStringLiteral(" Up start"));
            sendKey(QEvent::KeyPress, Qt::Key_Up, navigationModifiers);
            QCOMPARE(session->currentIndex(), upTarget);
            QVERIFY(!indexHasPaintedAreaInViewport(layout, upTarget));
            QVERIFY(panel->property("visualCursorIndex").toInt()
                    != upTarget);
            assertPaintedCursorVisible(modeName + QStringLiteral(" Up press"));
            sendKey(QEvent::KeyRelease, Qt::Key_Up, navigationModifiers);
            waitForCoordinatedReveal(
                upTarget, modeName + QStringLiteral(" Up reveal"));

            // PageDown deliberately selects a target almost one viewport away.
            // Keep the old visible highlight until the page target enters,
            // while navigation and deferred host commit already use the target.
            stopAnimation();
            layout->setContentY(0);
            session->setCurrentIndex(0);
            QVERIFY(invokeEnsureCurrentVisible(panel, false));
            QCoreApplication::processEvents();
            QTRY_COMPARE_WITH_TIMEOUT(
                panel->property("visualCursorIndex").toInt(), 0, 1000);
            sendKey(QEvent::KeyPress, Qt::Key_PageDown,
                    navigationModifiers);
            const int pageTarget = session->currentIndex();
            QVERIFY2(pageTarget > 0,
                     qPrintable(modeName + QStringLiteral(
                         ": PageDown did not advance")));
            if (animation->property("running").toBool()) {
                // Depending on the retained vertical anchor, the page target
                // can already touch the old viewport. If it is still clipped,
                // the visual cursor must remain on an intermediate item; if it
                // is visible, handing off immediately is equally correct.
                if (!indexHasPaintedAreaInViewport(layout, pageTarget)) {
                    QVERIFY(panel->property("visualCursorIndex").toInt()
                            != pageTarget);
                }
            }
            assertPaintedCursorVisible(
                modeName + QStringLiteral(" PageDown press"));
            sendKey(QEvent::KeyRelease, Qt::Key_PageDown,
                    navigationModifiers);
            waitForCoordinatedReveal(
                pageTarget, modeName + QStringLiteral(" PageDown reveal"));

            sendKey(QEvent::KeyPress, Qt::Key_PageUp,
                    navigationModifiers);
            const int previousPageTarget = session->currentIndex();
            QVERIFY2(previousPageTarget < pageTarget,
                     qPrintable(modeName + QStringLiteral(
                         ": PageUp did not move toward the start")));
            assertPaintedCursorVisible(
                modeName + QStringLiteral(" PageUp press"));
            sendKey(QEvent::KeyRelease, Qt::Key_PageUp,
                    navigationModifiers);
            waitForCoordinatedReveal(
                previousPageTarget,
                modeName + QStringLiteral(" PageUp reveal"));

            sendKey(QEvent::KeyPress, Qt::Key_End, navigationModifiers);
            QCOMPARE(session->currentIndex(), entryCount - 1);
            QVERIFY(panel->property("cursorChromeTransitionActive").toBool());
            sendKey(QEvent::KeyRelease, Qt::Key_End, navigationModifiers);
            waitForCoordinatedReveal(
                entryCount - 1, modeName + QStringLiteral(" End reveal"));
            sendKey(QEvent::KeyPress, Qt::Key_Home, navigationModifiers);
            QCOMPARE(session->currentIndex(), 0);
            QVERIFY(panel->property("cursorChromeTransitionActive").toBool());
            sendKey(QEvent::KeyRelease, Qt::Key_Home, navigationModifiers);
            waitForCoordinatedReveal(
                0, modeName + QStringLiteral(" Home reveal"));

            // A pointer press supersedes a still-running keyboard destination.
            // Freeze the animated frame, select a fully visible center item,
            // and verify that no stale pending target carries it offscreen.
            sendKey(QEvent::KeyPress, Qt::Key_PageDown,
                    navigationModifiers);
            sendKey(QEvent::KeyRelease, Qt::Key_PageDown,
                    navigationModifiers);
            QVERIFY(animation->property("running").toBool()
                    || chromeAnimation->property("running").toBool());
            if (nativeMode == MasonryLayout::Details) {
                QVERIFY(panel->property(
                            "cursorChromeTransitionActive").toBool());
                const QRectF liveRect = panel->property(
                    "cursorChromeRect").toRectF();
                layout->setDensity(25.2);
                QCoreApplication::processEvents();
                QVERIFY(panel->property(
                            "cursorChromeTransitionActive").toBool());
                QVERIFY(panel->property("cursorChromeRect").toRectF().isValid());
                QVERIFY(liveRect.isValid());
                layout->setDensity(24.2);
                QCoreApplication::processEvents();
                QVERIFY(panel->property(
                            "cursorChromeTransitionActive").toBool());
            }
            int clickedIndex = layout->indexAt(
                layout->width() / 2,
                layout->contentY() + layout->height() / 2);
            if (clickedIndex < 0) {
                const QVariantList visible = layout->visibleIndexes();
                QVERIFY(!visible.isEmpty());
                clickedIndex = visible.at(visible.size() / 2).toInt();
            }
            QVERIFY(indexHasPaintedAreaInViewport(layout, clickedIndex));
            QVERIFY(QMetaObject::invokeMethod(
                panel, "handlePointerPress", Qt::DirectConnection,
                Q_ARG(QVariant, QVariant(clickedIndex)),
                Q_ARG(QVariant, QVariant::fromValue(int(Qt::LeftButton))),
                Q_ARG(QVariant, QVariant::fromValue(int(Qt::NoModifier)))));
            QCOMPARE(session->currentIndex(), clickedIndex);
            QCOMPARE(panel->property("visualCursorIndex").toInt(),
                     clickedIndex);
            QCOMPARE(panel->property("pendingVisualCursorIndex").toInt(), -1);
            QVERIFY(!animation->property("running").toBool());
            QVERIFY(!panel->property(
                        "cursorChromeTransitionActive").toBool());
            assertPaintedCursorVisible(
                modeName + QStringLiteral(" pointer interruption"));

            if (nativeMode == MasonryLayout::Details) {
                // A manual wheel gesture has no keyboard destination. It
                // cancels both chrome and the intermediate visual identity,
                // then resumes ordinary logical-cursor scrolling semantics.
                sendKey(QEvent::KeyPress, Qt::Key_PageDown,
                        navigationModifiers);
                sendKey(QEvent::KeyRelease, Qt::Key_PageDown,
                        navigationModifiers);
                QVERIFY(panel->property(
                            "cursorChromeTransitionActive").toBool());
                QVariant wheelHandled;
                QVERIFY(QMetaObject::invokeMethod(
                    panel, "handlePanelWheel", Qt::DirectConnection,
                    Q_RETURN_ARG(QVariant, wheelHandled),
                    Q_ARG(QVariant, QVariant(18.0)),
                    Q_ARG(QVariant, QVariant(120.0)),
                    Q_ARG(QVariant, QVariant::fromValue(
                        int(Qt::NoModifier))),
                    Q_ARG(QVariant, QVariant(0.0)),
                    Q_ARG(QVariant, QVariant(0.0))));
                QVERIFY(wheelHandled.toBool());
                QVERIFY(!panel->property(
                            "cursorChromeTransitionActive").toBool());
                QCOMPARE(panel->property("pendingVisualCursorIndex").toInt(),
                         -1);
                QCOMPARE(panel->property("visualCursorIndex").toInt(),
                         session->currentIndex());
                stopAnimation();

                // Inactive panels never paint cursor chrome. Cancellation must
                // leave the row's selected/normal state intact and restoring
                // activity must return the ordinary delegate-owned cursor.
                sendKey(QEvent::KeyPress, Qt::Key_PageDown,
                        navigationModifiers);
                sendKey(QEvent::KeyRelease, Qt::Key_PageDown,
                        navigationModifiers);
                QVERIFY(animation->property("running").toBool());
                QVERIFY(panel->property(
                            "cursorChromeTransitionActive").toBool());
                panel->setProperty("showCursor", false);
                QCoreApplication::processEvents();
                QVERIFY(!panel->property(
                            "cursorChromeTransitionActive").toBool());
                auto *underlay = findVisualItem(
                    panelItem, QStringLiteral("galleryCursorChromeUnderlay"));
                auto *border = findVisualItem(
                    panelItem, QStringLiteral("galleryCursorChromeBorder"));
                QVERIFY(underlay && border);
                QVERIFY(!underlay->parentItem()->isVisible());
                QVERIFY(!border->parentItem()->isVisible());
                int visual = panel->property("visualCursorIndex").toInt();
                auto *inactiveSurface = findVisualItem(
                    panelItem, QStringLiteral("gallerySelectionSurface-%1")
                                   .arg(visual));
                if (!inactiveSurface) {
                    const QVariantList visible = layout->visibleIndexes();
                    QVERIFY(!visible.isEmpty());
                    visual = visible.at(visible.size() / 2).toInt();
                    inactiveSurface = findVisualItem(
                        panelItem,
                        QStringLiteral("gallerySelectionSurface-%1")
                            .arg(visual));
                }
                QVERIFY(inactiveSurface);
                QCOMPARE(inactiveSurface->property(
                             "visualBorderWidth").toReal(), 0.0);
                QCOMPARE(inactiveSurface->property("color").value<QColor>(),
                         QColor(QStringLiteral("#405060")));
                stopAnimation();
                session->setCurrentIndex(visual);
                panel->setProperty("visualCursorIndex", visual);
                QVERIFY(invokeEnsureCurrentVisible(panel, false));
                panel->setProperty("showCursor", true);
                QCoreApplication::processEvents();
                assertPaintedCursorVisible(
                    modeName + QStringLiteral(" active cursor restored"));
            }
        }

        // Columns swaps a bounded virtual window instead of changing contentY.
        // Compact modes deliberately settle both that window and their cursor
        // chrome atomically, without leaving an independent overlay animation.
        panel->setProperty("presentationMode", QStringLiteral("columns"));
        panel->setProperty("columnCount", 2);
        QTRY_COMPARE_WITH_TIMEOUT(layout->presentationMode(),
                                  MasonryLayout::Columns, 3000);
        layout->setDensity(30);
        layout->setWindowTopIndex(0);
        session->setCurrentIndex(0);
        panel->setProperty("pendingVisualCursorIndex", -1);
        panel->setProperty("visualCursorIndex", 0);
        QCoreApplication::processEvents();
        assertPaintedCursorVisible(QStringLiteral("columns PageDown start"));
        const auto assertColumnsAtomic = [&](int target,
                                              const QString &context) {
            QVERIFY2(!animation->property("running").toBool(),
                     qPrintable(context + QStringLiteral(
                         ": viewport animation is running")));
            QVERIFY2(!chromeAnimation->property("running").toBool(),
                     qPrintable(context + QStringLiteral(
                         ": cursor animation is running")));
            QVERIFY(!panel->property(
                         "cursorChromeTransitionActive").toBool());
            QCOMPARE(panel->property("pendingVisualCursorIndex").toInt(), -1);
            QCOMPARE(panel->property("visualCursorIndex").toInt(), target);
            assertPaintedCursorVisible(context);
        };

        // A three-column viewport must remain stationary while the cursor
        // traverses columns that are already visible. Only crossing from the
        // rightmost visible column into the next one advances the horizontal
        // strip, by exactly one column width.
        panel->setProperty("columnCount", 3);
        layout->setWindowTopIndex(0);
        session->setCurrentIndex(0);
        panel->setProperty("pendingVisualCursorIndex", -1);
        panel->setProperty("visualCursorIndex", 0);
        QCoreApplication::processEvents();
        const int threeColumnRows = qMax(
            1, int(std::floor((layout->height()
                - layout->paddingTop() - layout->paddingBottom())
                / layout->density())));
        const qreal threeColumnOffset = layout->columnStride();
        for (int visibleColumn = 1; visibleColumn <= 2; ++visibleColumn) {
            sendKey(QEvent::KeyPress, Qt::Key_Right);
            QCOMPARE(session->currentIndex(),
                     visibleColumn * threeColumnRows);
            QCOMPARE(layout->contentY(), 0.0);
            assertColumnsAtomic(session->currentIndex(),
                                QStringLiteral("columns visible Right"));
            sendKey(QEvent::KeyRelease, Qt::Key_Right);
        }
        sendKey(QEvent::KeyPress, Qt::Key_Right);
        QCOMPARE(session->currentIndex(), 3 * threeColumnRows);
        QVERIFY(qAbs(layout->contentY() - threeColumnOffset) < 0.01);
        assertColumnsAtomic(session->currentIndex(),
                            QStringLiteral("columns edge Right"));
        sendKey(QEvent::KeyRelease, Qt::Key_Right);

        panel->setProperty("columnCount", 2);
        layout->setWindowTopIndex(0);
        session->setCurrentIndex(0);
        panel->setProperty("pendingVisualCursorIndex", -1);
        panel->setProperty("visualCursorIndex", 0);
        QCoreApplication::processEvents();

        sendKey(QEvent::KeyPress, Qt::Key_PageDown);
        const int columnsPageTarget = session->currentIndex();
        QVERIFY(columnsPageTarget > 0);
        QVERIFY(layout->windowTopIndex() > 0);
        assertColumnsAtomic(columnsPageTarget,
                            QStringLiteral("columns PageDown"));
        sendKey(QEvent::KeyRelease, Qt::Key_PageDown);

        sendKey(QEvent::KeyPress, Qt::Key_PageUp);
        QCOMPARE(session->currentIndex(), 0);
        QCOMPARE(layout->windowTopIndex(), 0);
        assertColumnsAtomic(0, QStringLiteral("columns PageUp"));
        sendKey(QEvent::KeyRelease, Qt::Key_PageUp);

        const QVariantList firstWindow = layout->visibleIndexes();
        QVERIFY(firstWindow.size() >= 4);
        const int rightEdge = firstWindow.constLast().toInt();
        session->setCurrentIndex(rightEdge);
        panel->setProperty("pendingVisualCursorIndex", -1);
        panel->setProperty("visualCursorIndex", rightEdge);
        QCoreApplication::processEvents();
        const int firstWindowTop = layout->windowTopIndex();
        sendKey(QEvent::KeyPress, Qt::Key_Right);
        const int rightPageTarget = session->currentIndex();
        QVERIFY(rightPageTarget > rightEdge);
        QVERIFY(layout->windowTopIndex() > firstWindowTop);
        assertColumnsAtomic(rightPageTarget,
                            QStringLiteral("columns Right page"));
        sendKey(QEvent::KeyRelease, Qt::Key_Right);

        const int rightWindowTop = layout->windowTopIndex();
        session->setCurrentIndex(rightWindowTop);
        panel->setProperty("pendingVisualCursorIndex", -1);
        panel->setProperty("visualCursorIndex", rightWindowTop);
        QCoreApplication::processEvents();
        sendKey(QEvent::KeyPress, Qt::Key_Left);
        const int leftPageTarget = session->currentIndex();
        QVERIFY(leftPageTarget < rightWindowTop);
        QVERIFY(layout->windowTopIndex() < rightWindowTop);
        assertColumnsAtomic(leftPageTarget,
                            QStringLiteral("columns Left page"));
        sendKey(QEvent::KeyRelease, Qt::Key_Left);

        sendKey(QEvent::KeyPress, Qt::Key_End);
        QCOMPARE(session->currentIndex(), entryCount - 1);
        assertColumnsAtomic(entryCount - 1,
                            QStringLiteral("columns End"));
        sendKey(QEvent::KeyRelease, Qt::Key_End);
        sendKey(QEvent::KeyPress, Qt::Key_Home);
        QCOMPARE(session->currentIndex(), 0);
        assertColumnsAtomic(0, QStringLiteral("columns Home"));
        sendKey(QEvent::KeyRelease, Qt::Key_Home);

        runtime->shutdown();
    }

    void offscreenEntryIsReplannedForALargerPresentationTier() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString templatePath = directory.filePath(
            QStringLiteral("template.png"));
        QImage source(QSize(1024, 768), QImage::Format_RGB32);
        source.fill(QColor(QStringLiteral("#4285b4")));
        QVERIFY(source.save(templatePath));

        constexpr int entryCount = 72;
        QVariantList catalog;
        catalog.reserve(entryCount);
        for (int index = 0; index < entryCount; ++index) {
            const QString path = directory.filePath(
                QStringLiteral("image-%1.png").arg(index, 3, 10,
                                                    QLatin1Char('0')));
            QVERIFY(QFile::copy(templatePath, path));
            catalog.append(catalogEntry(index, path));
        }

        QQuickView view;
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        options.maxDecodeThreads = 2;
        options.thumbnailCacheByteBudget = 128 * 1024 * 1024;
        auto *runtime = ZoinGallery::GalleryRuntime::install(
            view.engine(), options);
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(
            QStringLiteral("layout-offscreen-tier"));
        QVERIFY(session);
        QVERIFY(session->applyExternalCatalog(catalog, 1));

        QObject *panel = createPanel(view, session,
                                     QStringLiteral("tierSession"));
        QVERIFY(panel);
        auto *layout = panel->findChild<MasonryLayout *>(
            QStringLiteral("galleryMasonryLayout"));
        QVERIFY(layout);
        QTRY_COMPARE_WITH_TIMEOUT(layout->count(), entryCount, 5000);
        auto *lastImage = session->model()
            ->data(session->model()->index(entryCount - 1, 0),
                   FileListModel::ImageFileRole)
            .value<ImageFile *>();
        QVERIFY(lastImage);
        QTRY_VERIFY_WITH_TIMEOUT(lastImage->fullSize().isValid(), 10000);

        panel->setProperty("presentationMode", QStringLiteral("columns"));
        layout->setDensity(22);
        layout->setColumnCount(2);
        const int columnCapacity = qMax(
            1, int(std::floor(layout->height() / layout->density()))) * 2;
        layout->setWindowTopIndex(entryCount - columnCapacity);
        layout->reReadAndDecodeThumbnails();
        QTRY_VERIFY_WITH_TIMEOUT(!lastImage->imageIdUrl().isEmpty(), 10000);
        const QString smallProviderUrl = lastImage->imageIdUrl();

        layout->setWindowTopIndex(0);
        panel->setProperty("presentationMode", QStringLiteral("grid"));
        layout->setDensity(320);
        layout->reReadAndDecodeThumbnails();
        QTest::qWait(50);
        QCOMPARE(lastImage->imageIdUrl(), smallProviderUrl);
        const quint64 storesBeforeLargeTier =
            runtime->thumbnailCacheStoreCount();

        const qreal maximumY = qMax<qreal>(
            0, layout->contentHeight() - layout->height());
        // Repeated disjoint jumps exercise the desired-set generation gate;
        // stale pages must be canceled rather than accumulating 72 requests.
        for (int iteration = 0; iteration < 6; ++iteration) {
            layout->setContentY(iteration % 2 == 0 ? maximumY : 0);
            QCoreApplication::processEvents();
        }
        layout->setContentY(maximumY);
        QCoreApplication::processEvents();
        QVERIFY(runtime->thumbnailCachePendingRequestCount() <=
                layout->overscanIndexes().size());
        QTRY_VERIFY_WITH_TIMEOUT(
            lastImage->imageIdUrl() != smallProviderUrl, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(
            runtime->thumbnailCacheStoreCount() > storesBeforeLargeTier,
            10000);
        QVERIFY(runtime->thumbnailCachePendingRequestCount() <=
                layout->overscanIndexes().size());
    }

    void svgThumbnailsUseTheCurrentPhysicalPreviewResolution() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString svgPath = directory.filePath(
            QStringLiteral("vector-thumbnail.svg"));
        QFile svgFile(svgPath);
        QVERIFY(svgFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
        const QByteArray svg = R"SVG(<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24"><rect width="24" height="24" fill="#4f9bd8"/><circle cx="12" cy="12" r="7" fill="#f4d35e"/></svg>)SVG";
        QCOMPARE(svgFile.write(svg), qint64(svg.size()));
        svgFile.close();

        QQuickView view;
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        options.maxDecodeThreads = 1;
        auto *runtime = ZoinGallery::GalleryRuntime::install(
            view.engine(), options);
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(
            QStringLiteral("svg-thumbnail-resolution"));
        QVERIFY(session);
        QVERIFY(session->applyExternalCatalog(
            {catalogEntry(0, svgPath)}, 1));

        QObject *panel = createPanel(
            view, session, QStringLiteral("svgThumbnailSession"),
            QStringLiteral("grid"));
        QVERIFY(panel);
        panel->setProperty("devicePixelRatio", 1.75);
        auto *layout = panel->findChild<MasonryLayout *>(
            QStringLiteral("galleryMasonryLayout"));
        QVERIFY(layout);
        layout->setDensity(180);

        auto *thumbnail = panel->findChild<QQuickItem *>(
            QStringLiteral("galleryThumbnail-0"));
        auto *thumbnailImage = panel->findChild<QQuickItem *>(
            QStringLiteral("galleryThumbnailImage-0"));
        auto *thumbnailShader = panel->findChild<QQuickItem *>(
            QStringLiteral("galleryThumbnailShader-0"));
        QVERIFY(thumbnail && thumbnailImage && thumbnailShader);
        QTRY_VERIFY_WITH_TIMEOUT(thumbnail->isVisible(), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(thumbnailImage->property("status").toInt(),
                                  1, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(thumbnailShader->isVisible(), 10000);

        const QSizeF physicalViewport = thumbnailShader->property(
            "viewportSize").toSizeF();
        const qreal decodedWidth = thumbnailImage->implicitWidth();
        const qreal decodedHeight = thumbnailImage->implicitHeight();
        QVERIFY2(decodedWidth > 24 && decodedHeight > 24,
                 qPrintable(QStringLiteral(
                     "SVG was decoded at %1x%2 instead of the panel preview")
                         .arg(decodedWidth).arg(decodedHeight)));
        QVERIFY2(decodedWidth >= physicalViewport.width() - 2.0
                 && decodedHeight >= physicalViewport.height() - 2.0,
                 qPrintable(QStringLiteral(
                     "decoded SVG %1x%2 does not cover physical preview %3x%4")
                         .arg(decodedWidth).arg(decodedHeight)
                         .arg(physicalViewport.width())
                         .arg(physicalViewport.height())));

        runtime->shutdown();
    }

    void sparseExternalMetadataCompletionPublishesThumbnailWithoutViewportChange() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const QString templatePath = directory.filePath(
            QStringLiteral("template.png"));
        const QString delayedPath = directory.filePath(
            QStringLiteral("delayed.png"));
        QImage source(QSize(256, 256), QImage::Format_RGB32);
        source.fill(QColor(QStringLiteral("#4f7cac")));
        QVERIFY(source.save(templatePath));
        const QFileInfo templateInfo(templatePath);
        QVERIFY(templateInfo.size() > 0);
        QVERIFY(!QFileInfo::exists(delayedPath));

        constexpr int imageRow = 14;
        constexpr qint64 sourceVersion = 7'654'321'000'000;
        QVariantList catalog;
        catalog.reserve(imageRow + 1);
        for (int row = 0; row < imageRow; ++row) {
            catalog.append(QVariantMap{
                {QStringLiteral("entryId"),
                 QStringLiteral("sparse-folder-%1").arg(row)},
                {QStringLiteral("index"), row},
                {QStringLiteral("name"),
                 QStringLiteral("Folder %1").arg(row)},
                {QStringLiteral("isDir"), true},
                {QStringLiteral("isImage"), false},
                {QStringLiteral("selected"), false},
                {QStringLiteral("mtimeNs"), qint64(row + 1)},
                {QStringLiteral("size"), qint64(0)},
            });
        }
        catalog.append(QVariantMap{
            {QStringLiteral("entryId"), QStringLiteral("delayed-image")},
            {QStringLiteral("index"), imageRow},
            {QStringLiteral("name"), QStringLiteral("delayed.png")},
            {QStringLiteral("localPath"), delayedPath},
            {QStringLiteral("isDir"), false},
            {QStringLiteral("isImage"), true},
            {QStringLiteral("selected"), false},
            {QStringLiteral("mtimeNs"), sourceVersion},
            {QStringLiteral("size"), templateInfo.size()},
        });

        QQuickView view;
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        options.maxDecodeThreads = 1;
        auto *runtime = ZoinGallery::GalleryRuntime::install(
            view.engine(), options);
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(
            QStringLiteral("layout-sparse-delayed-metadata"));
        QVERIFY(session);
        QVERIFY(session->applyExternalCatalog(catalog, 1));

        auto *external = qobject_cast<ZoinGallery::ExternalCatalogModel *>(
            session->model());
        QVERIFY(external);
        auto *decodeManager = runtime->findChild<DecodeManager *>();
        QVERIFY(decodeManager);
        QObject *panel = createPanel(
            view, session, QStringLiteral("sparseMetadataSession"));
        QVERIFY(panel);
        auto *layout = panel->findChild<MasonryLayout *>(
            QStringLiteral("galleryMasonryLayout"));
        QVERIFY(layout);
        QTRY_COMPARE_WITH_TIMEOUT(layout->count(), imageRow + 1, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(layout->width() > 100 &&
                                 layout->height() > 100, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(
            layout->overscanIndexes().contains(imageRow), 5000);

        auto *image = session->model()
            ->data(session->model()->index(imageRow, 0),
                   FileListModel::ImageFileRole)
            .value<ImageFile *>();
        QVERIFY(image);

        // Let the missing-file metadata probe and GalleryPanel's one-time
        // callLater re-read both finish. This leaves the image in the active
        // viewport plan with no fullSize and therefore no decode request.
        QTRY_VERIFY_WITH_TIMEOUT(
            external->metadataSubmittedBatchCount() > 0, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(external->metadataPendingRequestCount(),
                                  qsizetype(0), 5000);
        // Also let the 120 ms initial width-settle decode timer expire; it is
        // an independent forced re-read and would mask the missing metadata
        // completion re-plan this test targets.
        QTest::qWait(200);
        QVERIFY(!image->fullSize().isValid());
        QVERIFY(image->imageIdUrl().isEmpty());

        const QVariantList visibleBefore = layout->visibleIndexes();
        const QVariantList overscanBefore = layout->overscanIndexes();
        const qreal contentYBefore = layout->contentY();

        // Materialize the source only after the automatic probe is known to
        // be complete, then inject the same completion a delayed decoder
        // would publish. The square dimensions deliberately match the
        // placeholder aspect ratio so rewrap keeps the viewport index sets
        // unchanged; metadata completion itself must re-plan the thumbnail.
        QVERIFY(QFile::copy(templatePath, delayedPath));
        ImageInfo info;
        info.path = delayedPath;
        info.lastModified = QDateTime::fromMSecsSinceEpoch(
            sourceVersion / 1'000'000, QTimeZone::UTC);
        info.fileSize = templateInfo.size();
        info.sourceVersionToken = sourceVersion;
        info.imageSize = source.size();
        info.orientation = ExifOrientation::Horizontal;
        info.requestNamespace = session->sessionId();
        decodeManager->imageInfoReady(info);

        QTRY_COMPARE_WITH_TIMEOUT(image->fullSize(), source.size(), 5000);
        QCOMPARE(layout->contentY(), contentYBefore);
        QCOMPARE(layout->visibleIndexes(), visibleBefore);
        QCOMPARE(layout->overscanIndexes(), overscanBefore);
        QTRY_VERIFY_WITH_TIMEOUT(!image->imageIdUrl().isEmpty(), 10000);
    }

    void metadataPlanningIsViewportScopedAndBounded() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString templatePath = directory.filePath(
            QStringLiteral("template.png"));
        QImage source(QSize(24, 18), QImage::Format_RGB32);
        source.fill(QColor(QStringLiteral("#6b7280")));
        QVERIFY(source.save(templatePath));

        constexpr int entryCount = 180;
        QVariantList catalog;
        catalog.reserve(entryCount);
        for (int index = 0; index < entryCount; ++index) {
            const QString path = directory.filePath(
                QStringLiteral("metadata-%1.png").arg(
                    index, 3, 10, QLatin1Char('0')));
            QVERIFY(QFile::copy(templatePath, path));
            catalog.append(catalogEntry(index, path));
        }

        QQuickView view;
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        options.maxDecodeThreads = 1;
        auto *runtime = ZoinGallery::GalleryRuntime::install(
            view.engine(), options);
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(
            QStringLiteral("layout-metadata-window"));
        QVERIFY(session);
        QVERIFY(session->applyExternalCatalog(catalog, 1));
        auto *decodeManager = runtime->findChild<DecodeManager *>();
        QVERIFY(decodeManager);
        int metadataResults = 0;
        int metadataFlushResults = 0;
        connect(decodeManager, &DecodeManager::imageInfoReady, this,
                [&](const ImageInfo &info) {
                    if (info.requestNamespace != session->sessionId()) {
                        return;
                    }
                    ++metadataResults;
                    if (info.isLast) {
                        ++metadataFlushResults;
                    }
                });
        auto *external = qobject_cast<ZoinGallery::ExternalCatalogModel *>(
            session->model());
        QVERIFY(external);

        // Construct the renderer directly in a fixed mode. In particular,
        // GallerySession::ensurePreviews must not enqueue a catalog scan before
        // the declarative presentation binding has reached MasonryLayout.
        QObject *panel = createPanel(
            view, session, QStringLiteral("metadataSession"),
            QStringLiteral("details"));
        QVERIFY(panel);
        auto *layout = panel->findChild<MasonryLayout *>(
            QStringLiteral("galleryMasonryLayout"));
        QVERIFY(layout);
        QTRY_COMPARE_WITH_TIMEOUT(layout->count(), entryCount, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(layout->presentationMode(),
                                  MasonryLayout::Details, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!layout->overscanIndexes().isEmpty(), 5000);

        const auto imageAt = [session](int row) {
            return session->model()
                ->data(session->model()->index(row, 0),
                       FileListModel::ImageFileRole)
                .value<ImageFile *>();
        };
        ImageFile *first = imageAt(0);
        ImageFile *last = imageAt(entryCount - 1);
        QVERIFY(first);
        QVERIFY(last);
        QTRY_VERIFY_WITH_TIMEOUT(first->fullSize().isValid(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!first->imageIdUrl().isEmpty(), 10000);
        QVERIFY2(!first->image().isNull() ||
                     !first->imageIdUrl().isEmpty(),
                 "Details did not publish its visible image thumbnail");
        QVERIFY2(!last->fullSize().isValid(),
                 "Details queued metadata outside visible + overscan");
        QVERIFY(external->metadataPeakPendingRequestCount() > 0);
        QVERIFY(external->metadataPeakPendingRequestCount() <=
                external->metadataRequestLimit());

        // Masonry needs catalog aspect ratios, but its background scan keeps
        // no more than a fixed admission window in DecodeManager while
        // visible rows retain priority.
        panel->setProperty("presentationMode", QStringLiteral("masonry"));
        QTRY_COMPARE_WITH_TIMEOUT(layout->presentationMode(),
                                  MasonryLayout::Masonry, 5000);
        layout->reReadAndDecodeThumbnails();
        QCoreApplication::processEvents();
        QVERIFY(external->metadataPendingRequestCount() > 0);

        // Leaving Masonry revokes that catalog-wide lease. Already admitted
        // work may finish, but a fixed renderer must not refill the queue or
        // probe the distant tail of the catalog.
        panel->setProperty("presentationMode", QStringLiteral("details"));
        QTRY_COMPARE_WITH_TIMEOUT(layout->presentationMode(),
                                  MasonryLayout::Details, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(external->metadataPendingRequestCount(),
                                  qsizetype(0), 10000);
        QVERIFY2(!last->fullSize().isValid(),
                 "Details kept the previous Masonry catalog scan alive");

        // Re-entering Masonry resumes the saved catalog cursor. Repeated
        // viewport/dataChanged plans keep renewing the cheap true marker, so
        // the scan cannot be paused by its own incremental results.
        panel->setProperty("presentationMode", QStringLiteral("masonry"));
        QTRY_COMPARE_WITH_TIMEOUT(layout->presentationMode(),
                                  MasonryLayout::Masonry, 5000);
        layout->reReadAndDecodeThumbnails();
        QTRY_VERIFY_WITH_TIMEOUT(last->fullSize().isValid(), 15000);
        QTRY_COMPARE_WITH_TIMEOUT(metadataResults, entryCount, 5000);
        QVERIFY(external->metadataPeakPendingRequestCount() <=
                external->metadataRequestLimit());
        // Low-watermark admission must produce chunky metadata batches. A
        // one-item refill would mark practically every result TimeToFlush and
        // repeatedly finalize/re-wrap the incremental Masonry row.
        QVERIFY2(metadataFlushResults <= 16,
                 qPrintable(QStringLiteral(
                     "metadata scan produced %1 flush batches for %2 rows")
                     .arg(metadataFlushResults).arg(entryCount)));
        QCOMPARE(quint64(metadataFlushResults),
                 external->metadataSubmittedBatchCount());
        QVERIFY(external->metadataSubmittedBatchCount() <= 16);
    }

    void separateExtensionsUseADedicatedRightAlignedField() {
        const QVariantList catalog{
            QVariantMap{
                {QStringLiteral("entryId"), QStringLiteral("archive")},
                {QStringLiteral("index"), 0},
                {QStringLiteral("name"), QStringLiteral("archive.tar.gz")},
                {QStringLiteral("isDir"), false},
                {QStringLiteral("isImage"), false},
                {QStringLiteral("size"), qint64(4096)},
                {QStringLiteral("displayBaseName"),
                 QStringLiteral("archive.tar")},
                {QStringLiteral("displayExtension"), QStringLiteral("gz")},
                {QStringLiteral("sizeText"), QStringLiteral("4 KiB")},
            },
            QVariantMap{
                {QStringLiteral("entryId"), QStringLiteral("photo")},
                {QStringLiteral("index"), 1},
                {QStringLiteral("name"), QStringLiteral("photo.jpeg")},
                {QStringLiteral("isDir"), false},
                {QStringLiteral("isImage"), false},
                {QStringLiteral("size"), qint64(8192)},
                {QStringLiteral("displayBaseName"), QStringLiteral("photo")},
                {QStringLiteral("displayExtension"),
                 QStringLiteral("jpeg")},
                {QStringLiteral("sizeText"), QStringLiteral("8 KiB")},
            },
        };

        QQuickView view;
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        auto *runtime = ZoinGallery::GalleryRuntime::install(
            view.engine(), options);
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(
            QStringLiteral("layout-separated-extensions"));
        QVERIFY(session);
        QVERIFY(session->applyExternalCatalog(catalog, 1));
        QObject *panel = createPanel(
            view, session, QStringLiteral("extensionSession"),
            QStringLiteral("details"));
        QVERIFY(panel);

        auto findItem = [panel](const QString &name) {
            return panel->findChild<QQuickItem *>(name);
        };
        QTRY_VERIFY_WITH_TIMEOUT(
            findItem(QStringLiteral("galleryBaseName-0")), 5000);
        QQuickItem *base0 = findItem(QStringLiteral("galleryBaseName-0"));
        QQuickItem *extension0 = findItem(
            QStringLiteral("galleryExtension-0"));
        QQuickItem *extension1 = findItem(
            QStringLiteral("galleryExtension-1"));
        QQuickItem *size0 = findItem(QStringLiteral("gallerySize-0"));
        QVERIFY(base0);
        QVERIFY(extension0);
        QVERIFY(extension1);
        QVERIFY(size0);

        // The standalone/default contract remains the combined filename.
        QCOMPARE(base0->property("text").toString(),
                 QStringLiteral("archive.tar.gz"));
        QVERIFY(!extension0->isVisible());

        panel->setProperty("separateFileExtensions", true);
        QTRY_COMPARE_WITH_TIMEOUT(base0->property("text").toString(),
                                  QStringLiteral("archive.tar"), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(extension0->isVisible(), 3000);
        QCOMPARE(extension0->property("text").toString(),
                 QStringLiteral("gz"));
        QCOMPARE(extension1->property("text").toString(),
                 QStringLiteral("jpeg"));
        QCOMPARE(extension0->property("horizontalAlignment").toInt(),
                 int(Qt::AlignLeft));
        QTRY_VERIFY_WITH_TIMEOUT(
            extension0->x() + extension0->width() <=
                size0->x() + 0.51,
            3000);
        QTRY_VERIFY_WITH_TIMEOUT(
            qAbs(extension0->x() + extension0->width() + 8
                 - size0->x()) <= 0.51,
            3000);
        QVERIFY(qAbs(extension0->x() - extension1->x()) <= 0.51);
        QVERIFY(qAbs(extension0->width() - extension1->width()) <= 0.51);

        // Columns uses the same split labels but no Size field; the extension
        // owns the trailing edge of each equal-width column cell.
        panel->setProperty("presentationMode", QStringLiteral("columns"));
        auto *layout = panel->findChild<MasonryLayout *>(
            QStringLiteral("galleryMasonryLayout"));
        QVERIFY(layout);
        QTRY_COMPARE_WITH_TIMEOUT(layout->presentationMode(),
                                  MasonryLayout::Columns, 3000);
        extension0 = findItem(QStringLiteral("galleryExtension-0"));
        size0 = findItem(QStringLiteral("gallerySize-0"));
        QVERIFY(extension0);
        QVERIFY(size0);
        QTRY_VERIFY_WITH_TIMEOUT(!size0->isVisible(), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(extension0->parentItem(), 3000);
        QVERIFY(qAbs(extension0->x() + extension0->width() -
                     extension0->parentItem()->width()) <= 0.51);
    }

    void detailsDelegateMatchesClassicFileListVisualContract() {
        QVariantList catalog;
        catalog.reserve(30);
        for (int index = 0; index < 30; ++index) {
            const bool folder = index == 1 || index == 2;
            QVariantMap entry{
                {QStringLiteral("entryId"),
                 QStringLiteral("classic-details-%1").arg(index)},
                {QStringLiteral("index"), index},
                {QStringLiteral("name"), folder
                     ? QStringLiteral("Pictures")
                     : QStringLiteral("archive.tar.gz")},
                {QStringLiteral("isDir"), folder},
                {QStringLiteral("isHidden"), index == 2},
                {QStringLiteral("isImage"), false},
                {QStringLiteral("selected"), index == 1},
                {QStringLiteral("size"), qint64(4096 + index)},
                {QStringLiteral("displayBaseName"), folder
                     ? QStringLiteral("Pictures")
                     : QStringLiteral("archive.tar")},
                {QStringLiteral("displayExtension"), folder
                     ? QString() : QStringLiteral("gz")},
                {QStringLiteral("sizeText"), folder
                     ? QStringLiteral("<DIR>") : QStringLiteral("4 KiB")},
                // f4 supplies a normal foreground for every catalog row.
                // Folder icon tint must still use its dedicated blue/white
                // state instead of inheriting this ordinary white text.
                {QStringLiteral("highlightStyle"), QVariantMap{
                     {QStringLiteral("normal"), QVariantMap{
                          {QStringLiteral("foreground"),
                           QStringLiteral("#e8edf2")},
                      }},
                 }},
            };
            catalog.append(entry);
        }

        QQuickView view;
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        auto *runtime = ZoinGallery::GalleryRuntime::install(
            view.engine(), options);
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(
            QStringLiteral("classic-details-visual-contract"));
        QVERIFY(session);
        QVERIFY(session->applyExternalCatalog(catalog, 1));
        session->setCurrentIndex(0);

        QObject *panel = createPanel(
            view, session, QStringLiteral("classicDetailsSession"),
            QStringLiteral("details"));
        QVERIFY(panel);
        panel->setProperty("density", 30.0);
        panel->setProperty("separateFileExtensions", true);
        panel->setProperty("theme", QVariantMap{
            {QStringLiteral("panelBackground"),
             QStringLiteral("transparent")},
            {QStringLiteral("text"), QStringLiteral("#e8edf2")},
            {QStringLiteral("mutedText"), QStringLiteral("#9aa7b5")},
            {QStringLiteral("cursorBackground"),
             QStringLiteral("#18456e")},
            {QStringLiteral("cursorBorder"),
             QStringLiteral("#1d5888")},
            {QStringLiteral("markedBackground"),
             QStringLiteral("#4f5037")},
            {QStringLiteral("markedText"), QStringLiteral("#ffd43b")},
            {QStringLiteral("directoryText"),
             QStringLiteral("#98d8ff")},
            {QStringLiteral("folderIcon"),
             QStringLiteral("#5ab2f1")},
            {QStringLiteral("separator"), QStringLiteral("#30363d")},
            {QStringLiteral("headerText"), QStringLiteral("#d7e0ea")},
            {QStringLiteral("controlHover"), QStringLiteral("#2a3745")},
        });
        panel->setProperty("metrics", QVariantMap{
            {QStringLiteral("detailsRowInset"), 8.0},
            {QStringLiteral("detailsRowSpacing"), 8.0},
            {QStringLiteral("detailsIconSlotSize"), 18.0},
            {QStringLiteral("detailsIconSize"), 16.0},
            {QStringLiteral("detailsNameFontPixelSize"), 13.0},
            {QStringLiteral("detailsSecondaryFontPixelSize"), 12.0},
            {QStringLiteral("detailsExtensionMinimumWidth"), 40.0},
            {QStringLiteral("detailsExtensionMaximumWidth"), 80.0},
            {QStringLiteral("detailsSizeColumnWidth"), 96.0},
            {QStringLiteral("detailsHeaderHeight"), 38.0},
            {QStringLiteral("detailsHeaderCellInset"), 8.0},
            {QStringLiteral("detailsHeaderFontPixelSize"), 12.0},
            {QStringLiteral("detailsSeparatorVerticalMargin"), 6.0},
            {QStringLiteral("detailsSeparatorWidth"), 1.0},
            {QStringLiteral("detailsScrollBarWidth"), 16.0},
        });
        panel->setProperty("columnSchema", QVariantList{
            QVariantMap{
                {QStringLiteral("id"), QStringLiteral("name")},
                {QStringLiteral("role"), QStringLiteral("name")},
                {QStringLiteral("title"), QStringLiteral("Name")},
                {QStringLiteral("width"), 50},
                {QStringLiteral("alignment"), QStringLiteral("left")},
                {QStringLiteral("sortMode"), QStringLiteral("name")},
                {QStringLiteral("sortable"), true},
            },
            QVariantMap{
                {QStringLiteral("id"), QStringLiteral("size")},
                {QStringLiteral("role"), QStringLiteral("size")},
                {QStringLiteral("title"), QStringLiteral("Size")},
                {QStringLiteral("width"), 14},
                {QStringLiteral("alignment"), QStringLiteral("right")},
                {QStringLiteral("sortMode"), QStringLiteral("size")},
                {QStringLiteral("sortable"), true},
            },
        });

        const auto findItem = [panel](const QString &name) {
            return panel->findChild<QQuickItem *>(name);
        };
        auto *layout = panel->findChild<MasonryLayout *>(
            QStringLiteral("galleryMasonryLayout"));
        QVERIFY(layout);
        QTRY_COMPARE_WITH_TIMEOUT(layout->presentationMode(),
                                  MasonryLayout::Details, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(layout->count(), 30, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(
            findItem(QStringLiteral("gallerySelectionSurface-1")), 3000);

        auto *panelItem = qobject_cast<QQuickItem *>(panel);
        auto *header = findItem(QStringLiteral("galleryDetailsHeader"));
        auto *headerCell0 = findVisualItem(
            panelItem, QStringLiteral("galleryDetailsHeaderCell-0"));
        auto *headerCell1 = findVisualItem(
            panelItem, QStringLiteral("galleryDetailsHeaderCell-1"));
        auto *headerText0 = findVisualItem(
            panelItem, QStringLiteral("galleryDetailsHeaderText-0"));
        auto *headerText1 = findVisualItem(
            panelItem, QStringLiteral("galleryDetailsHeaderText-1"));
        auto *headerSeparator = findVisualItem(
            panelItem, QStringLiteral("galleryDetailsHeaderSeparator-0"));
        auto *bottomSeparator = findItem(
            QStringLiteral("galleryDetailsHeaderBottomSeparator"));
        QVERIFY(panelItem);
        QVERIFY2(header, "missing Gallery Details header");
        QVERIFY2(headerCell0, "missing Gallery Details name header cell");
        QVERIFY2(headerCell1, "missing Gallery Details size header cell");
        QVERIFY2(headerText0, "missing Gallery Details name header text");
        QVERIFY2(headerText1, "missing Gallery Details size header text");
        QVERIFY2(headerSeparator, "missing Gallery Details column separator");
        QVERIFY2(bottomSeparator, "missing Gallery Details bottom separator");
        QCOMPARE(header->height(), 38.0);
        QCOMPARE(header->width(), 640.0);
        QCOMPARE(headerCell0->x(), 0.0);
        QCOMPARE(headerCell0->width(), 500.0);
        QCOMPARE(headerCell1->x(), 500.0);
        QCOMPARE(headerCell1->width(), 140.0);
        QCOMPARE(headerText0->x(), 8.0);
        QCOMPARE(headerText1->property("horizontalAlignment").toInt(),
                 int(Qt::AlignRight));
        QCOMPARE(headerSeparator->width(), 1.0);
        QCOMPARE(headerSeparator->height(), 26.0);
        QCOMPARE(bottomSeparator->height(), 1.0);
        QCOMPARE(headerText0->property("font").value<QFont>().pixelSize(),
                 12);

        QCOMPARE(layout->x(), 0.0);
        QCOMPARE(layout->y(), 38.0);
        QCOMPARE(layout->width(), 640.0);

        auto *cursorSurface = findItem(
            QStringLiteral("gallerySelectionSurface-0"));
        auto *markedSurface = findItem(
            QStringLiteral("gallerySelectionSurface-1"));
        auto *base0 = findItem(QStringLiteral("galleryBaseName-0"));
        auto *extension0 = findItem(QStringLiteral("galleryExtension-0"));
        auto *size0 = findItem(QStringLiteral("gallerySize-0"));
        auto *fileIcon = findItem(QStringLiteral("galleryFallbackIcon-0"));
        auto *folderBase = findItem(QStringLiteral("galleryBaseName-1"));
        auto *folderIcon = findItem(QStringLiteral("galleryFallbackIcon-1"));
        auto *plainFolderBase = findItem(
            QStringLiteral("galleryBaseName-2"));
        auto *plainFolderIcon = findItem(
            QStringLiteral("galleryFallbackIcon-2"));
        auto *hiddenSurface = findItem(
            QStringLiteral("gallerySelectionSurface-2"));
        auto *hiddenDetailsRow = findItem(
            QStringLiteral("galleryDetailsRow-2"));
        auto *folderExtension = findItem(
            QStringLiteral("galleryExtension-1"));
        auto *folderSize = findItem(QStringLiteral("gallerySize-1"));
        auto *scrollBar = findItem(QStringLiteral("galleryPanelScrollBar"));
        QVERIFY(cursorSurface && markedSurface && base0 && extension0
                && size0 && fileIcon && folderBase && folderIcon
                && plainFolderBase && plainFolderIcon
                && hiddenSurface && hiddenDetailsRow
                && folderExtension && folderSize && scrollBar);
        QCOMPARE(cursorSurface->width(), 640.0);
        QCOMPARE(cursorSurface->height(), 30.0);
        QCOMPARE(cursorSurface->property("color").value<QColor>(),
                 QColor(QStringLiteral("#18456e")));
        QCOMPARE(cursorSurface->property("visualBorderWidth").toReal(), 1.0);
        QCOMPARE(cursorSurface->property("visualBorderColor").value<QColor>(),
                 QColor(QStringLiteral("#1d5888")));

        const QQuickItem *row0 = cursorSurface->parentItem();
        QVERIFY(row0);
        const QQuickItem *hiddenRow = hiddenSurface->parentItem();
        QVERIFY(hiddenRow);
        QCOMPARE(hiddenRow->opacity(), 1.0);
        QCOMPARE(hiddenSurface->opacity(), 1.0);
        QCOMPARE(hiddenDetailsRow->opacity(), 0.5);
        QCOMPARE(row0->opacity(), 1.0);
        QTRY_VERIFY_WITH_TIMEOUT(
            qAbs(base0->mapToItem(row0, QPointF()).x() - 34.0) < 0.01,
            3000);
        const QPointF basePosition = base0->mapToItem(row0, QPointF());
        const QPointF iconPosition = fileIcon->mapToItem(row0, QPointF());
        QCOMPARE(basePosition.x(), 34.0);
        QCOMPARE(iconPosition.x(), 9.0);
        QCOMPARE(fileIcon->width(), 16.0);
        QVERIFY(QString::fromLatin1(base0->metaObject()->className())
                    .contains(QStringLiteral("QQuickText")));
        QCOMPARE(base0->height(), base0->implicitHeight());
        QCOMPARE(base0->property("font").value<QFont>().pixelSize(), 13);
        QCOMPARE(extension0->property("font").value<QFont>().pixelSize(),
                 12);
        QCOMPARE(size0->property("font").value<QFont>().pixelSize(), 12);
        QCOMPARE(extension0->property("text").toString(),
                 QStringLiteral("gz"));
        QCOMPARE(extension0->property("horizontalAlignment").toInt(),
                 int(Qt::AlignLeft));
        QCOMPARE(extension0->width(), 40.0);
        QCOMPARE(size0->width(), 96.0);
        QCOMPARE(size0->mapToItem(row0, QPointF()).x() + size0->width(),
                 632.0);
        QCOMPARE(base0->property("color").value<QColor>(),
                 QColor(QStringLiteral("#e8edf2")));
        QCOMPARE(folderBase->property("color").value<QColor>(),
                 QColor(QStringLiteral("#ffd43b")));
        QVERIFY(!folderExtension->isVisible());
        const QQuickItem *row1 = markedSurface->parentItem();
        QVERIFY(row1);
        const QPointF folderBasePosition = folderBase->mapToItem(
            row1, QPointF());
        const QPointF folderSizePosition = folderSize->mapToItem(
            row1, QPointF());
        QCOMPARE(folderBasePosition.x(), 34.0);
        QCOMPARE(folderBase->width(), folderSizePosition.x() - 8.0
                                      - folderBasePosition.x());
        QCOMPARE(fileIcon->property("effectiveIconColor").value<QColor>(),
                 QColor(QStringLiteral("#9aa7b5")));
        QCOMPARE(folderIcon->property("effectiveIconColor").value<QColor>(),
                 QColor(QStringLiteral("#ffd43b")));
        QCOMPARE(plainFolderIcon->property("effectiveIconColor").value<QColor>(),
                 QColor(QStringLiteral("#5ab2f1")));
        QCOMPARE(plainFolderBase->property("color").value<QColor>(),
                 QColor(QStringLiteral("#e8edf2")));
        QCOMPARE(markedSurface->property("color").value<QColor>(),
                 QColor(Qt::transparent));
        QCOMPARE(markedSurface->property("visualBorderWidth").toReal(), 0.0);

        // Persistent selection outranks the transient cursor. Moving the
        // cursor onto an already-selected folder must keep both its name and
        // Lucide folder icon yellow instead of applying the white cursor
        // foreground.
        session->setCurrentIndex(1);
        QTRY_COMPARE_WITH_TIMEOUT(
            folderBase->property("color").value<QColor>(),
            QColor(QStringLiteral("#ffd43b")), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(
            folderIcon->property("effectiveIconColor").value<QColor>(),
            QColor(QStringLiteral("#ffd43b")), 3000);

        // Folder labels stay white. Their Lucide icon is blue normally,
        // becomes white under the cursor, and persistent selection remains
        // the strongest state by promoting both label and icon to yellow.
        session->setCurrentIndex(2);
        QTRY_COMPARE_WITH_TIMEOUT(
            plainFolderIcon->property("effectiveIconColor").value<QColor>(),
            QColor(QStringLiteral("#e8edf2")), 3000);
        QCOMPARE(plainFolderBase->property("color").value<QColor>(),
                 QColor(QStringLiteral("#e8edf2")));
        QTRY_VERIFY_WITH_TIMEOUT(scrollBar->isVisible(), 3000);
        QCOMPARE(scrollBar->width(), 16.0);
        QCOMPARE(scrollBar->x(), 632.0);
        // The host reserves an 8px trailing panel inset. The 16px overlay is
        // anchored into that lane, while Details keeps its full 640px row.
        QCOMPARE(layout->width(), panelItem->width());
    }

    void compactModesUseMarkedTextWithoutSelectionBorder() {
        for (const QString &mode : {QStringLiteral("columns"),
                                    QStringLiteral("details")}) {
            QQuickView view;
            ZoinGallery::RuntimeOptions options;
            options.persistentCache = false;
            auto *runtime = ZoinGallery::GalleryRuntime::install(
                view.engine(), options);
            QVERIFY(runtime);
            auto *session = runtime->createExternalSession(
                QStringLiteral("compact-text-selection-%1").arg(mode));
            QVERIFY(session);
            QVERIFY(session->applyExternalCatalog(plainCatalog(8), 1));
            QVERIFY(session->applyExternalState(
                QStringLiteral("layout-entry-0"), 0,
                QStringList{QStringLiteral("layout-entry-1")}, 1));

            QObject *panel = createPanel(
                view, session,
                QStringLiteral("compactTextSelectionSession"), mode);
            QVERIFY(panel);
            panel->setProperty("theme", QVariantMap{
                {QStringLiteral("markedText"),
                 QStringLiteral("#ffd43b")},
                {QStringLiteral("selection"),
                 QStringLiteral("#ffd43b")},
            });
            auto *layout = panel->findChild<MasonryLayout *>(
                QStringLiteral("galleryMasonryLayout"));
            QVERIFY(layout);
            QTRY_COMPARE_WITH_TIMEOUT(layout->count(), 8, 3000);

            auto *surface = panel->findChild<QQuickItem *>(
                QStringLiteral("gallerySelectionSurface-1"));
            auto *label = panel->findChild<QQuickItem *>(
                QStringLiteral("galleryBaseName-1"));
            QTRY_VERIFY_WITH_TIMEOUT(surface && label, 3000);
            QCOMPARE(surface->property("color").value<QColor>(),
                     QColor(Qt::transparent));
            QCOMPARE(surface->property("visualBorderWidth").toReal(), 0.0);
            QCOMPARE(label->property("color").value<QColor>(),
                     QColor(QStringLiteral("#ffd43b")));

            session->setCurrentIndex(1);
            QTRY_COMPARE_WITH_TIMEOUT(
                label->property("color").value<QColor>(),
                QColor(QStringLiteral("#ffd43b")), 3000);
            QCOMPARE(surface->property("visualBorderWidth").toReal(), 1.0);
            const QColor expectedCursorBorder = mode == QStringLiteral("details")
                ? panel->property("cursorBorderColor").value<QColor>()
                : panel->property("cursorColor").value<QColor>().lighter(135);
            QCOMPARE(surface->property("visualBorderColor").value<QColor>(),
                     expectedCursorBorder);
            QVERIFY(surface->property("visualBorderColor").value<QColor>()
                    != QColor(QStringLiteral("#ffd43b")));
        }
    }

    void columnsAndDetailsUseIdenticalCompactLucideIcons() {
        for (const QString &mode : {QStringLiteral("columns"),
                                    QStringLiteral("details")}) {
            QQuickView view;
            view.engine()->addImageProvider(
                QStringLiteral("compact-icons"),
                new CompactIconProvider);
            ZoinGallery::RuntimeOptions options;
            options.persistentCache = false;
            auto *runtime = ZoinGallery::GalleryRuntime::install(
                view.engine(), options);
            QVERIFY(runtime);
            auto *session = runtime->createExternalSession(
                QStringLiteral("compact-lucide-%1").arg(mode));
            QVERIFY(session);
            QVERIFY(session->applyExternalCatalog(plainCatalog(4), 1));
            QVERIFY(session->applyExternalAppearance({QVariantMap{
                {QStringLiteral("entryId"),
                 QStringLiteral("layout-entry-0")},
                {QStringLiteral("highlightStyle"), QVariantMap{
                     {QStringLiteral("icon"),
                      QStringLiteral(
                          "qrc:/F4QtHost/icons/lucide/folder.svg")},
                 }},
            }}, 1));

            QObject *panel = createPanel(
                view, session, QStringLiteral("compactLucideSession"), mode);
            QVERIFY(panel);
            auto *layout = panel->findChild<MasonryLayout *>(
                QStringLiteral("galleryMasonryLayout"));
            QVERIFY(layout);
            QTRY_COMPARE_WITH_TIMEOUT(layout->count(), 4, 3000);
            auto *icon = panel->findChild<QQuickItem *>(
                QStringLiteral("galleryFallbackIcon-0"));
            QTRY_VERIFY_WITH_TIMEOUT(icon && icon->isVisible(), 3000);
            QCOMPARE(icon->width(), 16.0);
            QCOMPARE(icon->height(), 16.0);
            QCOMPARE(icon->opacity(), 1.0);
            if (mode == QStringLiteral("columns")) {
                QCOMPARE(icon->property("source").toUrl(),
                         QUrl(QStringLiteral(
                             "qrc:/F4QtHost/icons/lucide/folder.svg")));
            }

            // Large Lucide model routes are provider-rasterized for the
            // image-centric Gallery modes. Compact modes must request a new
            // frame for their actual 16-DIP slot; scaling the 128-DIP texture
            // down would discard one-physical-pixel strokes at fractional
            // DPRs when nearest-neighbour sampling is active.
            panel->setProperty("devicePixelRatio", 1.75);
            QVERIFY(session->applyExternalAppearance({QVariantMap{
                {QStringLiteral("entryId"),
                 QStringLiteral("layout-entry-0")},
                {QStringLiteral("highlightStyle"), QVariantMap{
                     {QStringLiteral("icon"), QStringLiteral(
                         "image://compact-icons/lucide/ZmlsZQ"
                         "?size=128&dpr=2&revision=1")},
                 }},
            }}, 2));
            QTRY_COMPARE_WITH_TIMEOUT(
                icon->property("source").toUrl().scheme(),
                QStringLiteral("image"), 3000);
            const QUrlQuery compactQuery(icon->property("source").toUrl());
            QCOMPARE(compactQuery.queryItemValue(QStringLiteral("size")),
                     QStringLiteral("16"));
            QCOMPARE(compactQuery.queryItemValue(QStringLiteral("dpr")),
                     QStringLiteral("1.75"));
            QCOMPARE(QColor(compactQuery.queryItemValue(
                         QStringLiteral("color"))),
                     icon->property("effectiveIconColor").value<QColor>());
        }
    }

    void nonLucideIconsPreserveTheirSourceColors() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString iconPath = directory.filePath(
            QStringLiteral("native-colour-icon.png"));
        QImage icon(20, 12, QImage::Format_ARGB32_Premultiplied);
        icon.fill(QColor(QStringLiteral("#f12a53")));
        for (int y = 0; y < icon.height(); ++y) {
            for (int x = icon.width() / 2; x < icon.width(); ++x)
                icon.setPixelColor(x, y, QColor(QStringLiteral("#19c97a")));
        }
        QVERIFY(icon.save(iconPath));

        QQuickView view;
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        auto *runtime = ZoinGallery::GalleryRuntime::install(
            view.engine(), options);
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(
            QStringLiteral("source-colour-icon-contract"));
        QVERIFY(session);
        QVERIFY(session->applyExternalCatalog(plainCatalog(2), 1));
        const QVariantMap sourceColourStyle{
            {QStringLiteral("icon"), QUrl::fromLocalFile(iconPath).toString()},
        };
        const QVariantMap lucideStyle{
            {QStringLiteral("icon"),
             QStringLiteral("qrc:/F4QtHost/icons/lucide/file.svg")},
        };
        QVERIFY(session->applyExternalAppearance({
            QVariantMap{
                {QStringLiteral("entryId"),
                 QStringLiteral("layout-entry-0")},
                {QStringLiteral("highlightStyle"), sourceColourStyle},
            },
            QVariantMap{
                {QStringLiteral("entryId"),
                 QStringLiteral("layout-entry-1")},
                {QStringLiteral("highlightStyle"), lucideStyle},
            },
        }, 1));

        QObject *panel = createPanel(
            view, session, QStringLiteral("sourceColourIconSession"),
            QStringLiteral("grid"));
        QVERIFY(panel);
        auto findItem = [panel](const QString &name) {
            return panel->findChild<QQuickItem *>(name);
        };

        QTRY_VERIFY_WITH_TIMEOUT(
            findItem(QStringLiteral("gallerySourceColorIcon-0")), 3000);
        auto *sourceColourIcon = findItem(
            QStringLiteral("gallerySourceColorIcon-0"));
        auto *sourceMask = findItem(QStringLiteral("galleryFallbackIcon-0"));
        auto *lucideMask = findItem(QStringLiteral("galleryFallbackIcon-1"));
        auto *lucideSourceColour = findItem(
            QStringLiteral("gallerySourceColorIcon-1"));
        QVERIFY(sourceColourIcon && sourceMask && lucideMask
                && lucideSourceColour);
        QTRY_VERIFY_WITH_TIMEOUT(sourceColourIcon->isVisible(), 3000);
        QVERIFY(!sourceMask->isVisible());
        QVERIFY(lucideMask->isVisible());
        QVERIFY(!lucideSourceColour->isVisible());
        QCOMPARE(sourceColourIcon->opacity(), 1.0);
        QCOMPARE(sourceColourIcon->property("fillMode").toInt(),
                 1); // Image.PreserveAspectFit
        QCOMPARE(sourceColourIcon->property("asynchronous").toBool(), true);
        QCOMPARE(sourceColourIcon->metaObject()->indexOfProperty("color"),
                 -1);
        auto *emptyThumbnail = findItem(
            QStringLiteral("galleryThumbnail-0"));
        auto *previewBackdrop = findItem(
            QStringLiteral("galleryThumbnailBackdrop-0"));
        QVERIFY(emptyThumbnail && previewBackdrop);
        QVERIFY(!emptyThumbnail->isVisible());
        QVERIFY(previewBackdrop->isVisible());
        QCOMPARE(previewBackdrop->parentItem(),
                 sourceColourIcon->parentItem());
        const auto previewChildren = previewBackdrop->parentItem()->childItems();
        QVERIFY(previewChildren.indexOf(previewBackdrop)
                < previewChildren.indexOf(sourceColourIcon));

        // The empty thumbnail layer used to remain visible because its `url`
        // property was compared directly with a string. Its black loading
        // backdrop sat above full-colour icons and multiplied every RGB
        // channel by 0.8 (0.7 with the dark system colour scheme). The card
        // remains visible now, but its earlier sibling position keeps the
        // opaque source-colour pixels untouched.
        QTest::qWait(100);
        const QImage sourceColourFrame = view.grabWindow();
        QVERIFY(!sourceColourFrame.isNull());
        const QPointF sceneSample = sourceColourIcon->mapToScene(QPointF(
            sourceColourIcon->width() * 0.25,
            sourceColourIcon->height() * 0.5));
        const qreal frameDpr = qreal(sourceColourFrame.width()) / view.width();
        const QPoint frameSample(qRound(sceneSample.x() * frameDpr),
                                 qRound(sceneSample.y() * frameDpr));
        QVERIFY(sourceColourFrame.rect().contains(frameSample));
        const QColor renderedSourceColour = sourceColourFrame.pixelColor(
            frameSample);
        QVERIFY2(qAbs(renderedSourceColour.red() - 241) <= 1
                 && qAbs(renderedSourceColour.green() - 42) <= 1
                 && qAbs(renderedSourceColour.blue() - 83) <= 1,
                 qPrintable(renderedSourceColour.name(QColor::HexArgb)));

        auto *sourceColourBrick = sourceColourIcon->parentItem()
            ? sourceColourIcon->parentItem()->parentItem() : nullptr;
        QVERIFY(sourceColourBrick);
        QVariant sizedRoute;
        QVERIFY(QMetaObject::invokeMethod(
            sourceColourBrick, "sourceColorIconAtSize",
            Q_RETURN_ARG(QVariant, sizedRoute),
            Q_ARG(QVariant, QVariant(QStringLiteral(
                "image://f4-icons/file/LQ?size=128&dpr=2&revision=1"))),
            Q_ARG(QVariant, QVariant(16.0)),
            Q_ARG(QVariant, QVariant())));
        const QUrlQuery sizedQuery(QUrl(sizedRoute.toString()));
        QCOMPARE(sizedQuery.queryItemValue(QStringLiteral("size")),
                 QStringLiteral("16"));
        QCOMPARE(sizedQuery.queryItemValue(QStringLiteral("dpr")),
                 QStringLiteral("1"));
        QVERIFY(!sizedQuery.hasQueryItem(QStringLiteral("color")));

        panel->setProperty("presentationMode", QStringLiteral("details"));
        QTRY_VERIFY_WITH_TIMEOUT(
            findItem(QStringLiteral("gallerySourceColorIcon-0"))->isVisible(),
            3000);
        sourceColourIcon = findItem(
            QStringLiteral("gallerySourceColorIcon-0"));
        sourceMask = findItem(QStringLiteral("galleryFallbackIcon-0"));
        QVERIFY(sourceColourIcon && sourceMask);
        QVERIFY(sourceColourIcon->isVisible());
        QVERIFY(!sourceMask->isVisible());
        QCOMPARE(sourceColourIcon->opacity(), 1.0);
        QCOMPARE(sourceColourIcon->property("asynchronous").toBool(), true);

        runtime->shutdown();
    }

    void fractionalDetailsDensityPreservesRowPhaseAndContentExtent() {
        QQuickView view;
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        auto *runtime = ZoinGallery::GalleryRuntime::install(
            view.engine(), options);
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(
            QStringLiteral("fractional-details-density"));
        QVERIFY(session);

        constexpr int entryCount = 37;
        constexpr qreal rowExtent = 24.2;
        QVERIFY(session->applyExternalCatalog(plainCatalog(entryCount), 1));

        QObject *panel = createPanel(
            view, session, QStringLiteral("fractionalDetailsSession"),
            QStringLiteral("details"));
        QVERIFY(panel);
        panel->setProperty("showDetailsHeader", false);
        panel->setProperty("density", rowExtent);

        auto *layout = panel->findChild<MasonryLayout *>(
            QStringLiteral("galleryMasonryLayout"));
        QVERIFY(layout);
        QTRY_COMPARE_WITH_TIMEOUT(layout->presentationMode(),
                                  MasonryLayout::Details, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(layout->count(), entryCount, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(
            qAbs(layout->density() - rowExtent) < 0.0001, 3000);
        const qreal usableViewportHeight = layout->height()
            - layout->paddingTop() - layout->paddingBottom();
        const int completeVisibleRows = qMax(
            1, int(std::floor(usableViewportHeight / rowExtent
                              + 0.000000001)));
        const qreal trailingViewportRemainder = qMax<qreal>(
            0, usableViewportHeight - completeVisibleRows * rowExtent);
        const qreal expectedContentHeight = layout->paddingTop()
            + entryCount * rowExtent + trailingViewportRemainder
            + layout->paddingBottom();
        QTRY_VERIFY_WITH_TIMEOUT(
            qAbs(layout->contentHeight() - expectedContentHeight) < 0.0001,
            3000);

        // Check both the strategy geometry and the instantiated QQuickItem.
        // The latter catches a regression where BrickItem::setGeometry()
        // rounded every row even though indexGeometry() remained fractional.
        const QList<int> visibleRows{0, 1, 4, 5, 10};
        for (const int index : visibleRows) {
            const QRectF geometry = layout->indexGeometry(index);
            QVERIFY2(qAbs(geometry.y() - index * rowExtent) < 0.0001,
                     qPrintable(QStringLiteral(
                         "row %1 strategy y=%2 expected=%3")
                         .arg(index).arg(geometry.y())
                         .arg(index * rowExtent)));
            QVERIFY(qAbs(geometry.height() - rowExtent) < 0.0001);

            QQuickItem *surface = nullptr;
            QTRY_VERIFY_WITH_TIMEOUT(
                (surface = panel->findChild<QQuickItem *>(
                    QStringLiteral("gallerySelectionSurface-%1")
                        .arg(index))),
                3000);
            QQuickItem *brick = surface->parentItem();
            QVERIFY(brick);
            QTRY_VERIFY2_WITH_TIMEOUT(
                qAbs(brick->y() - index * rowExtent) < 0.0001,
                qPrintable(QStringLiteral(
                    "row %1 delegate y=%2 expected=%3")
                    .arg(index).arg(brick->y())
                    .arg(index * rowExtent)),
                3000);
            QTRY_VERIFY2_WITH_TIMEOUT(
                qAbs(brick->height() - rowExtent) < 0.0001,
                qPrintable(QStringLiteral(
                    "row %1 delegate height=%2 expected=%3")
                    .arg(index).arg(brick->height()).arg(rowExtent)),
                3000);
        }
    }

    void masonryCatalogResetRetainsSlotsAndBoundsMaterialization() {
        QQuickView view;
        ZoinGallery::RuntimeOptions runtimeOptions;
        runtimeOptions.persistentCache = false;
        runtimeOptions.maxDecodeThreads = 2;
        auto *runtime = ZoinGallery::GalleryRuntime::install(
            view.engine(), runtimeOptions);
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(
            QStringLiteral("masonry-reset-budget"));
        QVERIFY(session);

        const QVariantList warmCatalog = prefixedCatalog(
            QStringLiteral("warm"), 32);
        const QVariantList smallCatalog = prefixedCatalog(
            QStringLiteral("small"), 14);
        const QVariantList largeCatalog = prefixedCatalog(
            QStringLiteral("large"), 447);
        const auto applyCatalog = [&](const QVariantList &catalog,
                                      qulonglong revision,
                                      const QString &path) {
            return session->applyExternalCatalog(
                catalog, revision,
                {{QStringLiteral("currentPath"), path},
                 {QStringLiteral("metadataDeferred"), true}});
        };
        QVERIFY(applyCatalog(warmCatalog, 1,
                             QStringLiteral("D:/synthetic/warm")));

        QObject *panel = createPanel(
            view, session, QStringLiteral("masonryResetBudgetSession"),
            QStringLiteral("masonry"));
        QVERIFY(panel);
        auto *layout = panel->findChild<MasonryLayout *>(
            QStringLiteral("galleryMasonryLayout"));
        auto *model = qobject_cast<ZoinGallery::ExternalCatalogModel *>(
            session->model());
        QVERIFY(layout && model);
        QTRY_COMPARE_WITH_TIMEOUT(layout->count(), 32, 3000);

        QQuickItem *firstSurface = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT(
            (firstSurface = panel->findChild<QQuickItem *>(
                QStringLiteral("gallerySelectionSurface-0"))), 3000);
        QPointer<BrickItem> firstSlot = qobject_cast<BrickItem *>(
            firstSurface->parentItem());
        QVERIFY(firstSlot);
        QPointer<ImageFile> warmImage = firstSlot->property("model")
                                            .value<ImageFile *>();
        QVERIFY(warmImage);

        bool resetObserved = false;
        bool oldImageAliveDuringReset = false;
        connect(model, &QAbstractItemModel::modelReset, this, [&]() {
            resetObserved = true;
            oldImageAliveDuringReset = !warmImage.isNull();
        });

        QSignalSpy replacementFrameSpy(&view, &QQuickWindow::frameSwapped);
        QElapsedTimer timer;
        timer.start();
        QVERIFY(applyCatalog(largeCatalog, 2,
                             QStringLiteral("D:/synthetic/large")));
        const qint64 firstResetNs = timer.nsecsElapsed();
        QVERIFY2(firstResetNs < 20'000'000,
                 qPrintable(QStringLiteral(
                     "synchronous first Masonry reset took %1 ms")
                     .arg(firstResetNs / 1'000'000.0, 0, 'f', 3)));
        QVERIFY(resetObserved);
        QVERIFY(oldImageAliveDuringReset);
        QVERIFY(warmImage);
        QCOMPARE(layout->count(), 447);

        // Snapshot rows are installed synchronously inside modelReset so the
        // threaded renderer can consume them at the nearest sync cutoff. The
        // slot is already current and actionable when the catalog call
        // returns, while its heavyweight QObject facade is still absent.
        QVERIFY(firstSlot->isVisible());
        QCOMPARE(firstSlot->visualRow()
                     .value(QStringLiteral("entryId")).toString(),
                 QStringLiteral("large-entry-0"));
        QCOMPARE(firstSlot->property("entryId").toString(),
                 QStringLiteral("large-entry-0"));
        QVERIFY(!firstSlot->property("model").value<ImageFile *>());
        const QRectF firstGeometry = layout->indexGeometry(0);
        QVERIFY(firstGeometry.isValid());
        QCOMPARE(layout->itemAt(firstGeometry.center().x(),
                                firstGeometry.center().y()),
                 static_cast<QQuickItem *>(firstSlot.data()));

        // The synchronous facade is actionable before any render callback,
        // while the stale/heavy QObject pointer is guaranteed absent.
        auto *panelItem = qobject_cast<QQuickItem *>(panel);
        QVERIFY(panelItem);
        panelItem->forceActiveFocus();
        view.requestActivate();
        QSignalSpy selectionSpy(
            panel, SIGNAL(selectionRequested(QString,QVariant)));
        QSignalSpy openSpy(
            panel, SIGNAL(openRequested(QString,int,bool,bool)));
        QVERIFY(selectionSpy.isValid());
        QVERIFY(openSpy.isValid());
        QVERIFY(QMetaObject::invokeMethod(
            panel, "handlePointerPress", Qt::DirectConnection,
            Q_ARG(QVariant, QVariant(0)),
            Q_ARG(QVariant, QVariant::fromValue(int(Qt::RightButton))),
            Q_ARG(QVariant, QVariant::fromValue(int(Qt::NoModifier)))));
        QCOMPARE(selectionSpy.size(), 1);
        QCOMPARE(selectionSpy.constFirst().at(0).toString(),
                 QStringLiteral("toggle"));
        QCOMPARE(selectionSpy.constFirst().at(1).toList(),
                 QVariantList{QStringLiteral("large-entry-0")});
        QVERIFY(!firstSlot->property("model").value<ImageFile *>());
        QKeyEvent enterPress(QEvent::KeyPress, Qt::Key_Return,
                             Qt::NoModifier);
        QCoreApplication::sendEvent(&view, &enterPress);
        QVERIFY(enterPress.isAccepted());
        QCOMPARE(openSpy.size(), 1);
        QCOMPARE(openSpy.constFirst().at(0).toString(),
                 QStringLiteral("large-entry-0"));
        QCOMPARE(openSpy.constFirst().at(1).toInt(), 0);
        QCOMPARE(openSpy.constFirst().at(2).toBool(), true);

        QElapsedTimer firstFrameTimer;
        firstFrameTimer.start();
        view.update();
        // Return from the swap signal before the queued facade-materializer
        // can run. This is the exact catalog frame presented to the user.
        QVERIFY(replacementFrameSpy.wait(3000));
        const qint64 stagedVisualNs = firstFrameTimer.nsecsElapsed();
        const qint64 applyToPaintedFrameNs = timer.nsecsElapsed();
        QVERIFY2(stagedVisualNs < 33'000'000,
                 qPrintable(QStringLiteral(
                     "snapshot-to-painted catalog frame took %1 ms")
                     .arg(stagedVisualNs / 1'000'000.0, 0, 'f', 3)));
        QVERIFY2(applyToPaintedFrameNs < 33'000'000,
                 qPrintable(QStringLiteral(
                     "apply-to-painted catalog frame took %1 ms")
                     .arg(applyToPaintedFrameNs / 1'000'000.0,
                          0, 'f', 3)));

        QQuickItem *newFirstSurface = panel->findChild<QQuickItem *>(
            QStringLiteral("gallerySelectionSurface-0"));
        QVERIFY(newFirstSurface);
        QCOMPARE(newFirstSurface->parentItem(),
                 static_cast<QQuickItem *>(firstSlot.data()));
        QVERIFY(firstSlot->isVisible());

        // Every row which can contribute to the first frame is complete and
        // current without constructing or binding an ImageFile QObject.
        const QVariantList firstFrameRows = layout->visibleIndexes();
        QVERIFY(!firstFrameRows.isEmpty());
        for (const QVariant &rowValue : firstFrameRows) {
            const int row = rowValue.toInt();
            QQuickItem *surface = panel->findChild<QQuickItem *>(
                QStringLiteral("gallerySelectionSurface-%1").arg(row));
            QVERIFY2(surface,
                     qPrintable(QStringLiteral(
                         "first frame has no delegate for visible row %1")
                         .arg(row)));
            auto *slot = qobject_cast<BrickItem *>(surface->parentItem());
            QVERIFY(slot);
            const QVariantMap visual = slot->visualRow();
            const bool image = row % 4 == 0;
            const QString extension = image
                ? QStringLiteral("png") : QStringLiteral("txt");
            const QString expectedName = QStringLiteral("large-%1.%2")
                                             .arg(row).arg(extension);
            QVERIFY(visual.value(QStringLiteral("valid")).toBool());
            QCOMPARE(visual.value(QStringLiteral("entryId")).toString(),
                     QStringLiteral("large-entry-%1").arg(row));
            QCOMPARE(slot->property("entryId").toString(),
                     QStringLiteral("large-entry-%1").arg(row));
            QCOMPARE(visual.value(QStringLiteral("sourceIndex")).toInt(),
                     row);
            QCOMPARE(visual.value(QStringLiteral("text")).toString(),
                     expectedName);
            QCOMPARE(visual.value(QStringLiteral("isFolder")).toBool(),
                     false);
            QCOMPARE(visual.value(QStringLiteral("isImage")).toBool(),
                     image);
            QCOMPARE(visual.value(QStringLiteral("isSelected")).toBool(),
                     false);
            QVERIFY(!visual.value(QStringLiteral("iconPath"))
                         .toString().isEmpty());
            const QVariantMap fields = visual.value(
                QStringLiteral("displayFields")).toMap();
            QCOMPARE(fields.value(QStringLiteral("displayBaseName"))
                         .toString(),
                     QStringLiteral("large-%1").arg(row));
            QCOMPARE(fields.value(QStringLiteral("displayExtension"))
                         .toString(), extension);
            QObject *label = panel->findChild<QObject *>(
                QStringLiteral("galleryMasonryLabel-%1").arg(row));
            QVERIFY(label);
            QCOMPARE(label->property("text").toString(), expectedName);
        }

        // Start another synchronous snapshot generation, then exercise both
        // a metadata notification and a viewport replacement before its
        // frame callback. Ordinary updateProperties() must keep every newly
        // active row snapshot-only and append it to the bounded deferred
        // queue rather than eagerly materializing the complete new window.
        QSignalSpy pendingUpdateFrameSpy(
            &view, &QQuickWindow::frameSwapped);
        QVERIFY(applyCatalog(largeCatalog, 3,
                             QStringLiteral("D:/synthetic/large-pending")));
        QQuickItem *pendingFirstSurface = panel->findChild<QQuickItem *>(
            QStringLiteral("gallerySelectionSurface-0"));
        QVERIFY(pendingFirstSurface);
        firstSlot = qobject_cast<BrickItem *>(
            pendingFirstSurface->parentItem());
        QVERIFY(firstSlot);
        QVERIFY(!firstSlot->property("model").value<ImageFile *>());
        const auto facadeChildCount = [&]() {
            return model->findChildren<ImageFile *>(
                QString(), Qt::FindDirectChildrenOnly).size();
        };
        const int facadesBeforePendingUpdates = facadeChildCount();
        const QVariantMap pendingMetadata{
            {QStringLiteral("entryId"), QStringLiteral("large-entry-0")},
            {QStringLiteral("index"), 0},
            {QStringLiteral("size"), qint64(4096)},
            {QStringLiteral("sizeText"), QStringLiteral("4 KB")},
        };
        QVERIFY(session->applyExternalMetadata(
            {pendingMetadata}, 3, 0, false));
        QCOMPARE(facadeChildCount(), facadesBeforePendingUpdates);
        QCOMPARE(firstSlot->visualRow()
                     .value(QStringLiteral("displayFields")).toMap()
                     .value(QStringLiteral("sizeText")).toString(),
                 QStringLiteral("4 KB"));

        const qreal pendingScrollTarget = qMin<qreal>(
            qMax<qreal>(0, layout->contentHeight() - layout->height()),
            layout->height() * 2);
        QVERIFY(pendingScrollTarget > 0);
        layout->setContentY(pendingScrollTarget);
        QCOMPARE(facadeChildCount(), facadesBeforePendingUpdates);
        int scrolledRow = -1;
        for (const QVariant &rowValue : layout->visibleIndexes()) {
            if (!firstFrameRows.contains(rowValue)) {
                scrolledRow = rowValue.toInt();
                break;
            }
        }
        QVERIFY(scrolledRow >= 0);
        QQuickItem *scrolledSurface = panel->findChild<QQuickItem *>(
            QStringLiteral("gallerySelectionSurface-%1").arg(scrolledRow));
        QVERIFY(scrolledSurface);
        QPointer<BrickItem> scrolledSlot = qobject_cast<BrickItem *>(
            scrolledSurface->parentItem());
        QVERIFY(scrolledSlot);
        QVERIFY(!scrolledSlot->property("model").value<ImageFile *>());
        QCOMPARE(scrolledSlot->visualRow()
                     .value(QStringLiteral("entryId")).toString(),
                 QStringLiteral("large-entry-%1").arg(scrolledRow));
        session->setCurrentIndex(scrolledRow);

        // Materialization is queued behind the swap. Once allowed to run it
        // may bind only the same entry identity represented by the snapshot,
        // including a row which became active during the pending scroll.
        view.update();
        QVERIFY(pendingUpdateFrameSpy.wait(3000));
        QTRY_VERIFY_WITH_TIMEOUT(
            (scrolledSurface = panel->findChild<QQuickItem *>(
                 QStringLiteral("gallerySelectionSurface-%1")
                     .arg(scrolledRow)))
            && (scrolledSlot = qobject_cast<BrickItem *>(
                    scrolledSurface->parentItem()))
            && scrolledSlot->property("viewIndex").toInt() == scrolledRow
            && scrolledSlot->visualFacadeReady(), 1500);
        QCOMPARE(scrolledSlot->property("model").value<ImageFile *>()->fileName(),
                 QStringLiteral("large-%1.%2").arg(scrolledRow).arg(
                     scrolledRow % 4 == 0 ? QStringLiteral("png")
                                          : QStringLiteral("txt")));
        session->setCurrentIndex(0);
        layout->setContentY(0);
        QQuickItem *returnedFirstSurface = nullptr;
        // Pool reuse can make QObject::findChild observe the old objectName
        // binding for one event turn. Re-resolve the current row while the
        // view settles instead of retaining that unrelated scrolled slot.
        QTRY_VERIFY_WITH_TIMEOUT(
            (returnedFirstSurface = panel->findChild<QQuickItem *>(
                 QStringLiteral("gallerySelectionSurface-0")))
            && (firstSlot = qobject_cast<BrickItem *>(
                    returnedFirstSurface->parentItem()))
            && firstSlot->property("viewIndex").toInt() == 0
            && firstSlot->visualRow().value(
                   QStringLiteral("entryId")).toString()
                   == QStringLiteral("large-entry-0")
            && firstSlot->visualFacadeReady()
            && firstSlot->property("model").value<ImageFile *>()
            && firstSlot->property("model").value<ImageFile *>()->fileName()
                   == QStringLiteral("large-0.png"), 1000);
        ImageFile *largeImage = firstSlot->property("model")
                                    .value<ImageFile *>();
        QVERIFY(largeImage);
        QVERIFY(largeImage != warmImage.data());
        QCOMPARE(largeImage->fileName(), QStringLiteral("large-0.png"));
        qInfo() << "Masonry staged catalog sync ms"
                << firstResetNs / 1'000'000.0
                << "full apply-to-frame ms"
                << applyToPaintedFrameNs / 1'000'000.0
                << "apply-to-complete-frame ms"
                << stagedVisualNs / 1'000'000.0;

        // Removed row objects remain valid only long enough for the direct
        // old->new delegate hand-off, then are reclaimed by DeferredDelete.
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QTRY_VERIFY_WITH_TIMEOUT(warmImage.isNull(), 3000);

        // Replace the catalog twice without yielding to the first reset's
        // polish/materialization callbacks. Generation and entry-ID guards
        // must prevent the intermediate small catalog from ever reaching the
        // latest painted slot or its delayed QObject facade.
        QSignalSpy rapidReplacementFrameSpy(
            &view, &QQuickWindow::frameSwapped);
        QVERIFY(applyCatalog(smallCatalog, 4,
                             QStringLiteral("D:/synthetic/small")));
        QVERIFY(applyCatalog(largeCatalog, 5,
                             QStringLiteral("D:/synthetic/large")));
        view.update();
        QVERIFY(rapidReplacementFrameSpy.wait(3000));
        QQuickItem *latestFirstSurface = panel->findChild<QQuickItem *>(
            QStringLiteral("gallerySelectionSurface-0"));
        QVERIFY(latestFirstSurface);
        firstSlot = qobject_cast<BrickItem *>(
            latestFirstSurface->parentItem());
        QVERIFY(firstSlot);
        QCOMPARE(firstSlot->visualRow()
                     .value(QStringLiteral("entryId")).toString(),
                 QStringLiteral("large-entry-0"));
        QCOMPARE(firstSlot->visualRow()
                     .value(QStringLiteral("text")).toString(),
                 QStringLiteral("large-0.png"));
        // The post-swap batch may already have run before QSignalSpy::wait()
        // returns. If so, it must belong to the latest generation, never the
        // intermediate small catalog.
        if (ImageFile *facade = firstSlot->property("model")
                                    .value<ImageFile *>()) {
            QCOMPARE(facade->fileName(), QStringLiteral("large-0.png"));
        }
        QTRY_VERIFY_WITH_TIMEOUT(firstSlot->visualFacadeReady(), 1000);
        QCOMPARE(firstSlot->property("model").value<ImageFile *>()->fileName(),
                 QStringLiteral("large-0.png"));

        const auto materializedCount = [&]() {
            return model->findChildren<ImageFile *>(
                QString(), Qt::FindDirectChildrenOnly).size();
        };
        QVERIFY2(materializedCount() <= 64,
                 qPrintable(QStringLiteral("materialized after 447 reset=%1")
                                .arg(materializedCount())));

        QList<qint64> resetDurationsNs{firstResetNs};
        qulonglong revision = 6;
        for (int iteration = 0; iteration < 8; ++iteration) {
            const bool useSmall = iteration % 2 == 0;
            timer.restart();
            QVERIFY(applyCatalog(
                useSmall ? smallCatalog : largeCatalog, revision++,
                useSmall ? QStringLiteral("D:/synthetic/small")
                         : QStringLiteral("D:/synthetic/large")));
            const qint64 elapsedNs = timer.nsecsElapsed();
            resetDurationsNs.append(elapsedNs);
            QCOMPARE(layout->count(), useSmall ? 14 : 447);
            QVERIFY2(elapsedNs < 33'000'000,
                     qPrintable(QStringLiteral(
                         "Masonry reset %1 rows took %2 ms")
                         .arg(useSmall ? 14 : 447)
                         .arg(elapsedNs / 1'000'000.0, 0, 'f', 3)));

            QCoreApplication::sendPostedEvents(
                nullptr, QEvent::DeferredDelete);
            QCoreApplication::processEvents();
            QVERIFY2(materializedCount() <= 64,
                     qPrintable(QStringLiteral(
                         "Masonry reset retained %1 ImageFile objects")
                         .arg(materializedCount())));
        }

        // A same-identity refresh keeps the visible ImageFile QObject. Its
        // notifying properties still have to update because assigning the
        // same pointer back to the QML delegate does not invalidate bindings.
        QQuickItem *stableFirstSurface = panel->findChild<QQuickItem *>(
            QStringLiteral("gallerySelectionSurface-0"));
        QVERIFY(stableFirstSurface);
        firstSlot = qobject_cast<BrickItem *>(
            stableFirstSurface->parentItem());
        QVERIFY(firstSlot);
        QTRY_VERIFY_WITH_TIMEOUT(firstSlot->visualFacadeReady(), 1000);
        QVariantList renamedLargeCatalog = largeCatalog;
        QVariantMap renamedFirst = renamedLargeCatalog.first().toMap();
        renamedFirst[QStringLiteral("name")] =
            QStringLiteral("large-renamed.png");
        renamedFirst[QStringLiteral("localPath")] =
            QStringLiteral("D:/synthetic/large/large-renamed.png");
        renamedLargeCatalog[0] = renamedFirst;
        QPointer<ImageFile> stableImage = firstSlot->property("model")
                                              .value<ImageFile *>();
        QVERIFY(stableImage);
        QSignalSpy textChangedSpy(stableImage, &ImageFile::textChanged);
        timer.restart();
        QVERIFY(applyCatalog(renamedLargeCatalog, revision++,
                             QStringLiteral("D:/synthetic/large")));
        const qint64 stableResetNs = timer.nsecsElapsed();
        resetDurationsNs.append(stableResetNs);
        QVERIFY2(stableResetNs < 33'000'000,
                 qPrintable(QStringLiteral(
                     "same-ID Masonry reset took %1 ms")
                     .arg(stableResetNs / 1'000'000.0, 0, 'f', 3)));
        QCOMPARE(firstSlot->property("model").value<ImageFile *>(),
                 stableImage.data());
        QCOMPARE(stableImage->fileName(), QStringLiteral("large-renamed.png"));
        QVERIFY(textChangedSpy.count() > 0);

        // A DecodeManager batch must publish every cheap known-size role but
        // pay for only one full Masonry rewrap. Row 0 and row 4 deliberately
        // receive opposite aspect ratios for the reset-geometry check below.
        auto *decodeManager = runtime->findChild<DecodeManager *>();
        QVERIFY(decodeManager);
        QList<ImageInfo> metadataBatch;
        for (int row = 0; row < renamedLargeCatalog.size(); ++row) {
            const QVariantMap entry = renamedLargeCatalog.at(row).toMap();
            if (!entry.value(QStringLiteral("isImage")).toBool()) {
                continue;
            }
            ImageInfo info;
            info.path = entry.value(QStringLiteral("localPath")).toString();
            // Decoder metadata can know the effective size before a deferred
            // semantic metadata chunk updates Entry::size. Snapshot-owned
            // Details fields must match ImageFile defaults in that interval.
            info.fileSize = row == 0 ? 12'345 : -1;
            info.sourceVersionToken = 0;
            info.imageSize = row == 0
                ? QSize(600, 100)
                : (row == 4 ? QSize(100, 600)
                            : QSize(320 + row % 5 * 20, 180));
            info.orientation = ExifOrientation::Horizontal;
            info.requestNamespace = session->sessionId();
            metadataBatch.append(std::move(info));
        }
        QVERIFY(!metadataBatch.isEmpty());
        metadataBatch.last().isLast = true;
        int knownSizeNotifications = 0;
        connect(model, &QAbstractItemModel::dataChanged, this,
                [&](const QModelIndex &, const QModelIndex &,
                    const QList<int> &roles) {
                    if (roles.contains(
                            ZoinGallery::ExternalCatalogModel::
                                KnownImageSizeRole)) {
                        ++knownSizeNotifications;
                    }
                });
        QSignalSpy layoutBandsSpy(layout, &MasonryLayout::layoutBandsChanged);
        timer.restart();
        decodeManager->imagesInfoReady(metadataBatch);
        const qint64 metadataBatchNs = timer.nsecsElapsed();
        QCOMPARE(knownSizeNotifications, metadataBatch.size());
        QVERIFY2(metadataBatchNs < 33'000'000,
                 qPrintable(QStringLiteral(
                     "447-row metadata batch dispatch took %1 ms")
                     .arg(metadataBatchNs / 1'000'000.0, 0, 'f', 3)));
        QTRY_COMPARE_WITH_TIMEOUT(layoutBandsSpy.count(), 1, 1000);
        const QVariantMap metadataVisual = model->index(0, 0).data(
            ZoinGallery::ExternalCatalogModel::VisualSnapshotRole).toMap();
        const QString snapshotSize = metadataVisual.value(
            QStringLiteral("displayFields")).toMap().value(
                QStringLiteral("sizeText")).toString();
        QCOMPARE(snapshotSize, QStringLiteral("12.06 KB"));
        QCOMPARE(stableImage->displayFields().value(
                     QStringLiteral("sizeText")).toString(),
                 snapshotSize);

        // Reordering known, unequal aspect ratios reuses the painted row slot.
        // Its geometry must equal the new hit-test geometry immediately; a
        // model reset is not a 500 ms cross-catalog layout animation.
        const QRectF geometryBeforeReorder = firstSlot->geometry();
        QVariantList reorderedCatalog = renamedLargeCatalog;
        reorderedCatalog.swapItemsAt(0, 4);
        timer.restart();
        QVERIFY(applyCatalog(reorderedCatalog, revision++,
                             QStringLiteral("D:/synthetic/large")));
        const qint64 reorderedResetNs = timer.nsecsElapsed();
        resetDurationsNs.append(reorderedResetNs);
        QVERIFY2(reorderedResetNs < 33'000'000,
                 qPrintable(QStringLiteral(
                     "known-size reordered reset took %1 ms")
                     .arg(reorderedResetNs / 1'000'000.0, 0, 'f', 3)));
        const QRectF expectedResetGeometry(
            layout->indexGeometry(0).toRect());
        QVERIFY(geometryBeforeReorder != expectedResetGeometry);
        QCOMPARE(firstSlot->geometry(), expectedResetGeometry);

        // Quick-search highlighting is applied only to current materialized
        // facades. Matching and repeated cursor changes must not construct the
        // remaining hundreds of ImageFile QObjects.
        const int materializedBeforeSearch = materializedCount();
        layout->quickSearch()->setMask(QStringLiteral("large"));
        for (int index = 1; index < 120; ++index) {
            layout->setCurrentIndex(index);
        }
        QCOMPARE(materializedCount(), materializedBeforeSearch);
        layout->quickSearch()->setMask(QString());

        qint64 worstResetNs = 0;
        for (const qint64 duration : std::as_const(resetDurationsNs)) {
            worstResetNs = qMax(worstResetNs, duration);
        }
        qInfo() << "Masonry alternating reset worst ms"
                << worstResetNs / 1'000'000.0
                << "metadata batch ms" << metadataBatchNs / 1'000'000.0
                << "materialized" << materializedCount();

        QPointer<ImageFile> shutdownOrphan = firstSlot->property("model")
                                                 .value<ImageFile *>();
        QVERIFY(shutdownOrphan);
        model->resetExternalSource();
        QCOMPARE(model->rowCount(), 0);
        QVERIFY(shutdownOrphan);
        model->shutdown();
        QVERIFY(shutdownOrphan.isNull());
        runtime->shutdown();
    }

    void detailsLiveSizeCatalogResetPaintsWithinKeyboardFrame() {
        QQuickView view;
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        options.maxDecodeThreads = 2;
        auto *runtime = ZoinGallery::GalleryRuntime::install(
            view.engine(), options);
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(
            QStringLiteral("details-live-size-reset"));
        QVERIFY(session);

        const auto catalogWithIcon = [](const QString &prefix, int count,
                                        const QString &iconPath) {
            QVariantList catalog = prefixedCatalog(prefix, count);
            for (int row = 0; row < catalog.size(); ++row) {
                QVariantMap entry = catalog.at(row).toMap();
                entry.insert(QStringLiteral("highlightStyle"), QVariantMap{
                    {QStringLiteral("icon"), iconPath},
                });
                catalog[row] = entry;
            }
            return catalog;
        };
        const QVariantList warmCatalog = catalogWithIcon(
            QStringLiteral("live-warm"), 80,
            QStringLiteral("qrc:/ZoinGallery/resources/FolderIcon.svg"));
        const QVariantList largeCatalog = catalogWithIcon(
            QStringLiteral("live-large"), 447,
            QStringLiteral("qrc:/ZoinGallery/resources/FileIcon.svg"));
        const auto applyCatalog = [&](const QVariantList &catalog,
                                      qulonglong revision,
                                      const QString &path) {
            return session->applyExternalCatalog(
                catalog, revision,
                {{QStringLiteral("currentPath"), path},
                 {QStringLiteral("metadataDeferred"), true}});
        };
        QVERIFY(applyCatalog(warmCatalog, 1,
                             QStringLiteral("D:/synthetic/live-warm")));
        QObject *panel = createPanel(
            view, session, QStringLiteral("detailsLiveSizeSession"),
            QStringLiteral("details"));
        QVERIFY(panel);
        auto *panelItem = qobject_cast<QQuickItem *>(panel);
        auto *layout = panel->findChild<MasonryLayout *>(
            QStringLiteral("galleryMasonryLayout"));
        QVERIFY(panelItem && layout);
        layout->setDensity(24.2);
        panelItem->setHeight(1200);
        view.resize(640, 1200);
        QTRY_VERIFY_WITH_TIMEOUT(layout->height() >= 1100, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(layout->visibleIndexes().size() >= 49, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(
            panel->findChild<QQuickItem *>(
                QStringLiteral("gallerySelectionSurface-0")), 3000);

        QSignalSpy frameSpy(&view, &QQuickWindow::frameSwapped);
        QElapsedTimer applyToFrameTimer;
        applyToFrameTimer.start();
        QVERIFY(applyCatalog(largeCatalog, 2,
                             QStringLiteral("D:/synthetic/live-large")));
        const qint64 synchronousResetNs = applyToFrameTimer.nsecsElapsed();
        const QVariantList visibleRows = layout->visibleIndexes();
        QVERIFY2(visibleRows.size() >= 49,
                 qPrintable(QStringLiteral("live-size delegates=%1")
                                .arg(visibleRows.size())));
        QQuickItem *firstSurface = panel->findChild<QQuickItem *>(
            QStringLiteral("gallerySelectionSurface-0"));
        QVERIFY(firstSurface);
        auto *firstSlot = qobject_cast<BrickItem *>(
            firstSurface->parentItem());
        QVERIFY(firstSlot);
        QVERIFY(!firstSlot->property("model").value<ImageFile *>());
        QCOMPARE(firstSlot->property("entryId").toString(),
                 QStringLiteral("live-large-entry-0"));
        view.update();
        QVERIFY(frameSpy.wait(3000));
        const qint64 applyToFrameNs = applyToFrameTimer.nsecsElapsed();

        // The offscreen threaded render loop batches swaps at a platform-
        // dependent cadence (about 50 ms on the Windows CI backend), so the
        // deterministic contract here is that all 49+ correct delegates are
        // synchronously ready for the next scene-graph sync. Canonical live
        // profiling measures the actual window's apply-to-swap latency.
        QVERIFY2(synchronousResetNs < 12'000'000,
                 qPrintable(QStringLiteral(
                     "49-row synchronous snapshot took %1 ms")
                     .arg(synchronousResetNs / 1'000'000.0, 0, 'f', 3)));
        QVERIFY2(applyToFrameNs < 100'000'000,
                 qPrintable(QStringLiteral(
                     "offscreen 49-row apply-to-frame stalled for %1 ms")
                     .arg(applyToFrameNs / 1'000'000.0, 0, 'f', 3)));
        for (const QVariant &rowValue : visibleRows) {
            const int row = rowValue.toInt();
            QQuickItem *surface = panel->findChild<QQuickItem *>(
                QStringLiteral("gallerySelectionSurface-%1").arg(row));
            QVERIFY(surface);
            auto *slot = qobject_cast<BrickItem *>(surface->parentItem());
            QVERIFY(slot);
            QCOMPARE(slot->property("entryId").toString(),
                     QStringLiteral("live-large-entry-%1").arg(row));
            QObject *visualModel = slot->visualModel();
            QVERIFY(visualModel);
            QCOMPARE(visualModel->property("text").toString(),
                     QStringLiteral("live-large-%1.%2").arg(row).arg(
                         row % 4 == 0 ? QStringLiteral("png")
                                      : QStringLiteral("txt")));
        }
        qInfo() << "Masonry live-size typed snapshot delegates"
                << visibleRows.size()
                << "sync ms" << synchronousResetNs / 1'000'000.0
                << "apply-to-frame ms" << applyToFrameNs / 1'000'000.0;
        runtime->shutdown();
    }

    void detailsScrollMetricsMatchClassicListViewEstimator() {
        QQuickView view;
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        auto *runtime = ZoinGallery::GalleryRuntime::install(
            view.engine(), options);
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(
            QStringLiteral("details-scroll-metrics"));
        QVERIFY(session);

        QObject *panel = createPanel(
            view, session, QStringLiteral("detailsMetricsSession"),
            QStringLiteral("details"));
        QVERIFY(panel);
        panel->setProperty("showDetailsHeader", false);
        panel->setProperty("density", 24.2);
        panel->setProperty("height", 435.2);
        auto *panelItem = qobject_cast<QQuickItem *>(panel);
        auto *layout = panel->findChild<MasonryLayout *>(
            QStringLiteral("galleryMasonryLayout"));
        auto *externalModel =
            qobject_cast<ZoinGallery::ExternalCatalogModel *>(
                session->model());
        auto *metrics = panel->findChild<QQuickItem *>(
            QStringLiteral("galleryDetailsScrollMetrics"));
        auto *scrollBar = panel->findChild<QQuickItem *>(
            QStringLiteral("galleryPanelScrollBar"));
        QVERIFY(panelItem && layout && externalModel && metrics && scrollBar);
        QTRY_COMPARE_WITH_TIMEOUT(metrics->property("count").toInt(), 0,
                                  3000);

        QQmlComponent classicComponent(view.engine());
        classicComponent.setData(R"QML(
            import QtQuick
            ListView {
                objectName: "classicDetailsMetrics"
                property real rowExtent: 24.2
                width: 640
                height: 435.2
                enabled: false
                interactive: false
                clip: true
                cacheBuffer: 320
                boundsBehavior: Flickable.StopAtBounds
                delegate: Item {
                    required property int index
                    width: ListView.view.width
                    height: ListView.view.rowExtent
                }
            }
        )QML", QUrl(QStringLiteral("inline:ClassicDetailsMetrics.qml")));
        QVERIFY2(classicComponent.isReady(),
                 qPrintable(classicComponent.errorString()));
        QObject *classicObject = classicComponent.create(
            view.engine()->rootContext());
        QVERIFY2(classicObject, qPrintable(classicComponent.errorString()));
        auto *classic = qobject_cast<QQuickItem *>(classicObject);
        QVERIFY(classic);
        classic->setParent(panelItem);
        classic->setParentItem(panelItem);
        classic->setProperty("model", 0);

        const auto metricsDiagnostic = [&]() {
            return QStringLiteral(
                "proxy contentHeight=%1 classic=%2 proxyY=%3 "
                "classicY=%4 layoutY=%5 proxyExtent=%6 layoutDensity=%7 "
                "proxyIndex=%8 classicIndex=%9 rebuildPending=%10 "
                "proxyOrigin=%11 classicOrigin=%12 layoutHeight=%13 "
                "layoutContentHeight=%14 layoutCount=%15")
                .arg(metrics->property("contentHeight").toReal(),
                     0, 'g', 17)
                .arg(classic->property("contentHeight").toReal(),
                     0, 'g', 17)
                .arg(metrics->property("contentY").toReal(),
                     0, 'g', 17)
                .arg(classic->property("contentY").toReal(),
                     0, 'g', 17)
                .arg(layout->contentY(), 0, 'g', 17)
                .arg(metrics->property("rowExtent").toReal(),
                     0, 'g', 17)
                .arg(layout->density(), 0, 'g', 17)
                .arg(metrics->property("currentIndex").toInt())
                .arg(classic->property("currentIndex").toInt())
                .arg(false)
                .arg(metrics->property("originY").toReal(), 0, 'g', 17)
                .arg(classic->property("originY").toReal(), 0, 'g', 17)
                .arg(layout->height(), 0, 'g', 17)
                .arg(layout->contentHeight(), 0, 'g', 17)
                .arg(layout->count());
        };

        const auto setPanelPosition = [&](qreal position) {
            const bool positioned = QMetaObject::invokeMethod(
                panel, "setPanelContentY", Qt::DirectConnection,
                Q_ARG(QVariant, QVariant(position)),
                Q_ARG(QVariant, QVariant(true)));
            QVERIFY(positioned);
        };

        const auto verifyScrollBarUsesProxy = [&]() {
            const qreal extent = metrics->property("contentHeight").toReal();
            QVERIFY2(extent > 0, qPrintable(metricsDiagnostic()));
            const qreal expectedSize = qMin<qreal>(1, layout->height() / extent);
            const qreal expectedPosition = metrics->property("contentY").toReal()
                                         / extent;
            QTRY_VERIFY2_WITH_TIMEOUT(
                qAbs(scrollBar->property("size").toReal()
                     - expectedSize) < 0.0001
                && qAbs(scrollBar->property("position").toReal()
                        - expectedPosition) < 0.0001,
                qPrintable(metricsDiagnostic()), 3000);
        };

        const auto metricDelegateCount = [&]() {
            int count = 0;
            QList<QQuickItem *> pending{metrics};
            while (!pending.isEmpty()) {
                QQuickItem *item = pending.takeLast();
                if (item->objectName().startsWith(
                        QStringLiteral("galleryDetailsScrollMetricRow-"))) {
                    ++count;
                }
                pending.append(item->childItems());
            }
            return count;
        };

        // Start both native ListViews empty and populate them only after the
        // final fractional row extent and viewport are established. This is
        // the legacy f4 Loader lifecycle and is deterministic in the pinned
        // Qt 6.11.1 runtime. Dynamic cache-window estimates after scrolling
        // are deliberately not compared across instances: Qt incubates those
        // delegates in instance-dependent frame order.
        QVERIFY(session->applyExternalCatalog(plainCatalog(105), 1));
        classic->setProperty("model", 105);
        QTRY_COMPARE_WITH_TIMEOUT(metrics->property("count").toInt(), 105,
                                  3000);
        QTRY_COMPARE_WITH_TIMEOUT(classic->property("count").toInt(), 105,
                                  3000);
        QTRY_VERIFY2_WITH_TIMEOUT(
            qAbs(metrics->property("contentHeight").toReal() - 2526.4)
                < 0.0001
            && qAbs(classic->property("contentHeight").toReal() - 2526.4)
                < 0.0001,
            qPrintable(metricsDiagnostic()), 5000);
        QCOMPARE(metrics->property("contentY").toReal(), qreal(0));
        QCOMPARE(classic->property("contentY").toReal(), qreal(0));
        verifyScrollBarUsesProxy();

        // The shared ScrollBar maps its terminal position through the proxy
        // extent, exactly like f4's classic manual ScrollBar mapping. The
        // renderer keeps an exact fractional geometry extent, so record and
        // verify that mapping independently from cursor reveal semantics.
        const qreal proxyExtent = metrics->property("contentHeight").toReal();
        const qreal dragEndpoint = qMax<qreal>(0, proxyExtent - layout->height());
        setPanelPosition(dragEndpoint);
        QTRY_VERIFY_WITH_TIMEOUT(
            qAbs(layout->contentY() - dragEndpoint) < 0.0001, 3000);
        verifyScrollBarUsesProxy();

        const int lastIndex = 104;
        const QRectF lastGeometry = layout->indexGeometry(lastIndex);
        QVERIFY(lastGeometry.isValid());
        QVERIFY(qAbs(lastGeometry.y() - lastIndex * 24.2) < 0.0001);
        QVERIFY(qAbs(lastGeometry.height() - 24.2) < 0.0001);

        // Cursor mirroring is imperative: a direct ListView binding can push
        // its model-reset index zero back into MasonryLayout during startup.
        // Exercise zero, an interior cursor, and the terminal row while
        // keeping the independent legacy ListView on the same cursor.
        for (const int cursor : {0, 37, lastIndex}) {
            session->setCurrentIndex(cursor);
            classic->setProperty("currentIndex", cursor);
            QTRY_COMPARE_WITH_TIMEOUT(layout->currentIndex(), cursor, 3000);
            QTRY_COMPARE_WITH_TIMEOUT(
                metrics->property("currentIndex").toInt(), cursor, 3000);
        }
        QVERIFY(invokeEnsureCurrentVisible(panel, false, QVariant(1)));
        const qreal exactEndpoint = qMax<qreal>(
            0, layout->contentHeight() - layout->height());
        QTRY_VERIFY_WITH_TIMEOUT(
            qAbs(layout->contentY() - exactEndpoint) < 0.0001, 3000);
        QVERIFY(indexIntersectsViewport(layout, lastIndex));
        QVERIFY(lastGeometry.bottom()
                <= layout->contentY() + layout->height() + 0.0001);

        // A source/count/viewport change keeps the ScrollBar coupled to the
        // native estimator without rebuilding the exact renderer geometry.
        setPanelPosition(0);
        panel->setProperty("height", 360.0);
        QVERIFY(session->applyExternalCatalog(plainCatalog(37), 2));
        QTRY_COMPARE_WITH_TIMEOUT(metrics->property("count").toInt(), 37,
                                  3000);
        QTRY_VERIFY_WITH_TIMEOUT(metrics->property("contentHeight").toReal()
                                 > 0, 3000);
        verifyScrollBarUsesProxy();

        // The estimator is layout-only: even at 10k entries it materializes
        // a bounded cache window of empty Items and never requests metadata,
        // thumbnails, provider IDs, or ImageFile copies.
        QVERIFY(session->applyExternalCatalog(plainCatalog(10'000), 3));
        QTRY_COMPARE_WITH_TIMEOUT(metrics->property("count").toInt(), 10'000,
                                  5000);
        QTRY_VERIFY_WITH_TIMEOUT(metricDelegateCount() > 0, 3000);
        QVERIFY2(metricDelegateCount() <= 64,
                 qPrintable(QStringLiteral("metric delegates=%1")
                                .arg(metricDelegateCount())));
        const qreal largeUsableViewportHeight = layout->height()
            - layout->paddingTop() - layout->paddingBottom();
        const int largeCompleteVisibleRows = qMax(
            1, int(std::floor(largeUsableViewportHeight / layout->density()
                              + 0.000000001)));
        const qreal largeTrailingViewportRemainder = qMax<qreal>(
            0, largeUsableViewportHeight
                - largeCompleteVisibleRows * layout->density());
        const qreal largeExpectedContentHeight = layout->paddingTop()
            + 10'000 * layout->density() + largeTrailingViewportRemainder
            + layout->paddingBottom();
        QVERIFY(qAbs(layout->contentHeight() - largeExpectedContentHeight)
                < 0.0001);
        const auto materializedCount = [&]() {
            return externalModel->findChildren<ImageFile *>(
                QString(), Qt::FindDirectChildrenOnly).size();
        };
        QTRY_VERIFY2_WITH_TIMEOUT(
            materializedCount() <= 64,
            qPrintable(QStringLiteral(
                "10k Details catalog materialized %1 ImageFile objects")
                .arg(materializedCount())), 3000);

        QCOMPARE(runtime->thumbnailCachePendingRequestCount(), qsizetype(0));
        QCOMPARE(runtime->thumbnailCacheMissCount(), quint64(0));
        QCOMPARE(runtime->thumbnailCacheStoreCount(), quint64(0));
    }

    void verticalScrollBarDoesNotChangeViewportWidth() {
        QQuickView view;
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        auto *runtime = ZoinGallery::GalleryRuntime::install(
            view.engine(), options);
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(
            QStringLiteral("stable-scrollbar-lane"));
        QVERIFY(session);
        QVERIFY(session->applyExternalCatalog(plainCatalog(1), 1));

        QObject *panel = createPanel(
            view, session, QStringLiteral("stableScrollbarLaneSession"),
            QStringLiteral("grid"));
        QVERIFY(panel);
        auto *panelItem = qobject_cast<QQuickItem *>(panel);
        auto *layout = panel->findChild<MasonryLayout *>(
            QStringLiteral("galleryMasonryLayout"));
        auto *scrollBar = panel->findChild<QQuickItem *>(
            QStringLiteral("galleryPanelScrollBar"));
        QVERIFY(panelItem && layout && scrollBar);
        QTRY_COMPARE_WITH_TIMEOUT(layout->count(), 1, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!scrollBar->isVisible(), 3000);

        const qreal widthWithoutScrollBar = layout->width();
        const qreal leftInset = layout->x();
        const qreal rightInset = panelItem->width()
            - layout->x() - layout->width();
        QCOMPARE(leftInset, 6.0);
        QCOMPARE(rightInset, leftInset);

        QVERIFY(session->applyExternalCatalog(plainCatalog(200), 2));
        QTRY_COMPARE_WITH_TIMEOUT(layout->count(), 200, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(scrollBar->isVisible(), 3000);
        QCOMPARE(layout->x(), leftInset);
        QCOMPARE(layout->width(), widthWithoutScrollBar);
        QCOMPARE(panelItem->width() - layout->x() - layout->width(),
                 rightInset);
        QCOMPARE(scrollBar->x() + scrollBar->width(),
                 panelItem->width() + 8.0);

        runtime->shutdown();
    }

    void horizontalScrollBarIsFlushWithPanelBottom() {
        QQuickView view;
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        auto *runtime = ZoinGallery::GalleryRuntime::install(
            view.engine(), options);
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(
            QStringLiteral("horizontal-scrollbar-lane"));
        QVERIFY(session);
        QVERIFY(session->applyExternalCatalog(plainCatalog(200), 1));

        QObject *panel = createPanel(
            view, session, QStringLiteral("horizontalScrollbarLaneSession"),
            QStringLiteral("columns"));
        QVERIFY(panel);
        auto *panelItem = qobject_cast<QQuickItem *>(panel);
        auto *layout = panel->findChild<MasonryLayout *>(
            QStringLiteral("galleryMasonryLayout"));
        auto *scrollBar = panel->findChild<QQuickItem *>(
            QStringLiteral("galleryPanelColumnsScrollBar"));
        QVERIFY(panelItem && layout && scrollBar);

        QTRY_COMPARE_WITH_TIMEOUT(layout->presentationMode(),
                                  MasonryLayout::Columns, 3000);
        for (const int columnCount : {2, 3}) {
            panel->setProperty("columnCount", columnCount);
            QTRY_COMPARE_WITH_TIMEOUT(layout->columnCount(), columnCount,
                                      3000);
            QTRY_VERIFY_WITH_TIMEOUT(scrollBar->isVisible(), 3000);
            QTRY_VERIFY_WITH_TIMEOUT(
                qAbs(scrollBar->mapToItem(
                         panelItem, QPointF(0, scrollBar->height())).y()
                     - panelItem->height()) < 0.01,
                3000);

            // The viewport still owns its six-pixel tile inset; the scrollbar
            // is deliberately outside that inset and flush with the panel.
            QVERIFY(qAbs(panelItem->height()
                         - (layout->y() + layout->height()) - 6.0) < 0.01);
        }

        runtime->shutdown();
    }
};

QTEST_MAIN(MasonryLayoutModesTest)

#include "MasonryLayoutModesTest.moc"
