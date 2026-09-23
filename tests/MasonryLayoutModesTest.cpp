#include "FileListModel.h"
#include "DecodeManager.h"
#include "PersistentDerivedImageCache.h"
#include "MasonryLayout.h"
#include "SvgCursor.h"
#include "tests/DirectoryPreviewFixture.h"

#include <ZoinGallery/GalleryRuntime.h>
#include <ZoinGallery/GallerySession.h>

#include "src/embed/ExternalCatalogModel.h"

#include <QCoreApplication>
#include <QStandardItemModel>
#include <QColor>
#include <QColorSpace>
#include <QCursor>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QImage>
#include <QKeyEvent>
#include <QElapsedTimer>
#include <QEvent>
#include <QParallelAnimationGroup>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlProperty>
#include <QQuickImageProvider>
#include <QQuickView>
#include <QScopeGuard>
#include <QSGRendererInterface>
#include <QTemporaryDir>
#include <QUrlQuery>
#include <QtTest>

#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>
#include <utility>

#if defined(Q_OS_MACOS)
#include <CoreGraphics/CoreGraphics.h>
#endif

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
    explicit CompactIconProvider(bool checker = false)
        : QQuickImageProvider(QQuickImageProvider::Image), m_checker(checker) {}
    QSize lastRequestedSize;
    QSize chevronRequestedSize;

    QImage requestImage(const QString &id,
                        QSize *size,
                        const QSize &requestedSize) override {
        const QSize imageSize = requestedSize.isValid()
            ? requestedSize : QSize(16, 16);
        if (id.contains("chevron-"))
            chevronRequestedSize = requestedSize;
        if (size) {
            *size = imageSize;
        }
        QImage image(imageSize, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        if (m_checker) {
            lastRequestedSize = requestedSize;
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x)
                    if ((x + y) % 2)
                        image.setPixel(x, y, 0);
        }
        return image;
    }
private:
    bool m_checker;
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

bool setPanelObjectProperties(QObject *panel, const char *propertyName,
                              const QVariantMap &values) {
    if (!panel) {
        return false;
    }
    QObject *target = panel->property(propertyName).value<QObject *>();
    if (!target) {
        return false;
    }
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        QQmlProperty property(target, it.key());
        if (!property.isValid() || !property.write(it.value())) {
            return false;
        }
    }
    return true;
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
    QQuickItem *hiddenMatch = nullptr;
    QList<QQuickItem *> pending{root};
    while (!pending.isEmpty()) {
        QQuickItem *item = pending.takeLast();
        if (item->objectName() == objectName) {
            if (item->isVisible()) {
                return item;
            }
            hiddenMatch = item;
        }
        pending.append(item->childItems());
    }
    return hiddenMatch;
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
    void groupHeaderPixelGridAndSpacing_data() {
        QTest::addColumn<bool>("checkSpacing");
        QTest::addColumn<bool>("checkRaster");
        QTest::addColumn<QString>("title");
        QTest::addColumn<bool>("collapsed");
        QTest::newRow("spacing-input") << true << false << QString("Folders") << false;
        QTest::newRow("ancestor-pixel-grid") << false << false << QString("Folders") << false;
        QTest::newRow("physical-raster") << false << true << QString("Folders") << false;
        QTest::newRow("descender-center") << false << false << QString("Alpha") << false;
        QTest::newRow("collapsed-center") << false << false << QString("No data") << true;
    }

    void groupHeaderPixelGridAndSpacing() {
        QFETCH(bool, checkSpacing);
        QFETCH(bool, checkRaster);
        QFETCH(QString, title);
        QFETCH(bool, collapsed);
        QQuickView view;
        auto *rasterProvider = new CompactIconProvider(true);
        view.engine()->addImageProvider("header-raster", rasterProvider);
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        auto *runtime = ZoinGallery::GalleryRuntime::install(view.engine(), options);
        auto *session = runtime->createExternalSession("header-pixel-input");
        QVERIFY(session->applyExternalCatalog(plainCatalog(4), 1));
        auto *panel = qobject_cast<QQuickItem *>(createPanel(view, session, "headerPixelSession"));
        QVERIFY(panel);
        panel->setProperty("devicePixelRatio", view.devicePixelRatio());
        // Use a real SVG so the rendered check includes raster icon pixels,
        // unlike the deliberately unresolved icon URLs in generic fixtures.
        QTemporaryDir iconDirectory;
        QVERIFY(iconDirectory.isValid());
        for (const QString &name : {QStringLiteral("chevron-down"), QStringLiteral("chevron-right")}) {
            QFile svg(iconDirectory.filePath(name + ".svg"));
            QVERIFY(svg.open(QIODevice::WriteOnly));
            const QByteArray points = name.endsWith("down") ? "6 9 12 15 18 9" : "9 6 15 12 9 18";
            QVERIFY(svg.write("<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' viewBox='0 0 24 24'><polyline points='"
                + points + "' fill='none' stroke='white' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'/></svg>") > 0);
        }
        auto *resolver = panel->property("iconResolver").value<QObject *>();
        QVERIFY(resolver);
        resolver->setProperty("compactPrefix", checkRaster
            ? QStringLiteral("image://header-raster")
            : QUrl::fromLocalFile(iconDirectory.path()).toString());
        panel->setProperty("groupDescriptors", QVariantList{QVariantMap{
            {"key", "alpha"}, {"title", title}, {"startIndex", 1}, {"count", 3}}});
        auto *layout = panel->findChild<MasonryLayout *>("galleryViewportItem");
        QVERIFY(layout);
        if (collapsed)
            QVERIFY(layout->setGroupCollapsed("alpha", true));
        QQuickItem *header = nullptr;
        QTRY_VERIFY((header = findVisualItem(panel, "galleryGroupHeader-gallery-alpha")));
        auto *chevron = findVisualItem(panel, "galleryGroupHeader-gallery-alpha-chevron");
        QVERIFY(chevron);
        QTRY_COMPARE(chevron->property("status").toInt(), 1);
        if (checkRaster) {
            const int physicalExtent = qRound(chevron->width() * view.devicePixelRatio());
            QCOMPARE(rasterProvider->chevronRequestedSize, QSize(physicalExtent, physicalExtent));
        }
        if (checkSpacing) {
            const QPoint gap = header->mapToScene(QPointF(40, 3)).toPoint();
            QTest::mouseMove(&view, QPoint(1, 1));
            QTest::mouseMove(&view, gap);
            QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, gap);
            QVERIFY2(!layout->isGroupCollapsed("alpha"), "top spacing toggled the group");
            QVERIFY(!header->property("pointerHovered").toBool());
            QCOMPARE(header->property("backgroundColor"),
                     panel->property("headerColor"));
            const QPoint body = header->mapToScene(QPointF(40, 20)).toPoint();
            QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, body);
            QTRY_VERIFY(layout->isGroupCollapsed("alpha"));
            return;
        }
        // Move an ancestor after construction without changing local header
        // geometry. mapToItem alone does not subscribe to that translation.
        panel->setX(0.2);
        panel->setY(0.3);
        QTest::qWait(30);
        const qreal dpr = view.devicePixelRatio();
        for (const auto &suffix : {"title", "chevron", "count", "separator"}) {
            auto *leaf = findVisualItem(panel, QString("galleryGroupHeader-gallery-alpha-%1").arg(suffix));
            QVERIFY(leaf);
            const auto origin = leaf->mapToItem(view.contentItem(), QPointF());
            for (qreal coordinate : {origin.x() * dpr, origin.y() * dpr})
                QVERIFY2(qAbs(coordinate - qRound(coordinate)) < 0.01,
                    qPrintable(QString("%1 physical coordinate %2").arg(suffix).arg(coordinate, 0, 'f', 6)));
            QCOMPARE(leaf->mapToItem(view.contentItem(), QPointF(1, 0)) - origin, QPointF(1, 0));
            QCOMPARE(leaf->mapToItem(view.contentItem(), QPointF(0, 1)) - origin, QPointF(0, 1));
        }
        const auto capture = view.grabWindow();
        QVERIFY(!capture.isNull());
        if (!checkRaster) {
            const auto inkCenter = [&](QQuickItem *item) {
                const QPointF origin = item->mapToItem(view.contentItem(), QPointF()) * dpr;
                int top = capture.height(), bottom = -1;
                for (int y = 0; y < qRound(item->height() * dpr); ++y) {
                    for (int x = 0; x < qRound(item->width() * dpr); ++x) {
                        const int py = qRound(origin.y()) + y;
                        const QColor pixel = capture.pixelColor(qRound(origin.x()) + x, py);
                        if (qGray(pixel.rgb()) > 100) {
                            top = std::min(top, py);
                            bottom = std::max(bottom, py);
                        }
                    }
                }
                return (top + bottom) / 2.0;
            };
            const qreal iconCenter = inkCenter(chevron);
            for (const auto &suffix : {"title", "count"}) {
                auto *text = findVisualItem(panel, QString("galleryGroupHeader-gallery-alpha-%1").arg(suffix));
                const qreal textCenter = inkCenter(text);
                QVERIFY2(qAbs(textCenter - iconCenter) <= 1.0,
                    qPrintable(QString("%1 ink center %2, chevron %3 (physical pixels)")
                        .arg(suffix).arg(textCenter).arg(iconCenter)));
            }
        }
        if (checkRaster) {
            const QPointF origin = chevron->mapToItem(view.contentItem(), QPointF()) * dpr;
            const int extent = qRound(chevron->width() * dpr);
            const QColor background = panel->property("headerColor").value<QColor>();
            // A physical one-pixel checker must reach the framebuffer intact:
            // aligned wrappers alone cannot detect an oversized, resampled texture.
            for (int y = 0; y < extent; ++y) {
                for (int x = 0; x < extent; ++x) {
                    const QColor expected = (x + y) % 2 ? background : QColor(Qt::white);
                    QCOMPARE(capture.pixelColor(qRound(origin.x()) + x,
                                                qRound(origin.y()) + y), expected);
                }
            }
        }
        if (qEnvironmentVariableIsSet("F4_HEADER_PIXEL_CAPTURE"))
            QVERIFY(capture.save(qEnvironmentVariable("F4_HEADER_PIXEL_CAPTURE")));
    }

    void stickyGroupHeaderPinsSeparatorAtViewportTop() {
        QQuickView view;
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        auto *runtime = ZoinGallery::GalleryRuntime::install(view.engine(), options);
        auto *session = runtime->createExternalSession("sticky-header-top-edge");
        QVERIFY(session->applyExternalCatalog(plainCatalog(50), 1));
        auto *panel = qobject_cast<QQuickItem *>(createPanel(
            view, session, "stickyHeaderSession"));
        QVERIFY(panel);
        view.resize(640, 360);
        panel->setSize({640, 360});
        panel->setProperty("devicePixelRatio", view.devicePixelRatio());
        panel->setProperty("animateLayoutChanges", false);
        // The host's default gallery panel color is transparent. Keep that
        // case in this visual regression: a lighter transparent QColor is
        // still transparent, even though isolated Gallery tests use an
        // opaque theme by default.
        QVERIFY(setPanelObjectProperties(panel, "theme", QVariantMap{
            {QStringLiteral("panelBackground"),
             QColor(0, 0, 0, 0)},
            {QStringLiteral("controlHover"),
             QStringLiteral("#2a3745")},
        }));
        panel->setProperty("groupHeaderBackdropColor",
                           QColor(QStringLiteral("#191d23")));
        panel->setProperty("groupDescriptors", QVariantList{
            QVariantMap{{"key", "alpha"}, {"title", "Alpha"},
                        {"startIndex", 1}, {"count", 24}},
            QVariantMap{{"key", "beta"}, {"title", "Beta"},
                        {"startIndex", 25}, {"count", 25}},
        });
        auto *layout = panel->findChild<MasonryLayout *>("galleryViewportItem");
        QVERIFY(layout);
        QTRY_VERIFY_WITH_TIMEOUT(layout->contentHeight() > layout->height(), 3000);
        QVariantMap alpha;
        QTRY_VERIFY_WITH_TIMEOUT([&] {
            for (const QVariant &value : layout->visibleGroupHeaders()) {
                const QVariantMap header = value.toMap();
                if (header.value("key").toString() == QStringLiteral("alpha")) {
                    alpha = header;
                    return true;
                }
            }
            return false;
        }(), 3000);
        // Let the panel's initial viewport-placement transaction settle before
        // moving to the separator boundary under test.
        QTest::qWait(100);

        const qreal dpr = view.devicePixelRatio();
        const qreal physicalPixel = 1 / dpr;
        const qreal topSpacing = qRound(8 * dpr) / dpr;
        const qreal sectionOffset = alpha.value("offset").toReal();
        const qreal beforeSeparatorCrosses = sectionOffset + topSpacing
                                               - physicalPixel;
        layout->setContentY(beforeSeparatorCrosses);
        QTRY_VERIFY_WITH_TIMEOUT(qAbs(layout->contentY()
                                      - beforeSeparatorCrosses)
                                     < physicalPixel / 4,
                                 3000);

        const QString headerName = QStringLiteral(
            "galleryGroupHeader-gallery-alpha");
        auto *header = findVisualItem(panel, headerName);
        QTRY_VERIFY_WITH_TIMEOUT(header && header->isVisible(), 3000);
        QCOMPARE(header->property("backgroundColor").value<QColor>(),
                 panel->property("headerColor").value<QColor>());
        auto *separator = findVisualItem(
            panel, headerName + QStringLiteral("-separator"));
        QVERIFY(separator);
        QVERIFY2(!header->property("currentSticky").toBool(),
                 qPrintable(QStringLiteral(
                     "header pinned before its separator crossed the top edge: contentY=%1, section=%2")
                     .arg(layout->contentY(), 0, 'f', 3)
                     .arg(sectionOffset, 0, 'f', 3)));

        const qreal viewportTop = layout->mapToItem(
            view.contentItem(), QPointF()).y();
        const qreal lineBefore = separator->mapToItem(
            view.contentItem(), QPointF()).y();
        QVERIFY2(qAbs((lineBefore - viewportTop) * dpr - 1) < 0.01,
                 qPrintable(QStringLiteral("pre-sticky separator is at %1 physical px")
                     .arg((lineBefore - viewportTop) * dpr, 0, 'f', 3)));

        // The panel's startup restore can run after a queued layout commit.
        // Seed the session with the same target so that restore preserves the
        // boundary being inspected instead of snapping back to zero.
        session->setPanelScrollOffset(sectionOffset + topSpacing);
        layout->setContentY(sectionOffset + topSpacing);
        QTRY_VERIFY_WITH_TIMEOUT(qAbs(layout->contentY()
                                      - (sectionOffset + topSpacing))
                                     < physicalPixel / 4,
                                 3000);
        QTRY_VERIFY_WITH_TIMEOUT([&] {
            header = findVisualItem(panel, headerName);
            separator = findVisualItem(
                panel, headerName + QStringLiteral("-separator"));
            return header && separator
                && header->property("currentSticky").toBool();
        }(), 3000);
        const qreal pinnedTop = header->y();
        QVERIFY2(qAbs((pinnedTop + topSpacing) * dpr) < 0.01,
                 qPrintable(QStringLiteral("sticky header y=%1, separator offset=%2")
                     .arg(pinnedTop, 0, 'f', 3)
                     .arg(topSpacing, 0, 'f', 3)));
        QTRY_VERIFY_WITH_TIMEOUT(!separator->isVisible(), 3000);

        auto *background = header->findChild<QQuickItem *>(
            headerName + QStringLiteral("-background"));
        auto *hoverBackground = header->findChild<QQuickItem *>(
            headerName + QStringLiteral("-hover"));
        QVERIFY(background);
        QVERIFY2(!header->findChild<QObject *>(
                     headerName + QStringLiteral("-gradient-start")),
                 "sticky group header base background should be a solid fill, not a gradient");
        QVERIFY2(!header->findChild<QObject *>(
                     headerName + QStringLiteral("-gradient-end")),
                 "sticky group header base background should not fade to transparent");
        QVERIFY2(!header->findChild<QObject *>(
                     headerName + QStringLiteral("-hover-gradient-start")),
                 "sticky group header hover background should be a solid fill, not a gradient");
        QVERIFY2(!header->findChild<QObject *>(
                     headerName + QStringLiteral("-hover-gradient-end")),
                 "sticky group header hover background should not fade to transparent");
        QVERIFY(hoverBackground);
        const QColor backdropColor = QColor(QStringLiteral("#191d23"));
        const QColor baseBackgroundColor = backdropColor.lighter(120);
        const QColor headerHoverColor =
            panel->property("headerHoverColor").value<QColor>();
        QCOMPARE(header->property("backgroundColor").value<QColor>(),
                 baseBackgroundColor);
        QCOMPARE(header->property("backgroundColor")
                     .value<QColor>().alpha(), 255);
        QCOMPARE(background->property("color").value<QColor>(),
                 baseBackgroundColor);
        QCOMPARE(hoverBackground->property("color").value<QColor>(),
                 headerHoverColor);
        QCOMPARE(hoverBackground->y(), topSpacing);
        auto *title = findVisualItem(panel, headerName + QStringLiteral("-title"));
        auto *chevron = findVisualItem(panel, headerName + QStringLiteral("-chevron"));
        auto *count = findVisualItem(panel, headerName + QStringLiteral("-count"));
        QVERIFY(title);
        QVERIFY(chevron);
        QVERIFY(count);
        const qreal textBandBottom = std::max({
            title->mapToItem(header, QPointF(0, title->height())).y(),
            chevron->mapToItem(header, QPointF(0, chevron->height())).y(),
            count->mapToItem(header, QPointF(0, count->height())).y(),
        });
        QVERIFY2(header->height() >= textBandBottom,
                 "sticky header background must cover all header text");
        header->setVisible(false);
        view.requestUpdate();
        QTest::qWait(50);
        const QImage underlay = view.grabWindow();
        QVERIFY(!underlay.isNull());
        header->setVisible(true);
        panel->setProperty("hoverPointerInside", false);
        QTest::mouseMove(&view, QPoint(view.width() - 2, view.height() - 2));
        QVERIFY(!header->property("pointerHovered").toBool());
        QTRY_VERIFY_WITH_TIMEOUT(!hoverBackground->isVisible(), 3000);
        view.requestUpdate();
        QTest::qWait(50);
        QVERIFY(header->property("currentSticky").toBool());
        QVERIFY2(qAbs(layout->contentY() - (sectionOffset + topSpacing))
                     < physicalPixel / 4,
                 qPrintable(QStringLiteral(
                     "scroll reset before capture: contentY=%1 expected=%2")
                     .arg(layout->contentY(), 0, 'f', 3)
                     .arg(sectionOffset + topSpacing, 0, 'f', 3)));
        const QPointF headerOrigin = header->mapToItem(
            view.contentItem(), QPointF());
        const qreal clearSampleX = header->width() / 2;
        const qreal titleBandSampleY = title->mapToItem(
            header, QPointF(0, title->height() / 2)).y();
        const QImage baseCapture = view.grabWindow();
        QVERIFY(!baseCapture.isNull());
        if (qEnvironmentVariableIsSet(
                "F4_GROUP_HEADER_BASE_SOLID_CAPTURE"))
            QVERIFY(baseCapture.save(qEnvironmentVariable(
                "F4_GROUP_HEADER_BASE_SOLID_CAPTURE")));
        const auto sampleWindowPixelAt = [&](const QImage &image,
                                             qreal localX, qreal localY) {
            return image.pixelColor(
                qRound((headerOrigin.x() + localX) * dpr),
                qRound((headerOrigin.y() + localY) * dpr));
        };
        const auto colorDistance = [](const QColor &left,
                                      const QColor &right) {
            return qAbs(left.red() - right.red())
                + qAbs(left.green() - right.green())
                + qAbs(left.blue() - right.blue());
        };
        const auto renderedTextInkPixels = [&](QQuickItem *textItem) {
            const QRectF sceneInk = textItem->mapRectToItem(
                view.contentItem(),
                QRectF(0, 0, textItem->width(), textItem->height()));
            const int left = qMax(0, qFloor(sceneInk.left() * dpr));
            const int top = qMax(0, qFloor(sceneInk.top() * dpr));
            const int right = qMin(baseCapture.width(),
                                   qCeil(sceneInk.right() * dpr));
            const int bottom = qMin(baseCapture.height(),
                                    qCeil(sceneInk.bottom() * dpr));
            int pixels = 0;
            for (int y = top; y < bottom; ++y) {
                for (int x = left; x < right; ++x) {
                    if (colorDistance(baseCapture.pixelColor(x, y),
                                      baseBackgroundColor) > 80)
                        ++pixels;
                }
            }
            return pixels;
        };
        const auto renderedTargetColorPixels = [&](QQuickItem *textItem) {
            const QColor target = textItem->property("color").value<QColor>();
            const QRectF sceneBounds = textItem->mapRectToItem(
                view.contentItem(),
                QRectF(0, 0, textItem->width(), textItem->height()));
            const int left = qMax(0, qFloor(sceneBounds.left() * dpr));
            const int top = qMax(0, qFloor(sceneBounds.top() * dpr));
            const int right = qMin(baseCapture.width(),
                                   qCeil(sceneBounds.right() * dpr));
            const int bottom = qMin(baseCapture.height(),
                                    qCeil(sceneBounds.bottom() * dpr));
            int pixels = 0;
            for (int y = top; y < bottom; ++y) {
                for (int x = left; x < right; ++x) {
                    if (colorDistance(baseCapture.pixelColor(x, y), target) < 80)
                        ++pixels;
                }
            }
            return pixels;
        };
        const int titleInkPixels = renderedTextInkPixels(title);
        QVERIFY2(titleInkPixels > 0,
                 qPrintable(QStringLiteral(
                     "sticky group title is missing from the no-hover render: visible=%1 text=%2 bounds=%3x%4 color=%5")
                     .arg(title->isVisible())
                     .arg(title->property("text").toString())
                     .arg(title->width()).arg(title->height())
                     .arg(title->property("color").value<QColor>().name())));
        QVERIFY2(renderedTargetColorPixels(title) > 0,
                 "sticky group title color is missing from the no-hover render");
        QVERIFY2(renderedTextInkPixels(count) > 0,
                 "sticky group count is missing from the no-hover render");
        const QColor baseTextBand = sampleWindowPixelAt(
            baseCapture, clearSampleX, titleBandSampleY);
        QVERIFY2(colorDistance(baseTextBand, baseBackgroundColor) < 12,
                 qPrintable(QStringLiteral(
                     "sticky background does not cover content behind its text band: %1 vs %2")
                     .arg(baseTextBand.name(), baseBackgroundColor.name())));
        for (const qreal sampleY : {
                 topSpacing + 2 / dpr,
                 titleBandSampleY,
                 header->height() / 2,
                 header->height() - 2 / dpr,
             }) {
            const QColor rendered = sampleWindowPixelAt(
                baseCapture, clearSampleX, sampleY);
            const QColor obscured = sampleWindowPixelAt(
                underlay, clearSampleX, sampleY);
            QVERIFY2(colorDistance(rendered, baseBackgroundColor) < 12,
                     qPrintable(QStringLiteral(
                         "sticky background is not solid at y=%1: %2 vs %3")
                         .arg(sampleY, 0, 'f', 3)
                         .arg(rendered.name(), baseBackgroundColor.name())));
            QVERIFY2(colorDistance(rendered, obscured) >= 12,
                     qPrintable(QStringLiteral(
                         "sticky background lets underlying content show at y=%1: %2 vs %3")
                         .arg(sampleY, 0, 'f', 3)
                         .arg(rendered.name(), obscured.name())));
        }
        QTest::mouseMove(&view, header->mapToItem(
            view.contentItem(), QPointF(clearSampleX, header->height() / 2))
                                     .toPoint());
        QTRY_VERIFY_WITH_TIMEOUT(hoverBackground->isVisible(), 3000);
        view.requestUpdate();
        QTest::qWait(50);
        const QImage hoverCapture = view.grabWindow();
        QVERIFY(!hoverCapture.isNull());
        for (const qreal sampleY : {
                 topSpacing + 2 / dpr,
                 header->height() / 2,
                 header->height() - 2 / dpr,
             }) {
            const QColor rendered = sampleWindowPixelAt(
                hoverCapture, clearSampleX, sampleY);
            QVERIFY2(colorDistance(rendered, headerHoverColor) < 12,
                     qPrintable(QStringLiteral(
                         "sticky hover fill is not solid at y=%1: %2 vs %3")
                         .arg(sampleY, 0, 'f', 3)
                         .arg(rendered.name(), headerHoverColor.name())));
        }
    }

    void destroyedPreviewModelDetachesLayout() {
        MasonryLayout layout;
        layout.setContainedPreview(true);
        auto *model = new QStandardItemModel;
        layout.setModel(model);
        QSignalSpy changed(&layout, &MasonryLayout::modelChanged);
        delete model;
        QCOMPARE(layout.model(), nullptr);
        QCOMPARE(layout.count(), 0);
        QCOMPARE(changed.size(), 1);
        QTest::qWait(40);
    }

    void folderPreviewKnownAppearanceInFirstFrame() {
        QTemporaryDir dir;
        const QString path = dir.filePath("photo.png");
        QImage image(80, 60, QImage::Format_RGB32);
        image.fill(Qt::green);
        QVERIFY(image.save(path));
        QQuickView view;
        auto provider = QSharedPointer<DirectoryPreviewFixture>::create();
        provider->imagePath = path;
        provider->names = {"photo.png"};
        const auto unblock = qScopeGuard([&] { provider->block = false; });
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        options.directoryPreviewProvider = provider;
        auto *runtime = ZoinGallery::GalleryRuntime::install(view.engine(), options);
        auto *session = runtime->createExternalSession("folder-first-frame");
        QVariantList folders;
        for (int i = 0; i < 38; ++i) folders.append(previewFolder(i));
        QVERIFY(session->applyExternalCatalog(folders, 1, {{"currentPath", "/year"}}));
        auto *panel = qobject_cast<QQuickItem *>(createPanel(view, session, "firstFrameSession"));
        QVERIFY(panel);
        panel->setSize({1400, 1200});
        view.resize(1400, 1200);
        panel->setProperty("devicePixelRatio", view.devicePixelRatio());
        panel->setProperty("listView", false);
        const auto appearanceCount = [&] {
            int count = 0;
            for (int row = 0; row < 38; ++row) {
                auto *preview = findVisualItem(panel,
                    QStringLiteral("galleryFolderPreview-%1").arg(row));
                if (preview && preview->isVisible()
                    && preview->property("hasUsablePreview").toBool()) ++count;
            }
            return count;
        };
        QTRY_COMPARE_WITH_TIMEOUT(appearanceCount(), 38, 10000);
        QTest::qWait(100);
        QVERIFY(session->applyExternalCatalog({}, 2, {{"currentPath", "/other"}}));
        QTest::qWait(50);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        provider->block = true;
        int firstFrameCount = -1;
        int firstFrameFacades = -1;
        qint64 firstFrameNs = 0;
        QElapsedTimer firstFrameTimer;
        QList<qint64> firstFrameTimes;
        auto *layout = panel->findChild<MasonryLayout *>("galleryViewportItem");
        QVERIFY(layout);
        const auto connection = connect(&view, &QQuickWindow::beforeSynchronizing,
            &view, [&] {
                if (firstFrameCount >= 0 || layout->count() != 38) return;
                firstFrameNs = firstFrameTimer.nsecsElapsed();
                firstFrameCount = appearanceCount();
                firstFrameFacades = 0;
                for (int row = 0; row < 38; ++row) {
                    auto *item = layout->itemForIndex(row);
                    if (item && item->property("model").value<QObject *>())
                        ++firstFrameFacades;
                }
            }, Qt::DirectConnection);
        const auto disconnectFrame = qScopeGuard([&] { disconnect(connection); });
        for (int iteration = 0; iteration < 20; ++iteration) {
            firstFrameCount = firstFrameFacades = -1;
            firstFrameTimer.start();
            QVERIFY(session->applyExternalCatalog(folders, iteration * 2 + 3, {{"currentPath", "/year"}}));
            view.update();
            QTRY_VERIFY_WITH_TIMEOUT(firstFrameCount >= 0, 2000);
            qInfo() << "first frame" << iteration << ": folder appearances" << firstFrameCount
                    << "of 38; ImageFile facades" << firstFrameFacades << "ns" << firstFrameNs;
            QCOMPARE(firstFrameFacades, 0);
            QCOMPARE(firstFrameCount, 38);
            firstFrameTimes.append(firstFrameNs);
            QVERIFY(session->applyExternalCatalog({}, iteration * 2 + 4, {{"currentPath", "/other"}}));
            QTest::qWait(30);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        }
        std::sort(firstFrameTimes.begin(), firstFrameTimes.end());
        qInfo() << "38-folder first frame p95 ns" << firstFrameTimes[18];
        QVERIFY(firstFrameTimes[18] <= 50000000);
    }

    void folderPreviewReusesLightFrameAcrossImageNavigation() {
        QTemporaryDir dir;
        const QString path = dir.filePath("photo.png");
        QImage image(80, 60, QImage::Format_RGB32);
        image.fill(Qt::green);
        QVERIFY(image.save(path));
        QQuickView view;
        auto provider = QSharedPointer<DirectoryPreviewFixture>::create();
        provider->imagePath = path;
        provider->names = {"photo.png"};
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        options.directoryPreviewProvider = provider;
        auto *runtime = ZoinGallery::GalleryRuntime::install(view.engine(), options);
        auto *session = runtime->createExternalSession("folder-recycled-frame");
        QVERIFY(session->applyExternalCatalog({previewFolder(0)}, 1, {{"currentPath", "/folders"}}));
        auto *panel = qobject_cast<QQuickItem *>(createPanel(view, session, "recycledFrameSession"));
        QVERIFY(panel);
        panel->setProperty("listView", false);
        QQuickItem *preview = nullptr;
        QTRY_VERIFY((preview = findVisualItem(panel, "galleryFolderPreview-0")));
        QPointer<QQuickItem> retainedFrame = preview;
        QPointer<MasonryLayout> grid;
        QTRY_VERIFY((grid = preview->findChild<MasonryLayout *>("folderPreviewGrid")));
        QVERIFY(session->applyExternalCatalog({catalogEntry(0, path)}, 2, {{"currentPath", "/photos"}}));
        QTest::qWait(60);
        QVERIFY2(retainedFrame, "Recycled delegates must retain their lightweight folder frame");
        QVERIFY(!retainedFrame->isVisible());
        QTRY_VERIFY(!grid);
        auto *layout = panel->findChild<MasonryLayout *>("galleryViewportItem");
        QVERIFY(layout);
        auto *tile = layout->itemForIndex(0);
        QVERIFY(tile);
        view.engine()->rootContext()->setContextProperty("recycledFolderTile", tile);
        QQmlComponent observerComponent(view.engine());
        observerComponent.setData(R"QML(
            import QtQuick
            QtObject {
                id: observer
                property size activatedSize: Qt.size(0, 0)
                property Connections connection: Connections {
                    target: recycledFolderTile
                    function onFolderPreviewActiveChanged() {
                        if (recycledFolderTile.folderPreviewActive)
                            observer.activatedSize = Qt.size(recycledFolderTile.width, recycledFolderTile.height)
                    }
                }
            }
        )QML", QUrl("inline:FolderActivationObserver.qml"));
        QScopedPointer<QObject> observer(observerComponent.create());
        QVERIFY2(observer, qPrintable(observerComponent.errorString()));
        QVERIFY(session->applyExternalCatalog({previewFolder(0, 3)}, 3, {{"currentPath", "/folders"}}));
        QTRY_VERIFY(retainedFrame->isVisible());
        QCOMPARE(findVisualItem(panel, "galleryFolderPreview-0"), retainedFrame.data());
        QCOMPARE(observer->property("activatedSize").toSizeF(), tile->size());
    }

    void folderPreviewRepeatedNavigation() {
        QTemporaryDir dir;
        const QString path = dir.filePath("photo.png");
        QImage image(80, 60, QImage::Format_RGB32);
        image.fill(Qt::green);
        QVERIFY(image.save(path));
        QQuickView view;
        auto provider = QSharedPointer<DirectoryPreviewFixture>::create();
        provider->imagePath = path;
        provider->names = {"photo.png"};
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        options.directoryPreviewProvider = provider;
        auto *runtime = ZoinGallery::GalleryRuntime::install(view.engine(), options);
        auto *session = runtime->createExternalSession("folder-navigation");
        QVariantList folders;
        for (int i = 0; i < 38; ++i) folders.append(previewFolder(i));
        QVariantList parentFolders;
        for (int i = 0; i < 20; ++i) {
            auto folder = previewFolder(i + 100);
            folder["index"] = i;
            parentFolders.append(folder);
        }
        QVERIFY(session->applyExternalCatalog(folders, 1, {{"currentPath", "/year"}}));
        auto *panel = qobject_cast<QQuickItem *>(createPanel(view, session, "folderNavigationSession"));
        QVERIFY(panel);
        panel->setProperty("presentationMode", "masonry");
        panel->setProperty("listView", false);
        for (int iteration = 0; iteration < 20; ++iteration) {
            QTRY_VERIFY_WITH_TIMEOUT(findVisualItem(panel, "galleryFolderPreview-0"), 5000);
            QTRY_VERIFY_WITH_TIMEOUT(findVisualItem(panel, "galleryFolderPreview-0")->property("hasUsablePreview").toBool(), 5000);
            QTest::qWait(80);
            QVERIFY(session->applyExternalCatalog(parentFolders, iteration * 2 + 2, {{"currentPath", "/parent"}}));
            QTest::qWait(80);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QVERIFY(session->applyExternalCatalog(folders, iteration * 2 + 3, {{"currentPath", "/year"}}));
        }
        QVERIFY(session->applyExternalCatalog({}, 42, {{"currentPath", "/empty"}}));
        QTest::qWait(50);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCOMPARE(provider->leases.load(), 0);
    }

    void folderPreviewsSurviveGroupingChangeWithoutRegeneration() {
        QQuickView view;
        auto provider = QSharedPointer<DirectoryPreviewFixture>::create();
        provider->names = {"photo.jpg"};
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        options.directoryPreviewProvider = provider;
        auto *runtime = ZoinGallery::GalleryRuntime::install(
            view.engine(), options);
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(
            QStringLiteral("folder-grouping-change"));
        QVERIFY(session);

        QVariantList catalog;
        catalog.append(QVariantMap{
            {QStringLiteral("entryId"), QStringLiteral("parent")},
            {QStringLiteral("index"), 0},
            {QStringLiteral("name"), QStringLiteral("..")},
            {QStringLiteral("isDir"), true},
            {QStringLiteral("isImage"), false},
        });
        for (int index = 0; index < 12; ++index) {
            auto folder = previewFolder(index);
            folder[QStringLiteral("index")] = index + 1;
            catalog.append(folder);
        }
        QVERIFY(session->applyExternalCatalog(
            catalog, 1, {{QStringLiteral("currentPath"),
                          QStringLiteral("/year")}}));

        auto *model = qobject_cast<ZoinGallery::ExternalCatalogModel *>(
            session->model());
        QVERIFY(model);
        model->setDirectoryCacheMode(1);
        auto *panel = qobject_cast<QQuickItem *>(createPanel(
            view, session, QStringLiteral("folderGroupingSession")));
        QVERIFY(panel);
        auto *layout = panel->findChild<MasonryLayout *>(
            QStringLiteral("galleryViewportItem"));
        QVERIFY(layout);
        panel->setSize({1400, 1200});
        view.resize(1400, 1200);
        panel->setProperty("listView", false);

        const auto appearanceCount = [&] {
            int count = 0;
            for (int row = 1; row <= 12; ++row) {
                auto *preview = findVisualItem(
                    panel, QStringLiteral("galleryFolderPreview-%1").arg(row));
                if (preview && preview->isVisible()
                    && preview->property("hasUsablePreview").toBool()) {
                    ++count;
                }
            }
            return count;
        };
        QTRY_COMPARE_WITH_TIMEOUT(appearanceCount(), 12, 10000);
        const int enumerations = provider->enumerations.load();
        QVERIFY(enumerations > 0);

        QCOMPARE(provider->enumerations.load(), enumerations);

        const QVariantList groups{
            QVariantMap{
                {QStringLiteral("key"), QStringLiteral("no-data")},
                {QStringLiteral("title"), QStringLiteral("No data")},
                {QStringLiteral("startIndex"), 1},
                {QStringLiteral("count"), 12},
            },
        };
        QVariantList reordered;
        reordered.append(catalog.first());
        for (int index = 11; index >= 0; --index) {
            auto folder = catalog.at(index + 1).toMap();
            folder[QStringLiteral("index")] = 12 - index;
            reordered.append(folder);
        }
        QVERIFY(session->applyExternalCatalog(
            reordered, 2, {{QStringLiteral("currentPath"),
                            QStringLiteral("/year")}}));
        QVERIFY(panel->setProperty("groupDescriptors", groups));
        QVERIFY(panel->setProperty(
            "groupStateKey", QStringLiteral(
                "side=0|path=/year|mode=Modified")));
        QTRY_COMPARE_WITH_TIMEOUT(appearanceCount(), 12, 10000);
        QCOMPARE(provider->enumerations.load(), enumerations);
    }

    void folderPreviewsSurviveLayoutRewrapWithoutRegeneration() {
        QQuickView view;
        auto provider = QSharedPointer<DirectoryPreviewFixture>::create();
        provider->names = {"photo.jpg"};
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        options.directoryPreviewProvider = provider;
        auto *runtime = ZoinGallery::GalleryRuntime::install(
            view.engine(), options);
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(
            QStringLiteral("folder-layout-rewrap"));
        QVERIFY(session);
        QVariantList catalog;
        for (int index = 0; index < 12; ++index) {
            auto folder = previewFolder(index);
            folder[QStringLiteral("index")] = index;
            catalog.append(folder);
        }
        QVERIFY(session->applyExternalCatalog(
            catalog, 1, {{QStringLiteral("currentPath"),
                          QStringLiteral("/year")}}));
        auto *model = qobject_cast<ZoinGallery::ExternalCatalogModel *>(
            session->model());
        QVERIFY(model);
        // This is the configuration in which a transient empty demand would
        // retire the folder models instead of merely preserving them across
        // a parent-layout rewrap.
        model->setDirectoryCacheMode(0);
        auto *panel = qobject_cast<QQuickItem *>(createPanel(
            view, session, QStringLiteral("folderLayoutRewrapSession")));
        QVERIFY(panel);
        panel->setSize({1400, 1200});
        view.resize(1400, 1200);
        panel->setProperty("listView", false);
        auto *layout = panel->findChild<MasonryLayout *>(
            QStringLiteral("galleryViewportItem"));
        QVERIFY(layout);

        const auto appearanceCount = [&] {
            int count = 0;
            for (int row = 0; row < 12; ++row) {
                auto *preview = findVisualItem(
                    panel, QStringLiteral("galleryFolderPreview-%1").arg(row));
                if (preview && preview->isVisible()
                    && preview->property("hasUsablePreview").toBool()) {
                    ++count;
                }
            }
            return count;
        };
        QTRY_COMPARE_WITH_TIMEOUT(appearanceCount(), 12, 10000);
        const int enumerations = provider->enumerations.load();

        // A hidden intermediate layout is equivalent to the disposable
        // geometry pass used while the host applies a presentation update.
        // It must not be interpreted as an explicit preview cancellation.
        layout->setVisible(false);
        layout->setWidth(layout->width() + 1);
        layout->setWidth(layout->width() - 1);
        layout->setVisible(true);
        QTRY_COMPARE_WITH_TIMEOUT(appearanceCount(), 12, 10000);
        QCOMPARE(provider->enumerations.load(), enumerations);
    }

    void folderPreviewIconsToGrid_data() {
        QTest::addColumn<qreal>("iconDensity");
        QTest::newRow("one-cell") << qreal(70);
        QTest::newRow("four-cells") << qreal(160);
    }

    void folderPreviewIconsToGrid() {
        QFETCH(qreal, iconDensity);
        QTemporaryDir dir;
        QImage image(80, 60, QImage::Format_RGB32);
        image.fill(Qt::green);
        auto provider = QSharedPointer<DirectoryPreviewFixture>::create();
        provider->imagePath = dir.path();
        for (int i = 0; i < 16; ++i) {
            const auto name = QStringLiteral("photo%1.png").arg(i);
            provider->names.append(name);
            QVERIFY(image.save(dir.filePath(name)));
        }
        QQuickView view;
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        options.directoryPreviewProvider = provider;
        auto *runtime = ZoinGallery::GalleryRuntime::install(view.engine(), options);
        auto *session = runtime->createExternalSession("folder-mode-transition");
        QVariantList folders;
        for (int i = 0; i < 38; ++i) folders.append(previewFolder(i));
        QVERIFY(session->applyExternalCatalog(folders, 1));
        auto *panel = qobject_cast<QQuickItem *>(createPanel(view, session, "folderTransitionSession"));
        QVERIFY(panel);
        panel->setProperty("listView", false);
        panel->setProperty("devicePixelRatio", view.devicePixelRatio());
        for (const auto &mode : {"icons", "grid", "icons", "grid"}) {
            panel->setProperty("presentationMode", mode);
            if (QString::fromLatin1(mode) == "icons")
                panel->findChild<MasonryLayout *>("galleryViewportItem")->setDensity(iconDensity);
            QTest::qWait(500);
            for (int row = 0; row < 6; ++row) {
                QQuickItem *preview = nullptr;
                QTRY_VERIFY((preview = findVisualItem(panel, QStringLiteral("galleryFolderPreview-%1").arg(row))));
                MasonryLayout *grid = nullptr;
                QTRY_VERIFY((grid = preview->findChild<MasonryLayout *>("folderPreviewGrid")));
                const int expected = grid->width() < 80 ? 1 : grid->width() < 150 ? 4 : grid->width() < 300 ? 9 : 16;
                const auto readyCount = [&]() {
                    int ready = 0;
                    for (int i = 0; i < 16; ++i) {
                        auto *leaf = findVisualItem(preview, QStringLiteral("folderPreviewImage-%1").arg(i));
                        if (leaf && leaf->isVisible() && leaf->width() > 0 && leaf->height() > 0
                            && leaf->property("status").toInt() == 1) ++ready;
                    }
                    return ready;
                };
                qInfo() << mode << row << grid->size() << "ready" << readyCount() << "expected" << expected;
                if (readyCount() != expected) {
                    for (int i = 0; i < expected; ++i) {
                        auto *leaf = findVisualItem(preview, QStringLiteral("folderPreviewImage-%1").arg(i));
                        qInfo() << i << grid->indexGeometry(i) << grid->indexOriginalSize(i)
                                << "leaf" << leaf << (leaf ? leaf->property("source") : QVariant())
                                << (leaf ? leaf->isVisible() : false);
                    }
                }
                QTRY_COMPARE_WITH_TIMEOUT(readyCount(), expected, 3000);
            }
        }
    }

    void folderPreviewGridAndPixels_data() {
        QTest::addColumn<QString>("mode");
        for (const auto &mode : {"masonry", "grid", "icons"})
            QTest::newRow(mode) << QString::fromLatin1(mode);
    }

    void folderPreviewGridAndPixels() {
        QFETCH(QString, mode);
        QTemporaryDir dir;
        if (QFileInfo::exists("C:/Windows/Fonts/segoeui.ttf"))
            QVERIFY(QFontDatabase::addApplicationFont("C:/Windows/Fonts/segoeui.ttf") >= 0);
        QQuickView view;
        auto provider = QSharedPointer<DirectoryPreviewFixture>::create();
        provider->imagePath = dir.path();
        for (int i = 0; i < 16; ++i) {
            const QString name = QStringLiteral("photo%1.png").arg(i);
            provider->names.append(name);
            QImage image(i % 3 == 1 ? 41 : 73, i % 3 == 0 ? 41 : 73, QImage::Format_RGB32);
            image.fill(QColor::fromHsv(i * 23, 130, 210));
            for (int x = 0; x < image.width(); ++x)
                for (int y = 0; y < image.height(); ++y)
                    if ((x / 10 + y / 10) % 2 == 0) image.setPixelColor(x, y, image.pixelColor(x, y).lighter(120));
            QVERIFY(image.save(dir.filePath(name)));
        }
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        options.directoryPreviewProvider = provider;
        auto *runtime = ZoinGallery::GalleryRuntime::install(view.engine(), options);
        auto *session = runtime->createExternalSession("folder-pixels");
        QVERIFY(session->applyExternalCatalog({previewFolder(0)}, 1));
        auto *panel = qobject_cast<QQuickItem *>(createPanel(view, session, "folderPixelSession"));
        QVERIFY(panel);
        panel->setProperty("presentationMode", mode);
        panel->setProperty("listView", false);
        panel->setProperty("devicePixelRatio", view.devicePixelRatio());
        QQuickItem *preview = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((preview = findVisualItem(panel, "galleryFolderPreview-0")), 5000);
        MasonryLayout *grid = nullptr;
        QTRY_VERIFY((grid = preview->findChild<MasonryLayout *>("folderPreviewGrid")));
        QVERIFY(grid->property("containedPreview").toBool());
        QTRY_COMPARE(grid->count(), 16);
        QTest::qWait(200);
        auto *childCatalog = qobject_cast<ZoinGallery::ExternalCatalogModel *>(grid->model());
        QVERIFY(childCatalog);
        // Contained cards have independent geometry. They must not start the
        // top-level catalog-wide probe/fit pass for the twelve hidden samples.
        QVERIFY(childCatalog->imageOriginalSizeAt(15).isEmpty());
        auto *title = findVisualItem(preview, "folderPreviewTitle");
        QVERIFY(title);
        title->setProperty("font", QFont("Segoe UI", 9));
        title->setProperty("text", QStringLiteral("Folder with a long filename that wraps onto two lines"));
        QTest::qWait(100);
        QCOMPARE(title->property("lineCount").toInt(), 2);
        const QSizeF normalGridSize = grid->size();
        auto *outerLayout = panel->findChild<MasonryLayout *>("galleryViewportItem");
        const QRectF outer = outerLayout->itemForIndex(0)->boundingRect();
        qInfo() << "outer folder geometry" << outer;
        if (mode == "masonry")
            QVERIFY(qAbs(outer.width() - outer.height()) < 2);
        const auto checkPoint = [&](QQuickItem *leaf) {
            const QPointF origin = leaf->mapToItem(view.contentItem(), QPointF());
            const qreal dpr = view.devicePixelRatio();
            qInfo() << leaf->objectName() << "physical origin" << origin * dpr;
            QVERIFY(qAbs(origin.x() * dpr - qRound(origin.x() * dpr)) < 0.01);
            QVERIFY(qAbs(origin.y() * dpr - qRound(origin.y() * dpr)) < 0.01);
            QCOMPARE(leaf->mapToItem(view.contentItem(), QPointF(1, 0)) - origin, QPointF(1, 0));
            QCOMPARE(leaf->mapToItem(view.contentItem(), QPointF(0, 1)) - origin, QPointF(0, 1));
        };
        for (int width : {79, 80, 149, 150, 299, 300}) {
            // The lazy Loader owns the grid's final geometry.
            grid->parentItem()->setSize({qreal(width), qreal(width)});
            grid->setTargetHeight(width);
            const int cells = width < 80 ? 1 : width < 150 ? 4 : width < 300 ? 9 : 16;
            QTest::qWait(150);
            int displayed = 0;
            for (int i = 0; i < 16; ++i) {
                auto *leaf = findVisualItem(preview, QStringLiteral("folderPreviewImage-%1").arg(i));
                if (!leaf || !leaf->isVisible() || leaf->width() <= 0) continue;
                ++displayed;
                QTRY_COMPARE_WITH_TIMEOUT(leaf->property("status").toInt(), 1, 3000);
                checkPoint(leaf);
                const QSize original = grid->indexOriginalSize(i);
                QVERIFY(original.width() > 1 && original.height() > 1);
                const QRectF geometry = grid->indexGeometry(i);
                QVERIFY(qAbs(geometry.width() / geometry.height() - qreal(original.width()) / original.height()) < .05);
            }
            QCOMPARE(displayed, cells);
        }
        QCOMPARE(title->property("horizontalAlignment").toInt(), int(Qt::AlignHCenter));
        QCOMPARE(title->property("maximumLineCount").toInt(), 2);
        QCOMPARE(title->property("elide").toInt(), int(Qt::ElideRight));
        QCOMPARE(title->property("wrapMode").toInt(), 4); // Text.Wrap
        auto *folderFill = findVisualItem(preview, "folderPreviewFill");
        QVERIFY(folderFill);
        QCOMPARE(folderFill->property("color").value<QColor>(), QColor("#304051"));
        checkPoint(folderFill);
        checkPoint(title);
        for (const QString name : {"folderPreviewFrame", "folderPreviewTab"}) {
            auto *frame = findVisualItem(preview, name);
            QVERIFY(frame);
            checkPoint(frame);
            const qreal dpr = view.devicePixelRatio();
            QVERIFY(qAbs(frame->width()*dpr - qRound(frame->width()*dpr)) < 0.01);
            QVERIFY(qAbs(frame->height()*dpr - qRound(frame->height()*dpr)) < 0.01);
            QCOMPARE(frame->property("color").value<QColor>(), QColor("#397db1"));
        }
        grid->parentItem()->setSize(normalGridSize);
        grid->setTargetHeight(qRound(normalGridSize.height()));
        QTest::qWait(300);
        QVERIFY(grid->mapToItem(preview, QPointF(0, grid->height())).y()
                < title->mapToItem(preview, QPointF()).y());
        QCOMPARE(outerLayout->itemForIndex(0)->boundingRect(), outer);
        const auto capture = view.grabWindow();
        QVERIFY(!capture.isNull());
        QVERIFY(capture.save(dir.filePath("folder-preview.png")));
        if (!qEnvironmentVariable("F4_FOLDER_PREVIEW_CAPTURE").isEmpty())
            QVERIFY(capture.save(qEnvironmentVariable("F4_FOLDER_PREVIEW_CAPTURE") + "-" + mode + ".png"));
        for (const auto &nextMode : {"details", "grid", "icons", "masonry"}) {
            panel->setProperty("presentationMode", nextMode);
            if (QString::fromLatin1(nextMode) == "details") {
                QTRY_VERIFY(!findVisualItem(panel, "galleryFolderPreview-0")
                    || !findVisualItem(panel, "galleryFolderPreview-0")->isVisible());
            } else {
                QTRY_VERIFY((preview = findVisualItem(panel, "galleryFolderPreview-0")));
                QTRY_VERIFY(preview->property("hasUsablePreview").toBool());
                checkPoint(findVisualItem(preview, "folderPreviewTitle"));
            }
        }
    }

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
        auto *panelItem = qobject_cast<QQuickItem *>(panel);
        QVERIFY(panelItem);
        auto *layout = panel->findChild<MasonryLayout *>(
            QStringLiteral("galleryViewportItem"));
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
        QVERIFY(setPanelObjectProperties(panel, "theme", firstTheme));

        QQuickItem *cursorSurface = nullptr;
        QQuickItem *plainSurface = nullptr;
        QQuickItem *previewBackdrop = nullptr;
        QQuickItem *scrollBar = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT(
            (cursorSurface = findVisualItem(
                 panelItem,
                 QStringLiteral("gallerySelectionSurface-1"))),
            3000);
        QTRY_VERIFY_WITH_TIMEOUT(
            (plainSurface = findVisualItem(
                 panelItem,
                 QStringLiteral("gallerySelectionSurface-0"))),
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

        auto *plainBrick = plainSurface->parentItem();
        auto *hoverPointer = plainBrick
            ? plainBrick->findChild<QQuickItem *>(
                  QStringLiteral("gallerySelectionSurface-0"))
            : nullptr;
        QVERIFY(plainBrick && hoverPointer);
        QVERIFY(!hoverPointer->property("hoverEnabled").toBool());
        QVERIFY(panel->setProperty("hoveredIndex", 0));
        QTRY_COMPARE_WITH_TIMEOUT(
            plainSurface->property("color").value<QColor>(),
            QColor(QStringLiteral("#707172")), 3000);

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
        QVERIFY(setPanelObjectProperties(panel, "theme", secondTheme));

        // A hovered brick must keep observing the live semantic color rather
        // than retaining the value captured when its delegate was created.
        QTRY_COMPARE_WITH_TIMEOUT(
            plainSurface->property("color").value<QColor>(),
            QColor(QStringLiteral("#717273")), 3000);
        QVERIFY(panel->setProperty("hoveredIndex", -1));
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
        QCOMPARE(findVisualItem(
                     panelItem,
                     QStringLiteral("gallerySelectionSurface-1")),
                 cursorSurface);
        QCOMPARE(panel->findChild<QQuickItem *>(
                     QStringLiteral("galleryPanelScrollBar")),
                 scrollBar);

        // Hover belongs to the shared BrickItem surface. Verify every
        // presentation, including the compact Details rows and Icons mode
        // that historically bypassed this state entirely.
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
            QQuickItem *surface = nullptr;
            QTRY_VERIFY_WITH_TIMEOUT(
                (surface = findVisualItem(
                    panelItem,
                    QStringLiteral("gallerySelectionSurface-0"))),
                3000);
            QQuickItem *brick = surface->parentItem();
            QVERIFY(brick);
            QVERIFY(panel->setProperty("hoveredIndex", 0));
            QTRY_COMPARE_WITH_TIMEOUT(
                surface->property("color").value<QColor>(),
                QColor(QStringLiteral("#717273")), 3000);
            QVERIFY(panel->setProperty("hoveredIndex", -1));
        }
    }

    void neutralPanelTextColorsLeaveSemanticIconColorsAlone() {
        QVariantMap folder = catalogEntry(0);
        folder[QStringLiteral("name")] = QStringLiteral("folder");
        folder[QStringLiteral("isDir")] = true;
        folder[QStringLiteral("highlightStyle")] = QVariantMap{
            {QStringLiteral("normal"), QVariantMap{
                 {QStringLiteral("foreground"), QStringLiteral("#ff00ff")},
             }},
        };
        QVariantMap file = catalogEntry(1);
        file[QStringLiteral("name")] = QStringLiteral("file.txt");
        file[QStringLiteral("highlightStyle")] = QVariantMap{
            {QStringLiteral("normal"), QVariantMap{
                 {QStringLiteral("foreground"), QStringLiteral("#00ff00")},
             }},
        };

        QQuickView view;
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        auto *runtime = ZoinGallery::GalleryRuntime::install(
            view.engine(), options);
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(
            QStringLiteral("neutral-panel-text-colors"));
        QVERIFY(session);
        QVERIFY(session->applyExternalCatalog(QVariantList{folder, file}, 1));

        QObject *panel = createPanel(
            view, session, QStringLiteral("neutralPanelTextSession"),
            QStringLiteral("details"));
        QVERIFY(panel);
        panel->setProperty("showCursor", false);
        QVERIFY(setPanelObjectProperties(panel, "theme", QVariantMap{
            {QStringLiteral("text"), QStringLiteral("#222222")},
            {QStringLiteral("mutedText"), QStringLiteral("#333333")},
            {QStringLiteral("fileText"), QStringLiteral("#c4cbd3")},
            {QStringLiteral("folderText"), QStringLiteral("#ffffff")},
            {QStringLiteral("neutralFileTextColors"), true},
            {QStringLiteral("folderIcon"), QStringLiteral("#5ab2f1")},
        }));

        const auto findItem = [panel](const QString &name) {
            return panel->findChild<QQuickItem *>(name);
        };
        QTRY_VERIFY_WITH_TIMEOUT(
            findItem(QStringLiteral("galleryBaseName-0"))
            && findItem(QStringLiteral("galleryBaseName-1")), 3000);
        auto *folderText = findItem(QStringLiteral("galleryBaseName-0"));
        auto *fileText = findItem(QStringLiteral("galleryBaseName-1"));
        auto *folderIcon = findItem(QStringLiteral("galleryFallbackIcon-0"));
        auto *fileIcon = findItem(QStringLiteral("galleryFallbackIcon-1"));
        QVERIFY(folderText && fileText && folderIcon && fileIcon);

        QCOMPARE(folderText->property("color").value<QColor>(),
                 QColor(QStringLiteral("#ffffff")));
        QCOMPARE(fileText->property("color").value<QColor>(),
                 QColor(QStringLiteral("#c4cbd3")));
        const QColor fileIconColor = fileIcon->property(
            "effectiveIconColor").value<QColor>();
        QCOMPARE(fileIconColor, QColor(QStringLiteral("#00ff00")));
        QCOMPARE(folderIcon->property("effectiveIconColor").value<QColor>(),
                 QColor(QStringLiteral("#5ab2f1")));

        QVERIFY(setPanelObjectProperties(panel, "theme", QVariantMap{
            {QStringLiteral("text"), QStringLiteral("#222222")},
            {QStringLiteral("mutedText"), QStringLiteral("#333333")},
            {QStringLiteral("fileText"), QStringLiteral("#c4cbd3")},
            {QStringLiteral("folderText"), QStringLiteral("#ffffff")},
            {QStringLiteral("neutralFileTextColors"), false},
            {QStringLiteral("folderIcon"), QStringLiteral("#5ab2f1")},
        }));
        QTRY_COMPARE_WITH_TIMEOUT(
            folderText->property("color").value<QColor>(),
            QColor(QStringLiteral("#ff00ff")), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(
            fileText->property("color").value<QColor>(),
            QColor(QStringLiteral("#00ff00")), 3000);
        QCOMPARE(fileIcon->property("effectiveIconColor").value<QColor>(),
                 fileIconColor);
    }

    void panelHoverTracksViewportInsteadOfRecycledDelegate() {
        QQuickView view;
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        auto *runtime = ZoinGallery::GalleryRuntime::install(
            view.engine(), options);
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(
            QStringLiteral("stable-panel-hover"));
        QVERIFY(session);
        QVERIFY(session->applyExternalCatalog(plainCatalog(80), 1));

        QObject *panel = createPanel(
            view, session, QStringLiteral("stablePanelHoverSession"),
            QStringLiteral("details"));
        QVERIFY(panel);
        auto *layout = panel->findChild<MasonryLayout *>(
            QStringLiteral("galleryViewportItem"));
        QObject *hoverArea = panel->findChild<QObject *>(
            QStringLiteral("galleryMiddleButtonArea"));
        QVERIFY(layout);
        QVERIFY(hoverArea);
        QVERIFY(hoverArea->property("hoverEnabled").toBool());
        QCOMPARE(hoverArea->property("acceptedButtons").toInt(),
                 int(Qt::MiddleButton));
        QTRY_COMPARE_WITH_TIMEOUT(layout->count(), 80, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(
            layout->contentHeight() > layout->height(), 3000);

        const QRectF firstGeometry = layout->indexGeometry(0);
        QVERIFY(firstGeometry.isValid() && !firstGeometry.isEmpty());
        const qreal pointerX = firstGeometry.center().x();
        const qreal pointerY = firstGeometry.center().y();
        auto *panelItem = qobject_cast<QQuickItem *>(panel);
        QVERIFY(panelItem);
        const QPointF panelPoint = layout->mapToItem(
            panelItem, QPointF(pointerX, pointerY));
        QVariant hovered;
        QVERIFY(QMetaObject::invokeMethod(
            panel, "updateHoveredIndexAt", Qt::DirectConnection,
            Q_RETURN_ARG(QVariant, hovered),
            Q_ARG(QVariant, QVariant(panelPoint.x())),
            Q_ARG(QVariant, QVariant(panelPoint.y()))));
        QCOMPARE(hovered.toInt(), 0);
        QCOMPARE(panel->property("hoveredIndex").toInt(), 0);

        const QRectF targetGeometry = layout->indexGeometry(10);
        QVERIFY(targetGeometry.isValid() && !targetGeometry.isEmpty());
        QVERIFY(QMetaObject::invokeMethod(
            panel, "setPanelContentY", Qt::DirectConnection,
            Q_ARG(QVariant, QVariant(targetGeometry.y())),
            Q_ARG(QVariant, QVariant(true))));
        // onContentYChanged and the hover re-hit-test are synchronous. Check
        // that exact transition before unrelated startup placement timers can
        // restore the fixture's initial cursor at row zero.
        QVERIFY(layout->contentY() > 0);
        const QPointF viewportPoint = layout->mapFromItem(panelItem,
                                                          panelPoint);
        const int expected = layout->indexAtViewport(viewportPoint.x(),
                                                     viewportPoint.y());
        QVERIFY(expected >= 0);
        QVERIFY(expected != 0);
        QTRY_COMPARE_WITH_TIMEOUT(
            panel->property("hoveredIndex").toInt(), expected, 1000);

        QVERIFY(QMetaObject::invokeMethod(
            panel, "clearHoveredIndex", Qt::DirectConnection));
        QCOMPARE(panel->property("hoveredIndex").toInt(), -1);
    }

    void pointerDragDefersCursorAndCommitsOnlyFinalItem() {
        QQuickView view;
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        auto *runtime = ZoinGallery::GalleryRuntime::install(
            view.engine(), options);
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(
            QStringLiteral("deferred-pointer-drag"));
        QVERIFY(session);
        QVERIFY(session->applyExternalCatalog(plainCatalog(80), 1));
        session->setCurrentIndex(0);

        QObject *panel = createPanel(
            view, session, QStringLiteral("deferredPointerDragSession"),
            QStringLiteral("details"));
        QVERIFY(panel);
        auto *panelItem = qobject_cast<QQuickItem *>(panel);
        auto *layout = panel->findChild<MasonryLayout *>(
            QStringLiteral("galleryViewportItem"));
        QVERIFY(panelItem);
        QVERIFY(layout);
        QTRY_COMPARE_WITH_TIMEOUT(layout->count(), 80, 3000);
        QSignalSpy cursorSpy(panel,
                             SIGNAL(cursorRequested(QString,int,bool)));
        QVERIFY(cursorSpy.isValid());

        QVERIFY(QMetaObject::invokeMethod(
            panel, "handlePointerPress", Qt::DirectConnection,
            Q_ARG(QVariant, QVariant(0)),
            Q_ARG(QVariant, QVariant(int(Qt::LeftButton))),
            Q_ARG(QVariant, QVariant(int(Qt::NoModifier)))));
        QCOMPARE(cursorSpy.size(), 1);
        QVERIFY(cursorSpy.at(0).at(2).toBool());

        const auto dragTo = [&](int index) {
            const QRectF geometry = layout->indexGeometry(index);
            QVERIFY(geometry.isValid() && !geometry.isEmpty());
            const QPointF viewportPoint(
                geometry.center().x(),
                geometry.center().y() - layout->contentY());
            const QPointF panelPoint = layout->mapToItem(panelItem,
                                                         viewportPoint);
            QVERIFY(QMetaObject::invokeMethod(
                panel, "handlePointerDrag", Qt::DirectConnection,
                Q_ARG(QVariant, QVariant(panelPoint.x())),
                Q_ARG(QVariant, QVariant(panelPoint.y()))));
        };

        dragTo(1);
        QCOMPARE(session->currentIndex(), 1);
        QCOMPARE(cursorSpy.size(), 2);
        QVERIFY(cursorSpy.at(1).at(2).toBool());

        // Physical motion inside the same row must be a complete no-op.
        dragTo(1);
        QCOMPARE(cursorSpy.size(), 2);

        dragTo(2);
        QCOMPARE(session->currentIndex(), 2);
        QCOMPARE(cursorSpy.size(), 3);
        QVERIFY(cursorSpy.at(2).at(2).toBool());

        QVERIFY(QMetaObject::invokeMethod(
            panel, "endPointerDrag", Qt::DirectConnection));
        QCOMPARE(cursorSpy.size(), 4);
        QCOMPARE(cursorSpy.constLast().at(0).toString(),
                 session->entryIdAt(2));
        QCOMPARE(cursorSpy.constLast().at(1).toInt(), 2);
        QVERIFY(!cursorSpy.constLast().at(2).toBool());
        QVERIFY(!panel->property("cursorCommitPending").toBool());

        // The stable hover surface sits above the recycled delegates but
        // accepts no buttons. Exercise the real Qt delivery path to ensure a
        // normal click still reaches the row MouseArea underneath it.
        cursorSpy.clear();
        const QRectF clickGeometry = layout->indexGeometry(3);
        QVERIFY(clickGeometry.isValid() && !clickGeometry.isEmpty());
        const QPointF clickScenePoint = layout->mapToScene(QPointF(
            clickGeometry.center().x(),
            clickGeometry.center().y() - layout->contentY()));
        QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier,
                          clickScenePoint.toPoint());
        QTRY_COMPARE_WITH_TIMEOUT(session->currentIndex(), 3, 1000);
        QTRY_COMPARE_WITH_TIMEOUT(cursorSpy.size(), 2, 1000);
        QVERIFY(cursorSpy.at(0).at(2).toBool());
        QVERIFY(!cursorSpy.at(1).at(2).toBool());
    }

    void rightDragReleaseUsesFinalPosition_data() {
        QTest::addColumn<int>("button");
        QTest::addColumn<bool>("upward");
        QTest::addColumn<bool>("live");
        for (int button : {int(Qt::LeftButton), int(Qt::RightButton)})
            for (bool upward : {false, true})
                for (bool live : {false, true})
                    QTest::newRow(qPrintable(QString("%1-up%2-live%3").arg(button).arg(upward).arg(live)))
                        << button << upward << live;
    }

    void rightDragReleaseUsesFinalPosition() {
        QFETCH(int, button);
        QFETCH(bool, upward);
        QFETCH(bool, live);
        QQuickView view;
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        auto *runtime = ZoinGallery::GalleryRuntime::install(view.engine(), options);
        auto *session = runtime->createExternalSession("release-position");
        QVERIFY(session->applyExternalCatalog(plainCatalog(20), 1));
        session->setCurrentIndex(0);
        QObject *panel = createPanel(view, session, "releaseSession", "details");
        QVERIFY(panel);
        panel->setProperty("liveSelectionUpdates", live);
        auto *layout = panel->findChild<MasonryLayout *>("galleryViewportItem");
        QVERIFY(layout);
        QTRY_COMPARE(layout->count(), 20);
        QSignalSpy transactions(panel, SIGNAL(selectionTransactionRequested(QVariant,QString,int)));
        const auto point = [&](int row) {
            const QRectF rect = layout->indexGeometry(row);
            return layout->mapToScene(QPointF(rect.center().x(), rect.center().y() - layout->contentY()));
        };
        const int first = upward ? 3 : 1;
        const int last = upward ? 1 : 3;
        QTest::mousePress(&view, Qt::MouseButton(button), Qt::NoModifier, point(first).toPoint());
        QTest::mouseMove(&view, point(2).toPoint());
        QTest::qWait(30);
        // No move event at the final row: release itself must finish the drag.
        QMouseEvent release(QEvent::MouseButtonRelease, point(last),
                            view.mapToGlobal(point(last).toPoint()), Qt::MouseButton(button),
                            Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&view, &release);
        QCOMPARE(session->currentIndex(), last);
        if (button == int(Qt::RightButton)) {
            QVERIFY(!transactions.isEmpty());
            QCOMPARE(transactions.last().at(1).toString(), session->entryIdAt(last));
        }
    }

    void svgCursorAvoidsQtCustomColorSpaceCrash() {
        QVERIFY(QGuiApplication::overrideCursor() == nullptr);
        struct CursorReset {
            ~CursorReset() { SvgCursor::setOverrideCursor(); }
        } cursorReset;

        const QList<std::tuple<QString, qreal>> cursors{
            {QStringLiteral(":/ZoinGallery/resources/ScrollMode.svg"), 0.0},
            {QStringLiteral(":/ZoinGallery/resources/ScrollModeDown.svg"),
             0.0},
            {QStringLiteral(":/ZoinGallery/resources/ScrollModeUp.svg"),
             0.0},
            {QStringLiteral(":/ZoinGallery/resources/SphereScroll.svg"),
             37.0},
        };

        for (int repeat = 0; repeat < 100; ++repeat) {
            for (const auto &[path, rotation] : cursors) {
                SvgCursor::setOverrideCursor(path, 2.0, rotation);
                const QCursor *cursor = QGuiApplication::overrideCursor();
                QVERIFY(cursor);
                QCOMPARE(cursor->shape(), Qt::BitmapCursor);

                const QImage image = cursor->pixmap().toImage();
                QVERIFY(!image.isNull());
                QVERIFY2(!image.colorSpace().isValid(),
                         "custom cursor must remain untagged so Qt 6.11.1 "
                         "uses the safe system-sRGB CoreGraphics path");

#if defined(Q_OS_MACOS)
                // This is the exact conversion in the submitted crash stack.
                // Repeating it also gives ASan/GuardMalloc a deterministic
                // regression seam for the former dangling CGColorSpaceRef.
                CGImageRef cgImage = image.toCGImage();
                QVERIFY(cgImage);
                CGImageRelease(cgImage);
#endif
            }
        }

        for (const int direction : {-1, 0, 1}) {
            SvgCursor::setScrollingModeCursor(true, direction, 2.0);
            const QCursor *cursor = QGuiApplication::overrideCursor();
            QVERIFY(cursor);
            QCOMPARE(cursor->shape(), Qt::BitmapCursor);
        }
        SvgCursor::setScrollingModeCursor(false, 0, 2.0);
        QVERIFY(QGuiApplication::overrideCursor() == nullptr);

        SvgCursor::setOverrideCursor();
        QVERIFY(QGuiApplication::overrideCursor() == nullptr);
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
            QStringLiteral("galleryViewportItem"));
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

    void groupedOverflowAndDisabling_data() {
        QTest::addColumn<bool>("disableGroups");
        QTest::newRow("grouped-overflow") << false;
        QTest::newRow("disable-groups") << true;
    }

    void groupedOverflowAndDisabling() {
        QFETCH(bool, disableGroups);
        QQuickView view;
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        auto *runtime = ZoinGallery::GalleryRuntime::install(view.engine(), options);
        auto *session = runtime->createExternalSession("grouped-scroll-transition");
        QVERIFY(session->applyExternalCatalog(plainCatalog(6), 1));
        QObject *panel = createPanel(view, session, "groupedScrollSession", "masonry");
        QVERIFY(panel);
        auto *layout = panel->findChild<MasonryLayout *>("galleryViewportItem");
        auto *scrollBar = panel->findChild<QQuickItem *>("galleryPanelScrollBar");
        QVERIFY(layout && scrollBar);
        panel->setProperty("devicePixelRatio", view.devicePixelRatio());
        QVariantList groups;
        for (int i = 0; i < 6; ++i)
            groups.append(QVariantMap{{"key", QString::number(i)}, {"title", "Group"},
                                      {"startIndex", i}, {"count", 1}});
        QVERIFY(panel->setProperty("groupDescriptors", groups));
        QTRY_VERIFY(layout->contentHeight() > layout->height());
        QTRY_VERIFY(!layout->visibleGroupHeaders().isEmpty());
        auto *layer = panel->findChild<QQuickItem *>("galleryGroupHeaderLayer");
        QVERIFY(layer);
        auto headerCount = [&]() {
            int count = 0;
            for (auto *item : layer->childItems())
                if (item->objectName().startsWith("galleryGroupHeader-") && item->isVisible()) ++count;
            return count;
        };
        QTRY_VERIFY(headerCount() > 0);
        if (disableGroups) {
            QVERIFY(panel->setProperty("groupDescriptors", QVariantList{}));
            QTRY_VERIFY(layout->visibleGroupHeaders().isEmpty());
            QTRY_COMPARE(headerCount(), 0);
        } else {
            QTRY_VERIFY(layout->needScroll());
            QTRY_VERIFY(scrollBar->isVisible());
            view.show();
            QVERIFY(QTest::qWaitForWindowExposed(&view));
            QTest::qWait(100);
            const qreal barStart = scrollBar->mapToScene(QPointF()).x();
            for (auto *header : layer->childItems()) {
                if (!header->objectName().startsWith("galleryGroupHeader-") || !header->isVisible()) continue;
                auto *count = header->findChild<QQuickItem *>(header->objectName() + "-count");
                QVERIFY(count);
                QVERIFY(count->mapToScene(QPointF(count->width(), 0)).x() <= barStart - 7.9);
                auto *separator = findVisualItem(qobject_cast<QQuickItem *>(panel), header->objectName() + "-separator");
                QVERIFY(separator);
                QVERIFY(separator->mapToScene(QPointF(separator->width(), 0)).x() <= barStart - 3.9);
                const QPointF origin = count->mapToScene(QPointF());
                const qreal dpr = view.devicePixelRatio();
                QVERIFY(qAbs(origin.x() * dpr - qRound(origin.x() * dpr)) < 0.01);
                QVERIFY(qAbs(origin.y() * dpr - qRound(origin.y() * dpr)) < 0.01);
                QCOMPARE(count->mapToScene(QPointF(1, 0)) - origin, QPointF(1, 0));
                QCOMPARE(count->mapToScene(QPointF(0, 1)) - origin, QPointF(0, 1));
            }
            if (qEnvironmentVariableIsSet("F4_GROUP_GUTTER_CAPTURE"))
                QVERIFY(view.grabWindow().save(qEnvironmentVariable("F4_GROUP_GUTTER_CAPTURE")));
            auto *header = findVisualItem(qobject_cast<QQuickItem *>(panel), "galleryGroupHeader-gallery-0");
            QVERIFY(header);
            const QPoint excluded = QPointF(barStart - 2,
                header->mapToScene(QPointF(0, header->height() / 2)).y()).toPoint();
            QTest::mouseMove(&view, excluded);
            QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, excluded);
            QVERIFY2(!layout->isGroupCollapsed("0"), "Header accepted a click in its scrollbar clearance");
            auto *hover = findVisualItem(qobject_cast<QQuickItem *>(panel), header->objectName() + "-hover");
            QVERIFY(hover);
            QVERIFY(!hover->isVisible());
            QVERIFY(hover->mapToScene(QPointF(hover->width(), 0)).x() <= barStart - 3.9);
            const QPoint inside = header->mapToScene(QPointF(20, header->height() / 2)).toPoint();
            QTest::mouseMove(&view, inside);
            QTRY_VERIFY(hover->isVisible());
            QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, inside);
            QTRY_VERIFY(layout->isGroupCollapsed("0"));
        }
        runtime->shutdown();
    }

    void groupedLayoutsRenderHeadersAndSkipCollapsedRows() {
        QQuickView view;
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        auto *runtime = ZoinGallery::GalleryRuntime::install(
            view.engine(), options);
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(
            QStringLiteral("grouped-layouts"));
        QVERIFY(session);
        QVERIFY(session->applyExternalCatalog(plainCatalog(9), 1,
                                              {{"currentPath", "/grouped"}}));
        session->setCurrentIndex(2);

        QObject *panel = createPanel(
            view, session, QStringLiteral("groupedLayoutsSession"),
            QStringLiteral("masonry"));
        QVERIFY(panel);
        auto *layout = panel->findChild<MasonryLayout *>(
            QStringLiteral("galleryViewportItem"));
        QVERIFY(layout);
        QObject *iconResolver = panel->property("iconResolver")
                                    .value<QObject *>();
        QVERIFY(iconResolver);
        iconResolver->setProperty(
            "compactPrefix", QStringLiteral("qrc:/test/lucide"));

        const QVariantList descriptors{
            QVariantMap{{QStringLiteral("key"), QStringLiteral("alpha")},
                        {QStringLiteral("title"), QStringLiteral("Alpha")},
                        {QStringLiteral("startIndex"), 1},
                        {QStringLiteral("count"), 3}},
            QVariantMap{{QStringLiteral("key"), QStringLiteral("beta")},
                        {QStringLiteral("title"), QStringLiteral("Beta")},
                        {QStringLiteral("startIndex"), 4},
                        {QStringLiteral("count"), 5}},
        };
        QVERIFY(panel->setProperty("groupDescriptors", descriptors));
        QVERIFY(panel->setProperty(
            "groupStateKey", QStringLiteral("side=0|path=/grouped|mode=Name")));
        panel->setProperty("devicePixelRatio", view.devicePixelRatio());
        QTRY_COMPARE_WITH_TIMEOUT(layout->groupForIndex(2), 0, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!layout->visibleGroupHeaders().isEmpty(),
                                 3000);
        const QRectF leadingGeometry = layout->indexGeometry(0);
        const QRectF firstGroupGeometry = layout->indexGeometry(1);
        QVERIFY(leadingGeometry.isValid() && !leadingGeometry.isEmpty());
        QVERIFY(firstGroupGeometry.isValid() && !firstGroupGeometry.isEmpty());
        qreal firstHeaderHeight = 0;
        for (const QVariant &value : layout->visibleGroupHeaders()) {
            const QVariantMap header = value.toMap();
            if (header.value(QStringLiteral("key")).toString()
                == QStringLiteral("alpha")) {
                firstHeaderHeight = header.value(
                    QStringLiteral("height")).toReal();
                break;
            }
        }
        QVERIFY(firstHeaderHeight > 0);
        QVERIFY2(firstGroupGeometry.top()
                     >= leadingGeometry.bottom() + firstHeaderHeight - 0.01,
                 qPrintable(QStringLiteral(
                     "leadingTop=%1 leadingBottom=%2 firstGroupTop=%3 "
                     "header=%4")
                     .arg(leadingGeometry.top(), 0, 'f', 2)
                     .arg(leadingGeometry.bottom(), 0, 'f', 2)
                     .arg(firstGroupGeometry.top(), 0, 'f', 2)
                     .arg(firstHeaderHeight)));
        const qreal dpr = view.devicePixelRatio();
        const auto *panelItem = qobject_cast<QQuickItem *>(panel);
        QVERIFY(panelItem);
        for (const QString &key : {QStringLiteral("alpha"),
                                    QStringLiteral("beta")}) {
            auto *header = findVisualItem(
                qobject_cast<QQuickItem *>(panel),
                QStringLiteral("galleryGroupHeader-gallery-%1").arg(key));
            QVERIFY(header);
            auto *toggleIcon = findVisualItem(
                qobject_cast<QQuickItem *>(panel),
                header->objectName() + QStringLiteral("-chevron"));
            QVERIFY(toggleIcon);
            QVERIFY2(toggleIcon->property("source").isValid(),
                     "group toggle must use an image, not a text glyph");
            auto *separator = findVisualItem(
                qobject_cast<QQuickItem *>(panel),
                header->objectName() + QStringLiteral("-separator"));
            QVERIFY(separator);
            auto *title = findVisualItem(
                qobject_cast<QQuickItem *>(panel),
                header->objectName() + QStringLiteral("-title"));
            QVERIFY(title);
            QVERIFY2(title->x() < toggleIcon->x(),
                     "group title must precede the expand/collapse icon");
            QVERIFY2(toggleIcon->x() <= title->x()
                         + title->property("implicitWidth").toReal() + 7,
                     "group chevron must follow the text, not the panel edge");
            const QPointF separatorInHeader = separator->mapToItem(header, QPointF());
            QVERIFY2(separatorInHeader.y() >= 7.5,
                     "group separator needs space above it");
            const auto *layoutItem = qobject_cast<QQuickItem *>(layout);
            QVERIFY(layoutItem);
            const QPointF panelOrigin = panelItem->mapToItem(
                view.contentItem(), QPointF());
            const QPointF separatorOrigin = separator->mapToItem(
                view.contentItem(), QPointF());
            QVERIFY2(qAbs(separatorOrigin.x() - panelOrigin.x()) < 0.01,
                     "group separator must start at the panel edge");
            const qreal scrollInset = panel->property("groupHeaderRightInset").toReal();
            const qreal lineEnd = panelOrigin.x() + panelItem->width()
                - (scrollInset > 0 ? scrollInset + 4 : 0);
            QVERIFY2(qAbs(separatorOrigin.x() + separator->width()
                         - qRound(lineEnd * dpr) / dpr) < 0.01,
                     "group separator must stop before the scrollbar, or at the panel edge without one");
            for (const QString &leafSuffix : {QStringLiteral("-chevron"),
                                               QStringLiteral("-title"),
                                               QStringLiteral("-count")}) {
                auto *leaf = findVisualItem(
                    qobject_cast<QQuickItem *>(panel),
                    header->objectName() + leafSuffix);
                QVERIFY(leaf);
                const QPointF origin = leaf->mapToItem(
                    view.contentItem(), QPointF());
                QVERIFY2(qAbs(origin.x() * dpr
                              - qRound(origin.x() * dpr)) < 0.01,
                         qPrintable(QStringLiteral("%1 x=%2")
                             .arg(leaf->objectName())
                             .arg(origin.x() * dpr, 0, 'f', 6)));
                QVERIFY2(qAbs(origin.y() * dpr
                              - qRound(origin.y() * dpr)) < 0.01,
                         qPrintable(QStringLiteral("%1 y=%2")
                             .arg(leaf->objectName())
                             .arg(origin.y() * dpr, 0, 'f', 6)));
                QCOMPARE(leaf->mapToItem(view.contentItem(), QPointF(1, 0))
                             - origin, QPointF(1, 0));
                QCOMPARE(leaf->mapToItem(view.contentItem(), QPointF(0, 1))
                             - origin, QPointF(0, 1));
            }
        }
        const QImage groupHeaderCapture = view.grabWindow();
        QVERIFY(!groupHeaderCapture.isNull());
        if (qEnvironmentVariableIsSet("F4_GROUP_HEADER_CAPTURE"))
            QVERIFY(groupHeaderCapture.save(qEnvironmentVariable("F4_GROUP_HEADER_CAPTURE")));
        const QStringList modes{
            QStringLiteral("masonry"), QStringLiteral("details"),
            QStringLiteral("grid"), QStringLiteral("icons"),
            QStringLiteral("columns"),
        };
        for (const QString &mode : modes) {
            panel->setProperty("presentationMode", mode);
            QTRY_COMPARE_WITH_TIMEOUT(
                layout->presentationMode(),
                mode == QStringLiteral("masonry") ? MasonryLayout::Masonry
                : mode == QStringLiteral("details") ? MasonryLayout::Details
                : mode == QStringLiteral("grid") ? MasonryLayout::Grid
                : mode == QStringLiteral("icons") ? MasonryLayout::Icons
                : MasonryLayout::Columns,
                3000);
            QTRY_VERIFY_WITH_TIMEOUT(
                layout->groupForIndex(5) == 1
                    && !layout->visibleGroupHeaders().isEmpty(), 3000);

            const QRectF beforeCollapse = layout->indexGeometry(1);
            const qreal contentBefore = layout->contentHeight();
            QVERIFY(beforeCollapse.isValid() && !beforeCollapse.isEmpty());
            QVERIFY(layout->setGroupCollapsed(QStringLiteral("alpha"), true));
            QTRY_VERIFY_WITH_TIMEOUT(layout->indexGeometry(1).isEmpty(),
                                     3000);
            QVERIFY(layout->indexGeometry(4).isValid());
            if (mode == QStringLiteral("columns")) {
                QVERIFY(layout->contentHeight() <= contentBefore);
            } else {
                QVERIFY(layout->contentHeight() < contentBefore);
            }
            QCOMPARE(layout->nearestVisibleIndex(1, true), 4);

            const auto navigation = layout->navigationTarget(
                0, mode == QStringLiteral("columns")
                       ? MasonryLayout::NavigateRight
                       : MasonryLayout::NavigateDown);
            const int navigationTarget = navigation.value(
                QStringLiteral("targetIndex")).toInt();
            QVERIFY(navigationTarget >= 4 && navigationTarget < 9);
            QVERIFY(!layout->indexGeometry(navigationTarget).isEmpty());

            QVERIFY(layout->setGroupCollapsed(QStringLiteral("alpha"), false));
            QTRY_VERIFY_WITH_TIMEOUT(!layout->indexGeometry(1).isEmpty(),
                                     3000);
            QCOMPARE(layout->nearestVisibleIndex(1, true), 2);
        }

        panel->setProperty("presentationMode", QStringLiteral("details"));
        QTRY_COMPARE_WITH_TIMEOUT(layout->presentationMode(),
                                  MasonryLayout::Details, 3000);
        session->setCurrentIndex(2);
        QVariant moved;
        QVERIFY(QMetaObject::invokeMethod(
            panel, "toggleGalleryGroup", Qt::DirectConnection,
            Q_RETURN_ARG(QVariant, moved),
            Q_ARG(QVariant, QVariant(QStringLiteral("alpha")))));
        QVERIFY(moved.toBool());
        QTRY_COMPARE_WITH_TIMEOUT(session->currentIndex(), 4, 3000);
        QVERIFY(layout->isGroupCollapsed(QStringLiteral("alpha")));
        QVERIFY(layout->indexGeometry(2).isEmpty());
        QCOMPARE(session->collapsedGroupKeys(
                     QStringLiteral("side=0|path=/grouped|mode=Name")),
                 QStringList{QStringLiteral("alpha")});

        layout->setGroupStateKey(QStringLiteral("side=0|path=/other|mode=Name"));
        layout->setGroupStateKey(
            QStringLiteral("side=0|path=/grouped|mode=Name"));
        QVERIFY(layout->isGroupCollapsed(QStringLiteral("alpha")));

        QVERIFY(layout->setGroupCollapsed(QStringLiteral("alpha"), false));
        QTRY_VERIFY_WITH_TIMEOUT(!layout->indexGeometry(2).isEmpty(), 3000);
        QVERIFY(session->collapsedGroupKeys(
            QStringLiteral("side=0|path=/grouped|mode=Name")).isEmpty());
    }

    void groupedMasonryKeyboardNavigationSkipsSectionHeaders() {
        QQuickView view;
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        auto *runtime = ZoinGallery::GalleryRuntime::install(
            view.engine(), options);
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(
            QStringLiteral("grouped-masonry-keyboard-navigation"));
        QVERIFY(session);
        QVERIFY(session->applyExternalCatalog(plainCatalog(9), 1,
                                              {{"currentPath", "/grouped"}}));

        const QVariantList descriptors{
            QVariantMap{{QStringLiteral("key"), QStringLiteral("alpha")},
                        {QStringLiteral("title"), QStringLiteral("Alpha")},
                        {QStringLiteral("startIndex"), 1},
                        {QStringLiteral("count"), 3}},
            QVariantMap{{QStringLiteral("key"), QStringLiteral("beta")},
                        {QStringLiteral("title"), QStringLiteral("Beta")},
                        {QStringLiteral("startIndex"), 4},
                        {QStringLiteral("count"), 5}},
        };
        session->setCurrentIndex(3);

        QObject *panel = createPanel(
            view, session, QStringLiteral("groupedMasonryKeyboardSession"),
            QStringLiteral("masonry"));
        QVERIFY(panel);
        auto *layout = panel->findChild<MasonryLayout *>(
            QStringLiteral("galleryViewportItem"));
        QVERIFY(layout);
        QVERIFY(panel->setProperty("groupDescriptors", descriptors));
        QVERIFY(panel->setProperty(
            "groupStateKey", QStringLiteral(
                "side=0|path=/grouped|mode=Name")));
        QTRY_COMPARE_WITH_TIMEOUT(layout->currentIndex(), 3, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(layout->groupForIndex(3), 0, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!layout->visibleGroupHeaders().isEmpty(),
                                 3000);

        const QRectF current = layout->indexGeometry(3);
        QVERIFY(current.isValid() && !current.isEmpty());
        QCOMPARE(layout->indexAt(current.center().x(), current.bottom() + 2),
                 -1);

        QVariant navigationResult;
        QVERIFY(QMetaObject::invokeMethod(
            panel, "navigationTargetForKey", Qt::DirectConnection,
            Q_RETURN_ARG(QVariant, navigationResult),
            Q_ARG(QVariant, QVariant(Qt::Key_Down)),
            Q_ARG(QVariant, QVariant(false))));
        const int firstDown = navigationResult.toInt();
        const QRectF firstDownGeometry = layout->indexGeometry(firstDown);
        QVERIFY2(layout->groupForIndex(firstDown) == 1,
                 qPrintable(QStringLiteral(
                     "Down crossed header to source index %1 in group %2")
                     .arg(firstDown)
                     .arg(layout->groupForIndex(firstDown))));
        QVERIFY(firstDownGeometry.left() <= current.center().x()
                && firstDownGeometry.right() >= current.center().x());

        session->setCurrentIndex(4);
        QTRY_COMPARE_WITH_TIMEOUT(layout->currentIndex(), 4, 3000);
        const QRectF upwardCurrent = layout->indexGeometry(4);
        navigationResult.clear();
        QVERIFY(QMetaObject::invokeMethod(
            panel, "navigationTargetForKey", Qt::DirectConnection,
            Q_RETURN_ARG(QVariant, navigationResult),
            Q_ARG(QVariant, QVariant(Qt::Key_Up)),
            Q_ARG(QVariant, QVariant(false))));
        const int firstUp = navigationResult.toInt();
        QVERIFY2(layout->groupForIndex(firstUp) == 0,
                 qPrintable(QStringLiteral(
                     "Up crossed header to source index %1 in group %2")
                     .arg(firstUp)
                     .arg(layout->groupForIndex(firstUp))));
        const QRectF firstUpGeometry = layout->indexGeometry(firstUp);
        QVERIFY(firstUpGeometry.left() <= upwardCurrent.center().x()
                && firstUpGeometry.right() >= upwardCurrent.center().x());

        // The item before the last alpha source row is intentionally not the
        // last source index. When the preserved X lands in the section header,
        // Down must enter beta at that X instead of walking alpha source order.
        session->setCurrentIndex(2);
        QTRY_COMPARE_WITH_TIMEOUT(layout->currentIndex(), 2, 3000);
        const QRectF boundaryCurrent = layout->indexGeometry(2);
        QVERIFY(boundaryCurrent.isValid() && !boundaryCurrent.isEmpty());
        QCOMPARE(layout->indexAt(boundaryCurrent.center().x(),
                                 boundaryCurrent.bottom() + 2), -1);
        navigationResult.clear();
        QVERIFY(QMetaObject::invokeMethod(
            panel, "navigationTargetForKey", Qt::DirectConnection,
            Q_RETURN_ARG(QVariant, navigationResult),
            Q_ARG(QVariant, QVariant(Qt::Key_Down)),
            Q_ARG(QVariant, QVariant(false))));
        const int boundaryDown = navigationResult.toInt();
        QVERIFY2(layout->groupForIndex(boundaryDown) == 1,
                 qPrintable(QStringLiteral(
                     "Down crossed header to source index %1 in group %2")
                     .arg(boundaryDown)
                     .arg(layout->groupForIndex(boundaryDown))));
    }

    void groupedItemsRemainMouseInteractive() {
        QQuickView view;
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        auto *runtime = ZoinGallery::GalleryRuntime::install(
            view.engine(), options);
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(
            QStringLiteral("grouped-mouse-interaction"));
        QVERIFY(session);

        QVariantList catalog = plainCatalog(9);
        QVariantMap parent = catalog.at(0).toMap();
        parent[QStringLiteral("name")] = QStringLiteral("..");
        parent[QStringLiteral("isDir")] = true;
        parent[QStringLiteral("isImage")] = false;
        catalog[0] = parent;
        QVERIFY(session->applyExternalCatalog(
            catalog, 1, {{QStringLiteral("currentPath"),
                          QStringLiteral("/grouped-mouse")}}));
        session->setCurrentIndex(0);

        QObject *panel = createPanel(
            view, session, QStringLiteral("groupedMouseSession"),
            QStringLiteral("masonry"));
        QVERIFY(panel);
        auto *panelItem = qobject_cast<QQuickItem *>(panel);
        auto *layout = panel->findChild<MasonryLayout *>(
            QStringLiteral("galleryViewportItem"));
        QVERIFY(panelItem);
        QVERIFY(layout);

        const QVariantList descriptors{
            QVariantMap{{QStringLiteral("key"), QStringLiteral("alpha")},
                        {QStringLiteral("title"), QStringLiteral("Alpha")},
                        {QStringLiteral("startIndex"), 1},
                        {QStringLiteral("count"), 3}},
            QVariantMap{{QStringLiteral("key"), QStringLiteral("beta")},
                        {QStringLiteral("title"), QStringLiteral("Beta")},
                        {QStringLiteral("startIndex"), 4},
                        {QStringLiteral("count"), 5}},
        };
        QVERIFY(panel->setProperty("groupDescriptors", descriptors));
        QVERIFY(panel->setProperty(
            "groupStateKey", QStringLiteral(
                "side=0|path=/grouped-mouse|mode=Name")));
        QTRY_COMPARE_WITH_TIMEOUT(layout->count(), 9, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(layout->groupForIndex(1) == 0
                                     && layout->groupForIndex(5) == 1,
                                 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!layout->visibleGroupHeaders().isEmpty(),
                                 3000);

        const auto scenePointForIndex = [&](int index, QPointF *scenePoint) {
            const QRectF geometry = layout->indexGeometry(index);
            if (!geometry.isValid() || geometry.isEmpty())
                return false;
            *scenePoint = layout->mapToScene(QPointF(
                geometry.center().x(),
                geometry.center().y() - layout->contentY()));
            return true;
        };
        const auto verifyMouseTarget = [&](int index) {
            const QRectF geometry = layout->indexGeometry(index);
            QVERIFY(geometry.isValid() && !geometry.isEmpty());
            const qreal targetContentY = qBound<qreal>(
                0, geometry.center().y() - layout->height() / 2,
                qMax<qreal>(0, layout->contentHeight() - layout->height()));
            layout->setContentY(targetContentY);
            QTRY_VERIFY_WITH_TIMEOUT(
                layout->contentY() == targetContentY, 1000);
            QPointF scenePoint;
            QVERIFY(scenePointForIndex(index, &scenePoint));
            const QPoint point = scenePoint.toPoint();
            QTest::mouseMove(&view, QPoint(1, 1));
            QTest::mouseMove(&view, point);
            QTRY_COMPARE_WITH_TIMEOUT(
                panel->property("hoveredIndex").toInt(), index, 1000);
            QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, point);
            QTRY_COMPARE_WITH_TIMEOUT(session->currentIndex(), index, 1000);
        };

        verifyMouseTarget(0);
        verifyMouseTarget(1);
        verifyMouseTarget(5);
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
            QStringLiteral("galleryViewportItem"));
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
            const QString objectName =
                QStringLiteral("gallerySelectionSurface-%1").arg(index);
            const auto surfaces = panelItem->findChildren<QQuickItem *>(
                objectName, Qt::FindChildrenRecursively);
            for (QQuickItem *surface : surfaces) {
                QQuickItem *const delegate = surface
                    ? surface->parentItem() : nullptr;
                if (delegate && delegate->isVisible()) {
                    return delegate;
                }
            }
            return nullptr;
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
            QStringLiteral("galleryViewportItem"));
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

    void iconRowsReserveUniformFourLineCapacity() {
        QVariantList catalog;
        const QString longName = QStringLiteral(
            "Beginning-of-a-very-long-file-name-with-many-descriptive-"
            "words-and-a-middle-section-that-must-not-all-be-visible-"
            "while-the-final-part-remains-important.extension");
        for (int index = 0; index < 12; ++index) {
            QVariantMap entry = catalogEntry(index);
            entry[QStringLiteral("name")] = index == 2
                ? longName
                : QStringLiteral("x%1").arg(index);
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
            QStringLiteral("galleryViewportItem"));
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
        QCOMPARE(firstLong.height(), secondShort.height());
        QVERIFY(firstLong.height() > firstLong.width());
        QCOMPARE(secondShort.top(), firstShort.bottom());

        // The analytical Icons layout reserves one fixed four-line-capable
        // stride for every row. Preview size remains invariant across short
        // and long labels and across physical rows.
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
        QVERIFY(fallbackIcon);
        QVERIFY(backdrop);
        // Empty non-image rows must not instantiate a dormant thumbnail
        // shader subtree. The mode delegate creates it lazily only when an
        // image source becomes available.
        QVERIFY(!panel->findChild<QQuickItem *>(
            QStringLiteral("galleryThumbnailShader-0")));
        QTRY_VERIFY_WITH_TIMEOUT(
            qAbs(fallbackIcon->width()
                 - qMin(shortPreview.width(), shortPreview.height())) <= 0.51,
            3000);
        QVERIFY(!backdrop->property("enabledForPresentation").toBool());
        QVERIFY(!backdrop->isVisible());

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
        QVERIFY(shortLabel->height() >= shortLabel->implicitHeight());
        QCOMPARE(shortLabel->height(), longLabel->height());

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
                 layout->indexGeometry(layout->count() - 1).bottom()
                     + layout->paddingBottom());

        // Grid remains the equal-height, one-line presentation.
        panel->setProperty("presentationMode", QStringLiteral("grid"));
        QTRY_COMPARE_WITH_TIMEOUT(layout->presentationMode(),
                                  MasonryLayout::Grid, 3000);
        QCOMPARE(layout->indexGeometry(0).height(), layout->density());
        QCOMPARE(layout->indexGeometry(2).height(), layout->density());

        runtime->shutdown();
    }

    void cachedSparseMetadataBatchSnapsWithoutGeometryAnimation() {
        constexpr int imageCount = 6;
        QVariantList catalog;
        catalog.reserve(imageCount);
        for (int row = 0; row < imageCount; ++row) {
            catalog.append(QVariantMap{
                {QStringLiteral("entryId"),
                 QStringLiteral("cached-layout-%1").arg(row)},
                {QStringLiteral("index"), row},
                {QStringLiteral("name"),
                 QStringLiteral("cached-layout-%1.png").arg(row)},
                {QStringLiteral("localPath"),
                 QStringLiteral("/virtual/cache/layout-%1.png").arg(row)},
                {QStringLiteral("isDir"), false},
                {QStringLiteral("isImage"), true},
                {QStringLiteral("mtimeNs"),
                 qint64(1'700'000'000'000'000'000LL + row)},
                {QStringLiteral("size"), qint64(4096 + row)},
            });
        }

        QQuickView view;
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        auto *runtime = ZoinGallery::GalleryRuntime::install(
            view.engine(), options);
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(
            QStringLiteral("cached-layout-batch"));
        QVERIFY(session);
        QVERIFY(session->applyExternalCatalog(catalog, 1));

        QObject *panel = createPanel(
            view, session, QStringLiteral("cachedLayoutSession"));
        QVERIFY(panel);
        auto *layout = panel->findChild<MasonryLayout *>(
            QStringLiteral("galleryViewportItem"));
        QVERIFY(layout);
        QTRY_COMPARE_WITH_TIMEOUT(layout->count(), imageCount, 3000);
        DecodeManager *decodeManager = runtime->findChild<DecodeManager *>();
        QVERIFY(decodeManager);

        auto metadataForRows = [&](const QList<int> &rows, bool cached) {
            QList<ImageInfo> infos;
            infos.reserve(rows.size());
            for (qsizetype position = 0; position < rows.size(); ++position) {
                const int row = rows.at(position);
                ImageFile *image = session->model()
                    ->data(session->model()->index(row, 0),
                           FileListModel::ImageFileRole)
                    .value<ImageFile *>();
                Q_ASSERT(image);
                ImageInfo info = image->info();
                info.imageSize = cached
                    ? QSize(480 + row * 120, 900 - row * 70)
                    : QSize(800 + row * 40, 600 + row * 30);
                info.isCached = cached;
                info.isLast = position == rows.size() - 1;
                info.requestNamespace = session->sessionId();
                infos.append(std::move(info));
            }
            return infos;
        };

        decodeManager->imagesInfoReady(
            metadataForRows({0, 1, 2, 3, 4, 5}, false));
        QTest::qWait(550);
        QCoreApplication::processEvents();

        QQuickItem *surface = panel->findChild<QQuickItem *>(
            QStringLiteral("gallerySelectionSurface-0"));
        QVERIFY(surface);
        QQuickItem *brick = surface->parentItem();
        QVERIFY(brick);
        auto *animation =
            brick->findChild<QParallelAnimationGroup *>();
        QVERIFY(!animation
                || animation->state() == QAbstractAnimation::Stopped);

        // Rows 0 and 2 form one sparse dataChanged range. Row 1 deliberately
        // retains valid, non-cached metadata; scanning the whole range used to
        // misclassify the cached batch and animate every visible brick for
        // 500 ms.
        decodeManager->imagesInfoReady(metadataForRows({0, 2}, true));
        QVERIFY(!animation
                || animation->state() == QAbstractAnimation::Stopped);
        const QRectF target = layout->indexGeometry(0).adjusted(
            0, layout->paddingTop(), 0, 0);
        const QRectF actual(brick->x(), brick->y(), brick->width(),
                            brick->height());
        QCOMPARE(actual.toRect(), target.toRect());

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
            QStringLiteral("galleryViewportItem"));
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
        QQuickItem *const initialIconDelegate = layout->currentItem();
        QVERIFY(initialIconDelegate);
        // The old Icons contentY addresses a completely different Grid row.
        // Switching must not restore that raw number or animate toward the
        // selected item after the new mode has already painted.
        const qreal iconsContentY = layout->contentY();
        // Reproduce the host adapter's one transaction around mode, saved
        // density and chrome geometry. A same-mode zoom deliberately keeps
        // the leading viewport brick; the enclosing mode transaction owns
        // the stronger cross-presentation cursor anchor.
        QVERIFY(QMetaObject::invokeMethod(
            panel, "beginPresentationStateUpdate", Q_ARG(QVariant, true)));
        panel->setProperty("presentationMode", QStringLiteral("grid"));
        panel->setProperty("density", 176.0);
        QVERIFY(QMetaObject::invokeMethod(
            panel, "endPresentationStateUpdate", Q_ARG(QVariant, true)));
        QTRY_COMPARE_WITH_TIMEOUT(layout->presentationMode(),
                                  MasonryLayout::Grid, 3000);
        QCoreApplication::processEvents();
        const QRectF switchedGeometry = layout->indexGeometry(selectedIndex);
        QVERIFY2(indexIntersectsViewport(layout, selectedIndex),
                 qPrintable(QStringLiteral(
                     "selected=%1 geometry=(%2,%3 %4x%5) contentY=%6 "
                     "viewport=%7 contentHeight=%8 visibleCount=%9")
                     .arg(selectedIndex)
                     .arg(switchedGeometry.x()).arg(switchedGeometry.y())
                     .arg(switchedGeometry.width())
                     .arg(switchedGeometry.height())
                     .arg(layout->contentY()).arg(layout->height())
                     .arg(layout->contentHeight())
                     .arg(layout->visibleIndexes().size())));
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
            if (candidate->isVisible()
                && candidate->property("viewIndex").toInt()
                       == selectedIndex) {
                selectedDelegate = candidate;
                break;
            }
        }
        QVERIFY(selectedDelegate);
        int activeGridSlots = 0;
        int hiddenOutgoingModeSlots = 0;
        for (BrickItem *slot : layout->findChildren<BrickItem *>()) {
            if (slot->isVisible()) {
                ++activeGridSlots;
                QCOMPARE(slot->property("mode").toString(),
                         QStringLiteral("grid"));
            }
            else if (slot->property("mode").toString()
                     == QStringLiteral("icons")) {
                ++hiddenOutgoingModeSlots;
            }
        }
        QVERIFY(activeGridSlots > 0);
        QCOMPARE(hiddenOutgoingModeSlots, 0);
        const auto delegateGeometry = [selectedDelegate] {
            return QRectF(selectedDelegate->x(), selectedDelegate->y(),
                          selectedDelegate->width(),
                          selectedDelegate->height());
        };
        QCOMPARE(delegateGeometry().toRect(),
                 layout->indexGeometry(selectedIndex).toRect());
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
        QVERIFY(layout->currentItem());
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
                QStringLiteral("galleryViewportItem"));
            QVERIFY(panelItem);
            QVERIFY(layout);
            QTRY_COMPARE_WITH_TIMEOUT(layout->count(), 80, 3000);
            panelItem->forceActiveFocus();
            view.requestActivate();
            QSignalSpy transactionSpy(
                panel, SIGNAL(selectionTransactionRequested(
                    QVariant,QString,int)));
            QVERIFY(transactionSpy.isValid());
            qulonglong selectionRevision = 1;

            for (Qt::Key key : keys) {
                QVERIFY(session->applyExternalState(
                    session->entryIdAt(40), 40, {}, ++selectionRevision));
                QVERIFY(invokeEnsureCurrentVisible(panel, false));
                QVERIFY(QMetaObject::invokeMethod(
                    panel, "cancelCursorChromeTransition",
                    Qt::DirectConnection));
                transactionSpy.clear();

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
                QCOMPARE(transactionSpy.size(), 0);
                QVERIFY(panel->property(
                    "keyboardShiftSelectionActive").toBool());

                QKeyEvent keyRelease(QEvent::KeyRelease, key,
                                     Qt::ShiftModifier);
                QCoreApplication::sendEvent(&view, &keyRelease);
                QCOMPARE(transactionSpy.size(), 0);
                QKeyEvent shiftRelease(QEvent::KeyRelease, Qt::Key_Shift,
                                       Qt::NoModifier);
                QCoreApplication::sendEvent(&view, &shiftRelease);
                QCOMPARE(transactionSpy.size(), 1);
                const QList<QVariant> transaction =
                    transactionSpy.constFirst();
                const QVariantList changes = transaction.at(0).toList();
                QVERIFY(!changes.isEmpty());
                QStringList acknowledged;
                for (const QVariant &value : changes) {
                    const QVariantMap change = value.toMap();
                    QVERIFY(change.value(QStringLiteral("selected")).toBool());
                    acknowledged.push_back(
                        change.value(QStringLiteral("entryId")).toString());
                }
                QCOMPARE(transaction.at(1).toString(),
                         session->cursorEntryId());
                QCOMPARE(transaction.at(2).toInt(),
                         session->currentIndex());
                QVERIFY(!panel->property(
                    "keyboardShiftSelectionActive").toBool());
                QVERIFY(session->applyExternalState(
                    session->cursorEntryId(), session->currentIndex(),
                    acknowledged, ++selectionRevision));
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
                QStringLiteral("galleryViewportItem"));
            QVERIFY(panelItem);
            QVERIFY(layout);
            QTRY_COMPARE_WITH_TIMEOUT(layout->count(), 80, 3000);
            panelItem->forceActiveFocus();
            view.requestActivate();
            QSignalSpy transactionSpy(
                panel, SIGNAL(selectionTransactionRequested(
                    QVariant,QString,int)));
            QVERIFY(transactionSpy.isValid());

            qulonglong selectionRevision = 1;
            const QString anchorId = session->entryIdAt(40);
            QVERIFY(!anchorId.isEmpty());
            for (Qt::Key key : keys) {
                // A selected anchor fixes this gesture to removal, including
                // when the cursor crosses rows that are not selected.
                QVERIFY(session->applyExternalState(
                    anchorId, 40, QStringList{anchorId},
                    ++selectionRevision));
                QVERIFY(invokeEnsureCurrentVisible(panel, false));
                QVERIFY(QMetaObject::invokeMethod(
                    panel, "cancelCursorChromeTransition",
                    Qt::DirectConnection));
                transactionSpy.clear();

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
                QCOMPARE(transactionSpy.size(), 0);
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
                // Moving onto an unselected target must not change the mode
                // while Shift is still held.
                QCoreApplication::sendEvent(&view, &keyPress);
                QVERIFY(!panel->property("keyboardShiftSelectionAdds").toBool());


                QKeyEvent keyRelease(QEvent::KeyRelease, key,
                                     Qt::ShiftModifier);
                QCoreApplication::sendEvent(&view, &keyRelease);
                QCOMPARE(transactionSpy.size(), 0);
                QKeyEvent shiftRelease(QEvent::KeyRelease, Qt::Key_Shift,
                                       Qt::NoModifier);
                QCoreApplication::sendEvent(&view, &shiftRelease);
                QVERIFY(!panel->property(
                    "keyboardShiftSelectionActive").toBool());
                QCOMPARE(transactionSpy.size(), 1);
                const QVariantList changes = transactionSpy.constFirst().at(0).toList();
                QCOMPARE(changes.size(), 1);
                QCOMPARE(changes.first().toMap().value("entryId").toString(), anchorId);
                QVERIFY(!changes.first().toMap().value("selected").toBool());
                const QStringList acknowledged;
                QVERIFY(session->applyExternalState(
                    session->cursorEntryId(), session->currentIndex(),
                    acknowledged,
                    ++selectionRevision));
                // A new physical Shift press starts a fresh gesture.
                QCoreApplication::sendEvent(&view, &shiftPress);
                QVERIFY(panel->property("keyboardShiftSelectionAdds").toBool());
                QCoreApplication::sendEvent(&view, &shiftRelease);
            }
        }
    }

    void heldInsertPreviewsEveryVisitedRowAndCommitsOnceInEveryMode_data() {
        QTest::addColumn<bool>("liveUpdates");
        QTest::newRow("deferred") << false;
        QTest::newRow("live") << true;
    }

    void heldInsertPreviewsEveryVisitedRowAndCommitsOnceInEveryMode() {
        QFETCH(bool, liveUpdates);
        const QStringList modes = {
            QStringLiteral("masonry"), QStringLiteral("grid"),
            QStringLiteral("icons"), QStringLiteral("details"),
            QStringLiteral("columns")};

        for (const QString &mode : modes) {
            QQuickView view;
            ZoinGallery::RuntimeOptions options;
            options.persistentCache = false;
            auto *runtime = ZoinGallery::GalleryRuntime::install(
                view.engine(), options);
            QVERIFY(runtime);
            auto *session = runtime->createExternalSession(
                QStringLiteral("held-insert-%1").arg(mode));
            QVERIFY(session);
            QVERIFY(session->applyExternalCatalog(plainCatalog(12), 1));
            session->setCurrentIndex(1);
            QObject *panel = createPanel(
                view, session, QStringLiteral("heldInsertSession"), mode);
            QVERIFY(panel);
            QVERIFY(panel->setProperty("liveSelectionUpdates", liveUpdates));
            auto *panelItem = qobject_cast<QQuickItem *>(panel);
            auto *layout = panel->findChild<MasonryLayout *>(
                QStringLiteral("galleryViewportItem"));
            QVERIFY(panelItem);
            QVERIFY(layout);
            QTRY_COMPARE_WITH_TIMEOUT(layout->count(), 12, 3000);
            panelItem->forceActiveFocus();
            view.requestActivate();

            QSignalSpy immediateSelectionSpy(
                panel, SIGNAL(selectionRequested(QString,QVariant)));
            QSignalSpy transactionSpy(
                panel, SIGNAL(selectionTransactionRequested(
                    QVariant,QString,int)));
            QSignalSpy cursorSpy(
                panel, SIGNAL(cursorRequested(QString,int,bool)));
            QVERIFY(immediateSelectionSpy.isValid());
            QVERIFY(transactionSpy.isValid());
            QVERIFY(cursorSpy.isValid());

            const auto sendInsertPress = [&](bool autoRepeat) {
                QKeyEvent event(QEvent::KeyPress, Qt::Key_Insert,
                                Qt::NoModifier, QString(), autoRepeat, 1);
                QCoreApplication::sendEvent(&view, &event);
                QVERIFY(event.isAccepted());
            };
            sendInsertPress(false);
            QCOMPARE(session->currentIndex(), 2);
            for (int repeat = 0; repeat < 4; ++repeat) {
                QKeyEvent repeatRelease(
                    QEvent::KeyRelease, Qt::Key_Insert, Qt::NoModifier,
                    QString(), true, 1);
                QCoreApplication::sendEvent(&view, &repeatRelease);
                sendInsertPress(true);
                QCOMPARE(session->currentIndex(), 3 + repeat);
            }
            QCOMPARE(immediateSelectionSpy.size(), 0);
            QCOMPARE(transactionSpy.size(), 0);
            QVERIFY(panel->property("keyboardToggleSelectionActive").toBool());
            for (const QList<QVariant> &request : std::as_const(cursorSpy))
                QVERIFY(request.at(2).toBool());

            if (liveUpdates) {
                QTRY_COMPARE_WITH_TIMEOUT(transactionSpy.size(), 1, 250);
                QVERIFY(panel->property("keyboardToggleSelectionActive").toBool());
            }
            QKeyEvent release(QEvent::KeyRelease, Qt::Key_Insert,
                              Qt::NoModifier);
            QCoreApplication::sendEvent(&view, &release);
            QVERIFY(release.isAccepted());
            QVERIFY(!panel->property(
                "keyboardToggleSelectionActive").toBool());
            QCOMPARE(transactionSpy.size(), 1);
            QCOMPARE(immediateSelectionSpy.size(), 0);

            const QList<QVariant> transaction = transactionSpy.constFirst();
            const QVariantList changes = transaction.at(0).toList();
            QCOMPARE(changes.size(), 5);
            QSet<QString> changedIds;
            for (const QVariant &value : changes) {
                const QVariantMap change = value.toMap();
                QVERIFY(change.value(QStringLiteral("selected")).toBool());
                changedIds.insert(
                    change.value(QStringLiteral("entryId")).toString());
            }
            for (int index = 1; index <= 5; ++index)
                QVERIFY(changedIds.contains(session->entryIdAt(index)));
            QCOMPARE(transaction.at(1).toString(), session->entryIdAt(6));
            QCOMPARE(transaction.at(2).toInt(), 6);
            for (const QList<QVariant> &request : std::as_const(cursorSpy))
                QVERIFY(request.at(2).toBool());
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
            QStringLiteral("galleryViewportItem"));
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
            QStringLiteral("galleryViewportItem"));
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
        QSignalSpy transactionSpy(
            panel, SIGNAL(selectionTransactionRequested(
                QVariant,QString,int)));
        QVERIFY(cursorSpy.isValid());
        QVERIFY(transactionSpy.isValid());

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

        // Shift keeps the same page geometry and paints its range locally.
        // Selection and cursor are committed atomically on physical Shift
        // release, never once per autorepeat.
        cursorSpy.clear();
        transactionSpy.clear();
        const int selectedBeforeShift = session->currentIndex();
        const qreal beforeShiftX =
            panel->property("currentItemCenterX").toReal();
        const qreal beforeShiftY =
            panel->property("currentItemCenterY").toReal();
        sendPress(Qt::Key_PageDown, Qt::ShiftModifier);
        QVERIFY(session->currentIndex() > selectedBeforeShift);
        QCOMPARE(transactionSpy.size(), 0);
        QCOMPARE(finalCommitCount(), 0);
        sendPress(Qt::Key_PageDown, Qt::ShiftModifier, true);
        QCOMPARE(transactionSpy.size(), 0);
        QCOMPARE(finalCommitCount(), 0);
        sendRelease(Qt::Key_PageDown, Qt::ShiftModifier);
        QCOMPARE(transactionSpy.size(), 0);
        sendRelease(Qt::Key_Shift);
        QCOMPARE(transactionSpy.size(), 1);
        const QList<QVariant> transaction = transactionSpy.constFirst();
        const QVariantList changes = transaction.at(0).toList();
        QVERIFY(changes.size() >= 2);
        QVERIFY(std::any_of(
            changes.cbegin(), changes.cend(), [&](const QVariant &value) {
                const QVariantMap change = value.toMap();
                return change.value(QStringLiteral("entryId")).toString()
                           == QStringLiteral("layout-entry-%1")
                                  .arg(selectedBeforeShift)
                    && change.value(QStringLiteral("selected")).toBool();
            }));
        QCOMPARE(transaction.at(1).toString(), session->cursorEntryId());
        QCOMPARE(transaction.at(2).toInt(), session->currentIndex());
        QVERIFY(qAbs(panel->property("currentItemCenterX").toReal() -
                     beforeShiftX) <= 0.01);
        QVERIFY(qAbs(panel->property("currentItemCenterY").toReal() -
                     beforeShiftY) <= 0.01);
        QCOMPARE(finalCommitCount(), 0);
        QTRY_VERIFY_WITH_TIMEOUT(
            !animation->property("running").toBool(), 1000);
        QCOMPARE(finalCommitCount(), 0);

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
            QStringLiteral("galleryViewportItem"));
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
            QStringLiteral("galleryViewportItem"));
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
            QStringLiteral("galleryViewportItem"));
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
            QStringLiteral("galleryViewportItem"));
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
            QStringLiteral("galleryViewportItem"));
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
            QStringLiteral("galleryViewportItem"));
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
        QVERIFY(setPanelObjectProperties(panel, "theme", QVariantMap{
            {QStringLiteral("cursorBackground"),
             QStringLiteral("#18456e")},
            {QStringLiteral("cursorBorder"), QStringLiteral("#1d5888")},
        }));
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
            QStringLiteral("galleryViewportItem"));
        QObject *animation = panel->findChild<QObject *>(
            QStringLiteral("galleryPanelScrollAnimation"));
        QObject *chromeAnimation = panel->findChild<QObject *>(
            QStringLiteral("galleryCursorChromeGeometryAnimation"));
        QVERIFY(panelItem);
        QVERIFY(layout);
        QVERIFY(animation);
        QVERIFY(chromeAnimation);
        QVERIFY(setPanelObjectProperties(panel, "theme", QVariantMap{
            {QStringLiteral("cursorBackground"), QStringLiteral("#18456e")},
            {QStringLiteral("cursorBorder"), QStringLiteral("#1d5888")},
            {QStringLiteral("markedBackground"), QStringLiteral("#4f5037")},
        }));
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
            QStringLiteral("galleryViewportItem"));
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
            QStringLiteral("galleryViewportItem"));
        QVERIFY(layout);
        layout->setDensity(180);

        QQuickItem *thumbnail = nullptr;
        QQuickItem *thumbnailImage = nullptr;
        QQuickItem *thumbnailShader = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT(
            (thumbnail = panel->findChild<QQuickItem *>(
                 QStringLiteral("galleryThumbnail-0")))
                && (thumbnailImage = panel->findChild<QQuickItem *>(
                    QStringLiteral("galleryThumbnailImage-0")))
                && (thumbnailShader = panel->findChild<QQuickItem *>(
                    QStringLiteral("galleryThumbnailShader-0"))),
            10000);
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

        // Keep the delayed row in the bounded one-row Masonry metadata
        // overscan. The planner no longer prefetches an entire extra viewport
        // on each side merely to exercise this completion path.
        constexpr int imageRow = 6;
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
            QStringLiteral("galleryViewportItem"));
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
        info.sourceVersionToken = QString::number(sourceVersion);
        info.imageSize = source.size();
        info.orientation = ExifOrientation::Horizontal;
        info.requestNamespace = session->sessionId();
        decodeManager->imageInfoReady(info);

        QTRY_COMPARE_WITH_TIMEOUT(image->fullSize(), source.size(), 5000);
        QVERIFY(external->data(external->index(imageRow, 0),
            ZoinGallery::ExternalCatalogModel::VisualSnapshotRole).toMap()
            .value("imageDimensionsKnown").toBool());
        QCOMPARE(layout->contentY(), contentYBefore);
        QCOMPARE(layout->visibleIndexes(), visibleBefore);
        QCOMPARE(layout->overscanIndexes(), overscanBefore);
        QTRY_VERIFY_WITH_TIMEOUT(!image->imageIdUrl().isEmpty(), 10000);
    }

    void masonryThumbnailsConvergeAfterCatalogReorder() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QString sourceDirectory = qEnvironmentVariable("ZOIN_THUMBNAIL_REGRESSION_DIR");
        if (sourceDirectory.isEmpty()) {
            sourceDirectory = directory.path();
            for (int i = 0; i < 80; ++i) {
                QImage image(i % 4 == 1 ? QSize(384, 512) : QSize(512, 384),
                             QImage::Format_RGB32);
                image.fill(QColor::fromHsv(i * 13 % 360, 160, 200));
                QVERIFY(image.save(directory.filePath(QStringLiteral("image-%1.png")
                                                     .arg(i, 3, 10, QLatin1Char('0')))));
            }
        }
        const QFileInfoList files = QDir(sourceDirectory).entryInfoList(
            {QStringLiteral("*.jpg"), QStringLiteral("*.png")}, QDir::Files, QDir::Name);
        QVERIFY(files.size() >= 20);
        QVariantList catalog;
        for (int row = 0; row < files.size(); ++row) {
            catalog.append(catalogEntry(row, files.at(row).absoluteFilePath()));
        }
        QQuickView view;
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        options.maxDecodeThreads = 4;
        auto *runtime = ZoinGallery::GalleryRuntime::install(view.engine(), options);
        QVERIFY(runtime);
        auto *left = runtime->createExternalSession(QStringLiteral("convergence-left"));
        auto *right = runtime->createExternalSession(QStringLiteral("convergence-right"));
        QVariantList provisionalCatalog;
        for (int row = 0; row < catalog.size(); row += 3) {
            auto entry = catalog.at(row).toMap();
            entry.insert(QStringLiteral("index"), provisionalCatalog.size());
            provisionalCatalog.append(entry);
        }
        QVERIFY(left->applyExternalCatalog(provisionalCatalog, 1));
        QVERIFY(right->applyExternalCatalog(catalog, 1));
        view.engine()->addImportPath(QStringLiteral(ZOIN_TEST_QML_IMPORT_PATH));
        view.engine()->rootContext()->setContextProperty(QStringLiteral("leftSession"), left);
        view.engine()->rootContext()->setContextProperty(QStringLiteral("rightSession"), right);
        auto *component = new QQmlComponent(view.engine(), &view);
        const QUrl url(QStringLiteral("inline:ThumbnailConvergence.qml"));
        component->setData(R"QML(
            import QtQuick
            import ZoinGallery 1.0
            Item {
                width: 1280; height: 720
                GalleryPanel {
                    objectName: "leftPanel"
                    width: 640; height: 720
                    session: leftSession
                    presentationMode: "masonry"
                    autoFocus: false
                }
                GalleryPanel {
                    objectName: "rightPanel"
                    x: 640; width: 640; height: 720
                    session: rightSession
                    presentationMode: "masonry"
                    autoFocus: false
                }
            }
        )QML", url);
        QTRY_VERIFY_WITH_TIMEOUT(component->isReady(), 5000);
        QObject *root = component->create();
        QVERIFY(root);
        view.setContent(url, component, root);
        view.show();
        auto *panel = root->findChild<QQuickItem *>(QStringLiteral("leftPanel"));
        QVERIFY(panel);
        auto *layout = panel->findChild<MasonryLayout *>(QStringLiteral("galleryViewportItem"));
        QVERIFY(layout);
        QTRY_VERIFY_WITH_TIMEOUT(!layout->visibleIndexes().isEmpty(), 5000);
        const auto missingThumbnails = [&]() {
            QStringList missing;
            for (const QVariant &value : layout->visibleIndexes()) {
                const int row = value.toInt();
                const QModelIndex index = left->model()->index(row, 0);
                auto *thumbnail = panel->findChild<QQuickItem *>(
                    QStringLiteral("galleryThumbnail-%1").arg(row));
                const QString modelUrl = index.data(FileListModel::ImageIdUrlRole).toString();
                const QString visualUrl = thumbnail ? thumbnail->property("source").toUrl().toString() : QString();
                if (modelUrl.isEmpty() || visualUrl != modelUrl) {
                    missing.append(QStringLiteral("row=%1 model=%2 visual=%3 size=%4x%5")
                        .arg(row).arg(modelUrl, visualUrl)
                        .arg(index.data(FileListModel::ImageFullSizeRole).toSize().width())
                        .arg(index.data(FileListModel::ImageFullSizeRole).toSize().height()));
                }
            }
            return missing.join(QLatin1Char('\n'));
        };
        QTRY_VERIFY2_WITH_TIMEOUT(missingThumbnails().isEmpty(), qPrintable(missingThumbnails()), 15000);
        // Directory enumeration adds rows between already visible entries.
        // Rewraps during these insertions must keep the same image facades.
        QVERIFY(left->applyExternalCatalog(catalog, 2));
        QTRY_VERIFY2_WITH_TIMEOUT(missingThumbnails().isEmpty(), qPrintable(missingThumbnails()), 15000);
        for (int cycle = 0; cycle < 3; ++cycle) {
            std::reverse(catalog.begin(), catalog.end());
            for (int row = 0; row < catalog.size(); ++row) {
                auto entry = catalog.at(row).toMap();
                entry.insert(QStringLiteral("index"), row);
                catalog[row] = entry;
            }
            QVERIFY(left->applyExternalCatalog(catalog, cycle + 3));
            for (qreal fraction : {0.1, 0.3, 0.1, 0.0}) {
                layout->setContentY(qMax<qreal>(0, layout->contentHeight() - layout->height()) * fraction);
                QTRY_VERIFY2_WITH_TIMEOUT(missingThumbnails().isEmpty(), qPrintable(missingThumbnails()), 15000);
            }
        }
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
            QStringLiteral("galleryViewportItem"));
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
                {QStringLiteral("name"), QStringLiteral("photo.js")},
                {QStringLiteral("isDir"), false},
                {QStringLiteral("isImage"), false},
                {QStringLiteral("size"), qint64(8192)},
                {QStringLiteral("displayBaseName"), QStringLiteral("photo")},
                {QStringLiteral("displayExtension"),
                 QStringLiteral("js")},
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
                 QStringLiteral("js"));
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
            QStringLiteral("galleryViewportItem"));
        QVERIFY(layout);
        QTRY_COMPARE_WITH_TIMEOUT(layout->presentationMode(),
                                  MasonryLayout::Columns, 3000);
        // A Loader releases the outgoing Details subtree with deleteLater().
        // During that event-loop turn QObject discovery can still see the
        // detached object, so select the active Columns visual by its explicit
        // right-aligned contract rather than by construction order.
        extension0 = nullptr;
        const auto extensionCandidates =
            panel->findChildren<QQuickItem *>(
                QStringLiteral("galleryExtension-0"));
        for (QQuickItem *candidate : extensionCandidates) {
            if (candidate->property("horizontalAlignment").toInt()
                == int(Qt::AlignRight)) {
                extension0 = candidate;
                break;
            }
        }
        size0 = nullptr;
        const auto sizeCandidates = panel->findChildren<QQuickItem *>(
            QStringLiteral("gallerySize-0"));
        for (QQuickItem *candidate : sizeCandidates) {
            if (extension0 && candidate->parentItem()
                == extension0->parentItem()) {
                size0 = candidate;
                break;
            }
        }
        QVERIFY(extension0);
        QVERIFY(size0);
        QTRY_VERIFY_WITH_TIMEOUT(!size0->isVisible(), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(extension0->parentItem(), 3000);
        const qreal extensionRight = extension0->x() + extension0->width();
        const qreal extensionParentWidth = extension0->parentItem()->width();
        QVERIFY2(qAbs(extensionRight - extensionParentWidth) <= 0.51,
                 qPrintable(QStringLiteral(
                     "extension right=%1 parent width=%2 parent=%3")
                     .arg(extensionRight).arg(extensionParentWidth)
                     .arg(extension0->parentItem()->objectName())));
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
        QVERIFY(setPanelObjectProperties(panel, "theme", QVariantMap{
            {QStringLiteral("panelBackground"),
             QStringLiteral("transparent")},
            {QStringLiteral("text"), QStringLiteral("#e8edf2")},
            {QStringLiteral("mutedText"), QStringLiteral("#9aa7b5")},
            {QStringLiteral("fileText"), QStringLiteral("#c4cbd3")},
            {QStringLiteral("folderText"), QStringLiteral("#ffffff")},
            {QStringLiteral("neutralFileTextColors"), true},
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
        }));
        QVERIFY(setPanelObjectProperties(panel, "metrics", QVariantMap{
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
        }));
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
            QStringLiteral("galleryViewportItem"));
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
                 QColor(QStringLiteral("#c4cbd3")));
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
                 QColor(QStringLiteral("#ffffff")));
        QCOMPARE(markedSurface->property("color").value<QColor>(),
                 QColor(Qt::transparent));
        QCOMPARE(markedSurface->property("visualBorderWidth").toReal(), 1.0);
        QCOMPARE(markedSurface->property("visualBorderColor").value<QColor>(),
                 panel->property("selectionColor").value<QColor>());

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

        // Folder labels stay at the configurable neutral folder color. Their
        // Lucide icon is blue normally, becomes white under the cursor, and
        // persistent selection remains the strongest state.
        session->setCurrentIndex(2);
        QTRY_COMPARE_WITH_TIMEOUT(
            plainFolderIcon->property("effectiveIconColor").value<QColor>(),
            QColor(QStringLiteral("#e8edf2")), 3000);
        QCOMPARE(plainFolderBase->property("color").value<QColor>(),
                 QColor(QStringLiteral("#ffffff")));
        QTRY_VERIFY_WITH_TIMEOUT(scrollBar->isVisible(), 3000);
        QCOMPARE(scrollBar->width(), 16.0);
        QCOMPARE(scrollBar->x(), 632.0);
        // The host reserves an 8px trailing panel inset. The 16px overlay is
        // anchored into that lane, while Details keeps its full 640px row.
        QCOMPARE(layout->width(), panelItem->width());
    }

    void compactModesCanUseMarkedTextWithoutSelectionBorder() {
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
            QVERIFY(setPanelObjectProperties(panel, "theme", QVariantMap{
                {QStringLiteral("showSelectionBorders"), false},
                {QStringLiteral("markedText"),
                 QStringLiteral("#ffd43b")},
                {QStringLiteral("selection"),
                 QStringLiteral("#ffd43b")},
            }));
            auto *layout = panel->findChild<MasonryLayout *>(
                QStringLiteral("galleryViewportItem"));
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

    void pendingThumbnailDoesNotFlashFallbackIcon() {
        QQuickView view;
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        auto *runtime = ZoinGallery::GalleryRuntime::install(view.engine(), options);
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(QStringLiteral("pending-thumbnail-icon"));
        QVERIFY(session->applyExternalCatalog(plainCatalog(1), 1));
        auto *panel = createPanel(view, session, QStringLiteral("pendingThumbnailSession"));
        QVERIFY(panel);
        QQuickItem *icon = nullptr;
        QTRY_VERIFY((icon = panel->findChild<QQuickItem *>(QStringLiteral("galleryFallbackIcon-0"))));
        auto *preview = icon->parentItem();
        auto *entry = preview->property("entry").value<QObject *>();
        QVERIFY(entry);
        QTest::qWait(150);
        QVERIFY(icon->isVisible());
        auto snapshot = entry->property("visualRow").toMap();
        snapshot.insert("isImage", true);
        snapshot.insert("imageDimensionsKnown", true);
        snapshot.insert("imageIdUrl", "");
        QVERIFY(entry->setProperty("visualRow", snapshot));
        QVERIFY(!icon->isVisible());
        snapshot.insert("imageIdUrl", "file:///missing-thumbnail-regression.png");
        QVERIFY(entry->setProperty("masonryGeometryReady", false));
        QVERIFY(entry->setProperty("visualRow", snapshot));
        // The cached URL is known before geometry allows the pixel request.
        QVERIFY(!preview->property("thumbnailHasSource").toBool());
        QVERIFY(!icon->isVisible());
        QVERIFY(entry->setProperty("masonryGeometryReady", true));
        // A failed request must still restore the fallback.
        QTRY_VERIFY(icon->isVisible());
        snapshot.insert("imageIdUrl", "");
        snapshot.insert("isImage", false);
        snapshot.insert("imageDimensionsKnown", false);
        QVERIFY(entry->setProperty("visualRow", snapshot));
        QVERIFY(icon->isVisible());
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
            const qreal windowDpr = view.devicePixelRatio();
            if (qAbs(windowDpr - 1.75) < 0.001)
                panel->setProperty("devicePixelRatio", windowDpr);
            session->setCurrentIndex(3);
            QVERIFY(setPanelObjectProperties(panel, "theme", QVariantMap{
                {QStringLiteral("panelBackground"),
                 QStringLiteral("#141922")},
                {QStringLiteral("mutedText"),
                 QStringLiteral("#5ab2f1")},
            }));
            auto *layout = panel->findChild<MasonryLayout *>(
                QStringLiteral("galleryViewportItem"));
            QVERIFY(layout);
            QTRY_COMPARE_WITH_TIMEOUT(layout->count(), 4, 3000);
            auto *icon = panel->findChild<QQuickItem *>(
                QStringLiteral("galleryFallbackIcon-0"));
            QTRY_VERIFY_WITH_TIMEOUT(icon, 3000);
            QVERIFY2(icon->isVisible(), qPrintable(QStringLiteral(
                "mode=%1 visible=%2 explicitVisible=%3 source=%4 "
                "lucide=%5 system=%6 thumbnail=%7 parentVisible=%8 "
                "parent=%9")
                .arg(mode)
                .arg(icon->isVisible())
                .arg(icon->property("visible").toBool())
                .arg(icon->property("source").toUrl().toString())
                .arg(icon->property("lucideSource").toBool())
                .arg(icon->property("systemFileSource").toBool())
                .arg(icon->parentItem()
                         ? icon->parentItem()->property("source")
                               .toUrl().toString() : QString())
                .arg(icon->parentItem()
                         ? icon->parentItem()->isVisible() : false)
                .arg(icon->parentItem()
                         ? icon->parentItem()->objectName() : QString())));
            QCOMPARE(icon->width(), 16.0);
            QCOMPARE(icon->height(), 16.0);
            QCOMPARE(icon->opacity(), 1.0);
            if (mode == QStringLiteral("columns")) {
                QCOMPARE(icon->property("source").toUrl(),
                         QUrl(QStringLiteral(
                             "qrc:/F4QtHost/icons/lucide/folder.svg")));
            }

            if (qAbs(windowDpr - 1.75) < 0.001
                && QFile::exists(QStringLiteral(
                    ":/F4QtHost/icons/lucide/folder.svg"))) {
                QTRY_COMPARE_WITH_TIMEOUT(icon->property("status").toInt(),
                                          1, 3000); // Image.Ready
                view.update();
                QTest::qWait(50);
                const QImage frame = view.grabWindow().convertToFormat(
                    QImage::Format_ARGB32_Premultiplied);
                QVERIFY(!frame.isNull());

                const QPointF sceneOrigin = icon->mapToScene(QPointF{});
                const QRect physicalRect(
                    QPoint(qRound(sceneOrigin.x() * windowDpr),
                           qRound(sceneOrigin.y() * windowDpr)),
                    QSize(qRound(icon->width() * windowDpr),
                          qRound(icon->height() * windowDpr)));
                QVERIFY(frame.rect().contains(physicalRect));

                const QColor expected = icon->property(
                    "effectiveIconColor").value<QColor>();
                int closestDistance = std::numeric_limits<int>::max();
                QPoint closestPoint;
                QColor closestColor;
                for (int y = physicalRect.top();
                     y <= physicalRect.bottom(); ++y) {
                    for (int x = physicalRect.left();
                         x <= physicalRect.right(); ++x) {
                        const QColor actual = frame.pixelColor(x, y);
                        const int distance = qAbs(actual.red() - expected.red())
                            + qAbs(actual.green() - expected.green())
                            + qAbs(actual.blue() - expected.blue());
                        if (distance < closestDistance) {
                            closestDistance = distance;
                            closestPoint = QPoint(x, y);
                            closestColor = actual;
                        }
                    }
                }
                const QByteArray tintDetails = QStringLiteral(
                    "%1 icon has no theme-tinted stroke: expected %2, "
                    "closest %3 at physical (%4,%5), icon physical rect "
                    "(%6,%7 %8x%9), logical scene origin (%10,%11), DPR %12")
                    .arg(mode, expected.name(QColor::HexArgb),
                         closestColor.name(QColor::HexArgb))
                    .arg(closestPoint.x()).arg(closestPoint.y())
                    .arg(physicalRect.x()).arg(physicalRect.y())
                    .arg(physicalRect.width()).arg(physicalRect.height())
                    .arg(sceneOrigin.x(), 0, 'f', 6)
                    .arg(sceneOrigin.y(), 0, 'f', 6)
                    .arg(windowDpr, 0, 'f', 2).toUtf8();
                QVERIFY2(closestDistance <= 6, tintDetails.constData());

                const auto verifyPhysicalEdge = [windowDpr, &mode](
                        qreal edge, const QString &name) {
                    const qreal physical = edge * windowDpr;
                    const QByteArray details = QStringLiteral(
                        "%1 icon %2 edge is %3 physical pixels at DPR %4")
                        .arg(mode, name).arg(physical, 0, 'f', 6)
                        .arg(windowDpr, 0, 'f', 2).toUtf8();
                    QVERIFY2(qAbs(physical - qRound(physical)) < 0.001,
                             details.constData());
                };
                verifyPhysicalEdge(sceneOrigin.x(), QStringLiteral("left"));
                verifyPhysicalEdge(sceneOrigin.y(), QStringLiteral("top"));
                verifyPhysicalEdge(sceneOrigin.x() + icon->width(),
                                   QStringLiteral("right"));
                verifyPhysicalEdge(sceneOrigin.y() + icon->height(),
                                   QStringLiteral("bottom"));
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

    void thumbnailsDisabledIgnoreLateImageDimensions_data() {
        QTest::addColumn<QString>("mode");
        for (const auto &mode : {"grid", "icons", "details", "columns"})
            QTest::newRow(mode) << QString::fromLatin1(mode);
    }

    void thumbnailsDisabledIgnoreLateImageDimensions() {
        QFETCH(QString, mode);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("late-size.png"));
        QImage fixtureImage(160, 80, QImage::Format_ARGB32_Premultiplied);
        fixtureImage.fill(QColor(QStringLiteral("#2478b9")));
        QVERIFY(fixtureImage.save(path));

        QQuickView view;
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        auto *runtime = ZoinGallery::GalleryRuntime::install(view.engine(), options);
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(
            QStringLiteral("thumbnail-off-%1").arg(mode));
        QVERIFY(session);
        session->setThumbnailsEnabled(false);
        QVERIFY(!session->thumbnailsEnabled());
        QVERIFY(session->applyExternalCatalog({catalogEntry(0, path)}, 1, {
            {QStringLiteral("metadataDeferred"), true}}));

        QObject *panel = createPanel(
            view, session, QStringLiteral("thumbnailOffSession"), mode);
        QVERIFY(panel);
        auto *layout = panel->findChild<MasonryLayout *>(
            QStringLiteral("galleryViewportItem"));
        QVERIFY(layout);
        panel->setProperty("devicePixelRatio", view.devicePixelRatio());
        QTRY_COMPARE_WITH_TIMEOUT(layout->count(), 1, 3000);
        auto *file = session->model()->index(0, 0)
            .data(FileListModel::ImageFileRole).value<ImageFile *>();
        QVERIFY(file);
        auto *thumbnail = panel->findChild<QQuickItem *>(
            QStringLiteral("galleryThumbnail-0"));
        auto *fallback = panel->findChild<QQuickItem *>(
            QStringLiteral("galleryFallbackIcon-0"));
        QTRY_VERIFY(thumbnail && fallback);
        QTRY_VERIFY(fallback->isVisible());
        QVERIFY(thumbnail->property("source").toUrl().isEmpty());
        const QRectF fixedGeometry = layout->indexGeometry(0);
        panel->setProperty("presentationMode", QStringLiteral("masonry"));
        QTRY_COMPARE_WITH_TIMEOUT(layout->presentationMode(),
                                  MasonryLayout::Masonry, 3000);
        const QRectF expectedDisabledGeometry = layout->indexGeometry(0);
        panel->setProperty("presentationMode", mode);
        QTRY_VERIFY_WITH_TIMEOUT(
            layout->presentationMode() != MasonryLayout::Masonry, 3000);
        QCOMPARE(layout->indexGeometry(0), fixedGeometry);

        ImageInfo info;
        info.path = QFileInfo(path).absoluteFilePath();
        info.requestNamespace = session->sessionId();
        info.imageSize = QSize(160, 80);
        info.fileSize = QFileInfo(path).size();
        info.lastModified = QFileInfo(path).lastModified();
        auto *decoder = runtime->findChild<DecodeManager *>();
        QVERIFY(decoder);
        decoder->imagesInfoReady({info});
        QTRY_VERIFY_WITH_TIMEOUT(file->fullSize().isValid(), 3000);
        QCOMPARE(thumbnail->property("source").toUrl(), QUrl());
        QCOMPARE(layout->indexGeometry(0), fixedGeometry);

        panel->setProperty("presentationMode", QStringLiteral("masonry"));
        QTRY_COMPARE_WITH_TIMEOUT(layout->presentationMode(),
                                  MasonryLayout::Masonry, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(layout->indexGeometry(0).isValid(), 3000);
        const QRectF disabledGeometry = layout->indexGeometry(0);
        QCOMPARE(disabledGeometry, expectedDisabledGeometry);
        QVERIFY(thumbnail->property("source").toUrl().isEmpty());

        const qreal dpr = view.devicePixelRatio();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        const QPointF fallbackOrigin = fallback->mapToScene(QPointF());
        for (const qreal coordinate : {fallbackOrigin.x() * dpr,
                                       fallbackOrigin.y() * dpr}) {
            QVERIFY2(qAbs(coordinate - qRound(coordinate)) < 0.01,
                     qPrintable(QStringLiteral("%1 at %2 physical px")
                         .arg(fallback->objectName()).arg(coordinate)));
        }
        QCOMPARE(fallback->mapToScene(QPointF(1, 0)) - fallbackOrigin,
                 QPointF(1, 0));
        QCOMPARE(fallback->mapToScene(QPointF(0, 1)) - fallbackOrigin,
                 QPointF(0, 1));
        const QImage frame = view.grabWindow();
        QVERIFY(!frame.isNull());
        const QRect iconRect(
            qRound(fallbackOrigin.x() * dpr),
            qRound(fallbackOrigin.y() * dpr),
            qRound(fallback->width() * dpr),
            qRound(fallback->height() * dpr));
        QVERIFY(frame.rect().contains(iconRect));
        const QImage iconCapture = frame.copy(iconRect);
        if (!qEnvironmentVariable("F4_THUMBNAIL_POLICY_CAPTURE").isEmpty())
            QVERIFY(iconCapture.save(qEnvironmentVariable(
                "F4_THUMBNAIL_POLICY_CAPTURE")));
        QSet<QRgb> renderedColors;
        for (int y = 0; y < iconCapture.height(); ++y) {
            for (int x = 0; x < iconCapture.width(); ++x)
                renderedColors.insert(iconCapture.pixel(x, y));
        }
        QVERIFY(renderedColors.size() > 1);

        session->setThumbnailsEnabled(true);
        QTRY_VERIFY_WITH_TIMEOUT(
            layout->indexGeometry(0).size()
                != expectedDisabledGeometry.size(),
            3000);
        runtime->shutdown();
    }

    void columnsTextUsesPhysicalPixelGrid() {
        QQuickView view;
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        auto *runtime = ZoinGallery::GalleryRuntime::install(view.engine(), options);
        auto *session = runtime->createExternalSession(QStringLiteral("columns-pixel-grid"));
        QVERIFY(session->applyExternalCatalog(plainCatalog(80), 1));
        auto *panel = createPanel(view, session, QStringLiteral("columnsPixelSession"), QStringLiteral("columns"));
        QVERIFY(panel);
        auto *item = qobject_cast<QQuickItem *>(panel);
        auto *layout = panel->findChild<MasonryLayout *>(QStringLiteral("galleryViewportItem"));
        QVERIFY(item && layout);
        const qreal dpr = view.devicePixelRatio();
        panel->setProperty("devicePixelRatio", dpr);
        item->setX(.25);
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        for (int columns : {2, 3}) {
            layout->setColumnCount(columns);
            panel->setProperty("separateFileExtensions", columns == 3);
            item->setX(columns == 3 ? .375 : .25);
            item->setWidth(columns == 3 ? 631.5 : 640);
            QTest::qWait(100);
            int checked = 0;
            const auto visit = [&](auto &&self, QQuickItem *leaf) -> void {
                if (leaf->isVisible() && (leaf->objectName().startsWith("galleryBaseName-")
                    || leaf->objectName().startsWith("galleryExtension-")
                    || leaf->objectName().startsWith("galleryFallbackIcon-"))) {
                    const auto origin = leaf->mapToScene(QPointF());
                    const auto physical = origin * dpr;
                    QVERIFY2(qAbs(physical.x()-qRound64(physical.x())) < .001
                        && qAbs(physical.y()-qRound64(physical.y())) < .001,
                        qPrintable(QString("%1 columns=%2 physical=%3,%4")
                            .arg(leaf->objectName()).arg(columns).arg(physical.x()).arg(physical.y())));
                    QCOMPARE(leaf->mapToScene(QPointF(1,0))-origin, QPointF(1,0));
                    QCOMPARE(leaf->mapToScene(QPointF(0,1))-origin, QPointF(0,1));
                    ++checked;
                }
                for (auto *child : leaf->childItems()) self(self,child);
            };
            visit(visit,item);
            QVERIFY(checked > columns);
            if (qEnvironmentVariableIsSet("F4_COLUMNS_CAPTURE"))
                QVERIFY(view.grabWindow().save(qEnvironmentVariable("F4_COLUMNS_CAPTURE")+QString::number(columns)+".png"));
        }
    }

    void masonryFirstRowLucideProviderUsesPhysicalRasterAndGrid() {
        QQuickView view;
        auto *checkerProvider = new CompactIconProvider(true);
        view.engine()->addImageProvider(
            QStringLiteral("masonry-icons"), checkerProvider);
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        auto *runtime = ZoinGallery::GalleryRuntime::install(
            view.engine(), options);
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(
            QStringLiteral("masonry-first-row-pixel-grid"));
        QVERIFY(session);
        QVERIFY(session->applyExternalCatalog(plainCatalog(4), 1));
        QVERIFY(session->applyExternalAppearance({QVariantMap{
            {QStringLiteral("entryId"), QStringLiteral("layout-entry-0")},
            {QStringLiteral("highlightStyle"), QVariantMap{
                 {QStringLiteral("icon"), QStringLiteral(
                      "image://masonry-icons/lucide/ZmlsZQ"
                      "?size=128&dpr=2&revision=1")},
             }},
        }}, 1));

        QObject *panel = createPanel(
            view, session, QStringLiteral("masonryFirstRowSession"),
            QStringLiteral("masonry"));
        QVERIFY(panel);
        auto *panelItem = qobject_cast<QQuickItem *>(panel);
        auto *layout = panel->findChild<MasonryLayout *>(
            QStringLiteral("galleryViewportItem"));
        QVERIFY(panelItem && layout);
        panelItem->setX(0.25);
        panelItem->setY(0.25);
        const qreal dpr = view.devicePixelRatio();
        panel->setProperty("devicePixelRatio", dpr);

        QTRY_COMPARE_WITH_TIMEOUT(layout->count(), 4, 3000);
        auto *icon = panel->findChild<QQuickItem *>(
            QStringLiteral("galleryFallbackIcon-0"));
        QVERIFY(icon);
        QTRY_VERIFY_WITH_TIMEOUT(icon->isVisible(), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(icon->property("status").toInt(), 1,
                                  3000);

        const QSize expectedPhysicalSize(
            qRound(icon->width() * dpr), qRound(icon->height() * dpr));
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QTRY_COMPARE(checkerProvider->lastRequestedSize, expectedPhysicalSize);
        const QPointF origin = icon->mapToScene(QPointF{});
        qInfo().nospace() << "masonry icon geometry=" << icon->x() << ','
                          << icon->y() << ' ' << icon->width() << 'x'
                          << icon->height() << " scene=" << origin
                          << " sourceSize="
                          << icon->property("sourceSize").toSize()
                          << " offset="
                          << icon->property("pixelGridOffset").toPointF();
        const auto onPhysicalGrid = [dpr](qreal value) {
            return qAbs(value * dpr - qRound(value * dpr)) < 0.001;
        };
        QVERIFY(onPhysicalGrid(origin.x()));
        QVERIFY(onPhysicalGrid(origin.y()));
        QVERIFY(onPhysicalGrid(origin.x() + icon->width()));
        QVERIFY(onPhysicalGrid(origin.y() + icon->height()));
        const QPointF unitX = icon->mapToScene(QPointF(1, 0)) - origin;
        const QPointF unitY = icon->mapToScene(QPointF(0, 1)) - origin;
        QCOMPARE(unitX, QPointF(1, 0));
        QCOMPARE(unitY, QPointF(0, 1));

        // Moving an ancestor after layout must invalidate the leaf's
        // scene-space correction even though the panel itself has not moved.
        auto *ancestor = new QQuickItem(view.contentItem());
        ancestor->setSize(QSizeF(view.width(), view.height()));
        panelItem->setParentItem(ancestor);
        ancestor->setX(0.25);
        ancestor->setY(0.25);
        QTest::qWait(100);
        const QPointF moved = icon->mapToScene(QPointF{});
        qInfo() << "first-row physical origin after ancestor movement" << moved * dpr;
        QVERIFY(onPhysicalGrid(moved.x()));
        QVERIFY(onPhysicalGrid(moved.y()));
        for (int index = 0; index < 4; ++index) {
            auto *leaf = panel->findChild<QQuickItem *>(
                QStringLiteral("galleryFallbackIcon-%1").arg(index));
            QVERIFY(leaf && leaf->isVisible());
            const QPointF position = leaf->mapToScene(QPointF{});
            QVERIFY2(onPhysicalGrid(position.x()) && onPhysicalGrid(position.y()),
                     qPrintable(QStringLiteral("%1 physical origin %2,%3")
                         .arg(leaf->objectName()).arg(position.x() * dpr)
                         .arg(position.y() * dpr)));
            QVERIFY(onPhysicalGrid(position.x() + leaf->width()));
            QVERIFY(onPhysicalGrid(position.y() + leaf->height()));
            QCOMPARE(leaf->mapToScene(QPointF(1, 0)) - position, QPointF(1, 0));
            QCOMPARE(leaf->mapToScene(QPointF(0, 1)) - position, QPointF(0, 1));
        }

        const QImage raster = view.grabWindow();
        QVERIFY(!raster.isNull());
        const QRect region(QPoint(qRound(moved.x() * dpr), qRound(moved.y() * dpr)),
                           expectedPhysicalSize);
        QVERIFY(raster.rect().contains(region));
        const QImage crop = raster.copy(region);
        const QString captureDirectory = qEnvironmentVariable("ZOIN_PIXEL_CAPTURE_DIR");
        if (!captureDirectory.isEmpty()) {
            QVERIFY(QDir().mkpath(captureDirectory));
            QVERIFY(raster.save(captureDirectory + QStringLiteral("/masonry-window.png")));
            QVERIFY(crop.save(captureDirectory + QStringLiteral("/masonry-icon.png")));
        }
        const QColor ink = crop.pixelColor(0, 0);
        const QColor background = crop.pixelColor(1, 0);
        QVERIFY(ink != background);
        for (int y = 0; y < crop.height(); ++y)
            for (int x = 0; x < crop.width(); ++x)
                QCOMPARE(crop.pixelColor(x, y), (x + y) % 2 ? background : ink);
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
        QVERIFY(sourceColourIcon && sourceMask && lucideMask);
        QVERIFY(!lucideSourceColour);
        QTRY_VERIFY_WITH_TIMEOUT(sourceColourIcon->isVisible(), 3000);
        QVERIFY(!sourceMask->isVisible());
        QVERIFY2(lucideMask->isVisible(), qPrintable(QStringLiteral(
            "lucide visible=%1 explicitVisible=%2 source=%3 "
            "parentVisible=%4 parent=%5")
            .arg(lucideMask->isVisible())
            .arg(lucideMask->property("visible").toBool())
            .arg(lucideMask->property("source").toUrl().toString())
            .arg(lucideMask->parentItem()
                     ? lucideMask->parentItem()->isVisible() : false)
            .arg(lucideMask->parentItem()
                     ? lucideMask->parentItem()->objectName() : QString())));
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
        auto *sourceColourSlot = sourceColourIcon->parentItem();
        QVERIFY(sourceColourSlot);
        while (sourceColourSlot
               && sourceColourSlot->parentItem()
                      != previewBackdrop->parentItem()) {
            sourceColourSlot = sourceColourSlot->parentItem();
        }
        QVERIFY(sourceColourSlot);
        const auto previewChildren = previewBackdrop->parentItem()->childItems();
        QVERIFY(previewChildren.indexOf(previewBackdrop)
                < previewChildren.indexOf(sourceColourSlot));

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

        auto *sourceColourLayout = panel->findChild<MasonryLayout *>(
            QStringLiteral("galleryViewportItem"));
        auto *sourceColourBrick = sourceColourLayout
            ? sourceColourLayout->currentItem() : nullptr;
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
            QStringLiteral("galleryViewportItem"));
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

    void masonryParentReentryKeepsRequestedCursor() {
        QQuickView view;
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        options.maxDecodeThreads = 2;
        auto *runtime = ZoinGallery::GalleryRuntime::install(
            view.engine(), options);
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(
            QStringLiteral("parent-reentry"));
        QVERIFY(session);
        const auto child = prefixedCatalog(QStringLiteral("child"), 100);
        const auto parent = prefixedCatalog(QStringLiteral("parent"), 80);
        QVariantList preview;
        for (int row = 0; row < child.size(); row += 3) {
            preview.append(child.at(row));
        }
        qulonglong revision = 0;
        const auto apply = [&](const QVariantList &rows,
                               const QString &path, int cursor) {
            return session->applyExternalCatalog(rows, ++revision, {
                {QStringLiteral("currentPath"), path},
                {QStringLiteral("cursorIndex"), cursor},
                {QStringLiteral("cursorEntryId"), rows.at(cursor).toMap()
                     .value(QStringLiteral("entryId"))},
            });
        };
        QVERIFY(apply(child, QStringLiteral("/child"), 0));
        QObject *panel = createPanel(
            view, session, QStringLiteral("reentrySession"));
        QVERIFY(panel);
        panel->setProperty("devicePixelRatio", view.devicePixelRatio());
        auto *layout = panel->findChild<MasonryLayout *>(
            QStringLiteral("galleryViewportItem"));
        QVERIFY(layout);
        for (int cycle = 0; cycle < 3; ++cycle) {
            QTRY_COMPARE(session->currentIndex(), 0);
            QTRY_COMPARE(layout->contentY(), qreal(0));
            QVERIFY(apply(parent, QStringLiteral("/"), 50));
            QTest::qWait(250);
            QCOMPARE(session->currentIndex(), 50);
            QVERIFY(layout->contentY() > 0);
            QVERIFY(apply(preview, QStringLiteral("/child"), 0));
            QTest::qWait(50);
            // f4 brackets the short-to-full catalog handoff in a renderer
            // transaction. Without it, the viewport timer can conceal the
            // accumulating padding error by restoring the old offset later.
            QVERIFY(QMetaObject::invokeMethod(
                panel, "beginPresentationStateUpdate", Q_ARG(QVariant, false)));
            QVERIFY(apply(child, QStringLiteral("/child"), 0));
            QVERIFY(QMetaObject::invokeMethod(
                panel, "endPresentationStateUpdate", Q_ARG(QVariant, false)));
            QTest::qWait(250);
            QCOMPARE(session->currentIndex(), 0);
            QCOMPARE(layout->currentIndex(), 0);
            QCOMPARE(panel->property("visualCursorIndex").toInt(), 0);
            QCOMPARE(layout->contentY(), qreal(0));
            // Once delegates are visible, metadata/geometry refreshes use
            // the cursor anchor rather than the leading-row anchor.
            QVERIFY(QMetaObject::invokeMethod(
                panel, "beginPresentationStateUpdate", Q_ARG(QVariant, false)));
            layout->setSpacing(layout->spacing() + 1);
            QVERIFY(QMetaObject::invokeMethod(
                panel, "endPresentationStateUpdate", Q_ARG(QVariant, false)));
            QCOMPARE(layout->contentY(), qreal(0));
        }
    }

    void masonryPageNavigationSurvivesCachedMetadataAfterReentry() {
        QQuickView view;
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        options.maxDecodeThreads = 2;
        auto *runtime = ZoinGallery::GalleryRuntime::install(view.engine(), options);
        auto *session = runtime->createExternalSession("first-page-after-entry");
        const auto parent = prefixedCatalog("page-parent", 80);
        auto *decoder = runtime->findChild<DecodeManager *>();
        decoder->setImageCacheMode(CacheUsageMode::On);
        QVariantList child{QVariantMap{{"entryId", "page-up"}, {"index", 0},
            {"name", ".."}, {"isDir", true}, {"isImage", false}}};
        for (int row = 1; row < 356; ++row) {
            const QString key = QStringLiteral("page-child-%1").arg(row);
            ImageInfo info;
            info.source = {.resourceId = key, .sourceKey = key, .contentVersion = "v1",
                .versionStrength = "strong", .displayName = "image.jpg", .size = 4096 + row};
            info.sourceVersionToken = "v1";
            info.imageSize = row % 3 ? QSize(3000, 2000) : QSize(2000, 3000);
            PersistentDerivedImageCache::storeMetadata(info);
            child.append(QVariantMap{{"entryId", key}, {"index", row}, {"name", "image.jpg"},
                {"isImage", true}, {"resourceId", key}, {"sourceKey", key}, {"contentVersion", "v1"},
                {"versionStrength", "strong"}, {"size", 4096 + row}, {"sizeKnown", true}});
        }
        qulonglong revision = 0;
        const auto apply = [&](const QVariantList &rows, const QString &path, int cursor) {
            return session->applyExternalCatalog(rows, ++revision, {
                {"currentPath", path}, {"cursorIndex", cursor},
                {"cursorEntryId", rows.at(cursor).toMap().value("entryId")}
            });
        };
        QVERIFY(apply(child, "/child", 0));
        auto *panel = createPanel(view, session, "firstPageSession");
        QVERIFY(panel);
        auto *panelItem = qobject_cast<QQuickItem *>(panel);
        auto *layout = panel->findChild<MasonryLayout *>("galleryViewportItem");
        auto *animation = panel->findChild<QObject *>("galleryPanelScrollAnimation");
        QVERIFY(panelItem && layout && animation);
        panelItem->setSize(QSizeF(1000, 900));
        panel->setProperty("devicePixelRatio", view.devicePixelRatio());
        layout->setDensity(160);
        QTest::qWait(400);
        panelItem->forceActiveFocus();
        view.requestActivate();
        const auto pageKey = [&](Qt::Key key) {
            QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
            QCoreApplication::sendEvent(&view, &press);
            QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
            QCoreApplication::sendEvent(&view, &release);
        };
        for (int cycle = 0; cycle < 4; ++cycle) {
            QVERIFY(apply(parent, "/", 70));
            QTest::qWait(250);
            QVERIFY(layout->contentY() > 0);
            QVERIFY(QMetaObject::invokeMethod(panel, "beginPresentationStateUpdate", Q_ARG(QVariant, false)));
            QVariantMap state{{"currentPath", "/child"}, {"cursorIndex", 0},
                {"cursorEntryId", "page-up"}, {"metadataDeferred", true},
                {"catalogRowsDeferred", true}, {"totalCount", child.size()}};
            QVERIFY(session->applyExternalCatalog(child.mid(0, 48), ++revision, state));
            QVERIFY(QMetaObject::invokeMethod(panel, "endPresentationStateUpdate", Q_ARG(QVariant, false)));
            QTest::qWait(40);
            QVERIFY(QMetaObject::invokeMethod(panel, "beginPresentationStateUpdate", Q_ARG(QVariant, false)));
            state["catalogRowsDeferred"] = false;
            state["catalogDelta"] = QVariantMap{{"baseCatalogRevision", revision},
                {"oldTotalCount", child.size()}, {"ranges", QVariantList{QVariantMap{
                    {"oldIndex", 0}, {"index", 0}, {"count", child.size()}}}}};
            QVERIFY(session->applyExternalCatalog(child, ++revision, state));
            QVERIFY(QMetaObject::invokeMethod(panel, "endPresentationStateUpdate", Q_ARG(QVariant, false)));
            QTest::qWait(cycle % 2 ? 250 : 40);
            QCOMPARE(session->currentIndex(), 0);
            QCOMPARE(layout->contentY(), qreal(0));
            QTimer::singleShot(30, session, [session]() {
                // A newly visible row can publish the same cached dimensions
                // while PageDown is animating. Its geometry stays unchanged.
                auto *model = qobject_cast<ZoinGallery::ExternalCatalogModel *>(session->model());
                emit model->dataChanged(model->index(0), model->index(355),
                    {FileListModel::IsImageRole, FileListModel::FolderRole,
                     FileListModel::ImageFullSizeRole});
            });
            pageKey(Qt::Key_PageDown);
            const qreal destination = animation->property("to").toReal();
            QVERIFY2(destination >= layout->height() * .65,
                     qPrintable(QStringLiteral("PageDown moved only %1 of %2 pixels")
                         .arg(destination).arg(layout->height())));
            QTest::qWait(300);
            QVERIFY2(qAbs(layout->contentY() - destination) < 1,
                qPrintable(QStringLiteral("PageDown stopped at %1 instead of %2 after cached metadata")
                    .arg(layout->contentY()).arg(destination)));
            QVERIFY(layout->indexGeometry(session->currentIndex()).top() >= destination - 1);
            pageKey(Qt::Key_PageUp);
            QTest::qWait(300);
            QCOMPARE(layout->contentY(), qreal(0));
            pageKey(Qt::Key_Home);
            QTest::qWait(250);
        }
        runtime->shutdown();
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
            QStringLiteral("galleryViewportItem"));
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
        QVERIFY2(stagedVisualNs < 33'000'000,
                 qPrintable(QStringLiteral(
                     "snapshot-to-painted catalog frame took %1 ms")
                     .arg(stagedVisualNs / 1'000'000.0, 0, 'f', 3)));
        // Do not include the pointer/action assertions above in a frame
        // budget: they are test-harness work deliberately performed between
        // applyExternalCatalog() and view.update(). The synchronous reset and
        // snapshot-to-swap clocks isolate the two production boundaries;
        // the live navigation benchmark covers their end-to-end composition.

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
                << "snapshot-to-painted-frame ms"
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

        // A DecodeManager batch publishes one broad known-size range and pays
        // for only one full Masonry rewrap. Row 0 and row 4 deliberately
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
            info.path = QFileInfo(entry.value(
                QStringLiteral("localPath")).toString()).absoluteFilePath();
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
        QCOMPARE(knownSizeNotifications, 1);
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

        // Reordering known, unequal aspect ratios retains the painted item.
        // Its geometry must equal the new hit-test geometry immediately; a
        // model reset is not a 500 ms cross-catalog layout animation.
        const QRectF geometryBeforeReorder = firstSlot->geometry();
        const QVariant identityBeforeReorder = firstSlot->visualRow().value("entryId");
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
        const QRectF expectedResetGeometry(layout->indexGeometry(4).toRect());
        QCOMPARE(firstSlot->visualRow().value("entryId"), identityBeforeReorder);
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

    void reorderKeepsCursorViewportOffset_data() {
        QTest::addColumn<QString>("presentation");
        QTest::addColumn<bool>("delta");
        for (const char *mode : {"details", "icons", "grid", "masonry", "columns"}) {
            QTest::newRow(mode) << QString::fromLatin1(mode) << false;
            QTest::newRow(qPrintable(QString::fromLatin1(mode)+"-delta")) << QString::fromLatin1(mode) << true;
        }
    }

    void reorderKeepsCursorViewportOffset() {
        QFETCH(QString, presentation);
        QFETCH(bool, delta);
        QQuickView view;
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        auto *runtime = ZoinGallery::GalleryRuntime::install(view.engine(), options);
        auto *session = runtime->createExternalSession(QStringLiteral("reorder-anchor"));
        auto catalog = prefixedCatalog(QStringLiteral("anchor"), 200);
        QVariantMap catalogOptions{{"currentPath", "D:/synthetic/anchor"},
                                   {"catalogRowsDeferred", delta}, {"totalCount", 200}};
        QVERIFY(session->applyExternalCatalog(catalog, 1, catalogOptions));
        QVERIFY(session->applyExternalState(QStringLiteral("anchor-entry-80"), 80, {}, 1));
        auto *panel = createPanel(view, session, QStringLiteral("anchorSession"), presentation);
        QVERIFY(panel);
        panel->setProperty("devicePixelRatio", 1.75);
        auto *layout = panel->findChild<MasonryLayout *>(QStringLiteral("galleryViewportItem"));
        QVERIFY(layout);
        QTRY_COMPARE(layout->count(), 200);
        QTRY_COMPARE(layout->currentIndex(), 80);
        QTest::qWait(150);
        const bool horizontal = presentation == QStringLiteral("columns");
        const auto position = [layout, horizontal](int index) {
            const auto rect = layout->indexGeometry(index);
            return (horizontal ? rect.x() : rect.y()) - layout->contentY();
        };
        const auto rect = layout->indexGeometry(80);
        layout->setContentY((horizontal ? rect.x() : rect.y()) - 80);
        QTest::qWait(100);
        const qreal offset = position(80);
        QPersistentModelIndex persistent(session->model()->index(80, 0));
        QSignalSpy layoutChanges(session->model(), &QAbstractItemModel::layoutChanged);
        QSignalSpy rowMoves(session->model(), &QAbstractItemModel::rowsMoved);
        std::reverse(catalog.begin(), catalog.end());
        if (delta) {
            QVariantList ranges;
            for (int index=0; index<200; ++index) {
                auto entry = catalog[index].toMap();
                entry["index"] = index;
                catalog[index] = entry;
                ranges.append(QVariantMap{{"oldIndex", 199-index}, {"index", index}, {"count", 1}});
            }
            catalogOptions["catalogDelta"] = QVariantMap{{"baseCatalogRevision", 1},
                {"oldTotalCount", 200}, {"ranges", ranges}};
        }
        QVERIFY(QMetaObject::invokeMethod(panel, "beginPresentationStateUpdate",
                                          Q_ARG(QVariant, false)));
        QVERIFY(session->applyExternalCatalog(catalog, 2, catalogOptions));
        QVERIFY(session->applyExternalState(QStringLiteral("anchor-entry-80"), 119, {}, 2));
        QVERIFY(QMetaObject::invokeMethod(panel, "endPresentationStateUpdate",
                                          Q_ARG(QVariant, false)));
        qInfo() << "[FIX:sort-anchor] immediate" << layout->currentIndex() << position(119);
        QVERIFY(qAbs(position(119)-offset) < 0.6);
        QCOMPARE(persistent.row(), 119);
        QCOMPARE(layoutChanges.count(), 1);
        QCOMPARE(rowMoves.count(), 0);
        QTRY_COMPARE(layout->currentIndex(), 119);
        QTest::qWait(200);
        qInfo() << "[FIX:sort-anchor]" << presentation << offset << position(119);
        QVERIFY(qAbs(position(119)-offset) < 0.6);
        runtime->shutdown();
    }

    void sparsePageReplacementRebindsVisibleFacadeBeforeReset_data() {
        QTest::addColumn<QString>("presentation");
        QTest::newRow("details") << QStringLiteral("details");
        QTest::newRow("icons") << QStringLiteral("icons");
        QTest::newRow("masonry") << QStringLiteral("masonry");
    }

    void sparsePageReplacementRebindsVisibleFacadeBeforeReset() {
        QFETCH(QString, presentation);
        QQuickView view;
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        options.maxDecodeThreads = 2;
        auto *runtime = ZoinGallery::GalleryRuntime::install(
            view.engine(), options);
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(
            QStringLiteral("sparse-page-facade-lifetime"));
        QVERIFY(session);

        constexpr int logicalCount = 29'291;
        const QVariantList firstPage = prefixedCatalog(
            QStringLiteral("sparse-a"), 128);
        const QVariantList preview = firstPage.mid(0, 64);
        const QVariantMap sparseOptions{
            {QStringLiteral("currentPath"),
             QStringLiteral("D:/synthetic/sparse-a")},
            {QStringLiteral("metadataDeferred"), true},
            {QStringLiteral("catalogRowsDeferred"), true},
            {QStringLiteral("totalCount"), logicalCount},
            {QStringLiteral("cursorIndex"), 0},
            {QStringLiteral("cursorEntryId"),
             QStringLiteral("sparse-a-entry-0")},
        };
        QVERIFY(session->applyExternalCatalog(preview, 1, sparseOptions));

        QObject *panel = createPanel(
            view, session, QStringLiteral("sparseFacadeLifetimeSession"),
            presentation);
        QVERIFY(panel);
        auto *layout = panel->findChild<MasonryLayout *>(
            QStringLiteral("galleryViewportItem"));
        auto *model = qobject_cast<ZoinGallery::ExternalCatalogModel *>(
            session->model());
        QVERIFY(layout && model);
        QTRY_COMPARE_WITH_TIMEOUT(layout->count(), logicalCount, 3000);

        QQuickItem *surface = nullptr;
        QPointer<BrickItem> slot;
        QTRY_VERIFY_WITH_TIMEOUT(
            (surface = panel->findChild<QQuickItem *>(
                 QStringLiteral("gallerySelectionSurface-0")))
            && (slot = qobject_cast<BrickItem *>(surface->parentItem()))
            && slot->property("model").value<ImageFile *>(), 3000);
        QPointer<ImageFile> previewFacade =
            slot->property("model").value<ImageFile *>();
        QVERIFY(previewFacade);

        // Rows 0..63 retain their stable ids but receive authoritative page
        // objects. The old facade must survive this synchronous notification
        // and the visible slot must already point at the replacement when the
        // call returns.
        QVERIFY(session->applyExternalCatalogRows(firstPage, 1));
        QVERIFY(previewFacade);
        ImageFile *pageFacade = slot->property("model").value<ImageFile *>();
        QVERIFY(pageFacade);
        QVERIFY(pageFacade != previewFacade.data());
        QVERIFY(slot->visualFacadeReady());
        QCOMPARE(slot->visualRow()
                     .value(QStringLiteral("entryId")).toString(),
                 QStringLiteral("sparse-a-entry-0"));

        // Reproduce the production sequence without yielding to deleteLater:
        // an immediate directory reset used to dereference the retired page
        // facade while preserving the current item position.
        const QVariantList nextPreview = prefixedCatalog(
            QStringLiteral("sparse-b"), 64);
        QVariantMap nextOptions = sparseOptions;
        nextOptions[QStringLiteral("currentPath")] =
            QStringLiteral("D:/synthetic/sparse-b");
        nextOptions[QStringLiteral("cursorEntryId")] =
            QStringLiteral("sparse-b-entry-0");
        QVERIFY(session->applyExternalCatalog(
            nextPreview, 2, nextOptions));
        QCOMPARE(layout->count(), logicalCount);
        QCOMPARE(slot->visualRow()
                     .value(QStringLiteral("entryId")).toString(),
                 QStringLiteral("sparse-b-entry-0"));

        // Both obsolete generations are reclaimed after their synchronous
        // hand-off.  The live facade count remains viewport-sized rather than
        // scaling with the 29k-row logical catalog.
        QPointer<ImageFile> authoritativeFacade = pageFacade;
        QTRY_VERIFY_WITH_TIMEOUT(previewFacade.isNull(), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(authoritativeFacade.isNull(), 3000);
        // Revisit retained rows after the retired facades have actually died.
        // A raw cached pointer could be passed back into QML on this scroll.
        layout->setContentY(1);
        layout->setContentY(0);
        QTRY_VERIFY_WITH_TIMEOUT(slot->property("model").value<ImageFile *>(), 3000);
        QCOMPARE(slot->property("model").value<ImageFile *>(),
                 model->index(0, 0).data(FileListModel::ImageFileRole).value<ImageFile *>());
        const int liveFacades = model->findChildren<ImageFile *>(
            QString(), Qt::FindDirectChildrenOnly).size();
        QVERIFY2(liveFacades < 256,
                 qPrintable(QStringLiteral(
                     "sparse catalog materialized %1 ImageFile facades")
                     .arg(liveFacades)));
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
        QVariantList largeCatalog = catalogWithIcon(
            QStringLiteral("live-large"), 447,
            QStringLiteral("qrc:/ZoinGallery/resources/FileIcon.svg"));
        for (int row = 0; row < largeCatalog.size(); ++row) {
            QVariantMap entry = largeCatalog.at(row).toMap();
            entry.insert(
                QStringLiteral("name"),
                QStringLiteral(
                    "amd64_microsoft-windows-component-with-a-long-"
                    "winsxs-identity_31bf3856ad364e35_10.0.26100.%1_"
                    "none_48fdcfd155028bbe-%2.txt")
                    .arg(row % 1000)
                    .arg(row));
            largeCatalog[row] = entry;
        }
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
            QStringLiteral("galleryViewportItem"));
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
        // synchronously ready within one 60-Hz frame for the next scene-graph
        // sync. Canonical live profiling measures the actual window's
        // apply-to-swap latency.
        QVERIFY2(synchronousResetNs < 16'000'000,
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
                     largeCatalog.at(row).toMap()
                         .value(QStringLiteral("name")).toString());
        }
        qInfo() << "Masonry live-size typed snapshot delegates"
                << visibleRows.size()
                << "sync ms" << synchronousResetNs / 1'000'000.0
                << "apply-to-frame ms" << applyToFrameNs / 1'000'000.0;
        runtime->shutdown();
    }

    void sparseCatalogFinalizationKeepsViewportBoundedAndAtomic() {
        QQuickView view;
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        auto *runtime = ZoinGallery::GalleryRuntime::install(
            view.engine(), options);
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(
            QStringLiteral("sparse-atomic-finalization"));
        QVERIFY(session);

        const QVariantList finalWindow = prefixedCatalog(
            QStringLiteral("sparse-final"), 48);
        const QVariantMap provisionalOptions{
            {QStringLiteral("currentPath"),
             QStringLiteral("C:/Windows/WinSxS")},
            {QStringLiteral("metadataDeferred"), true},
            {QStringLiteral("catalogRowsDeferred"), true},
            {QStringLiteral("catalogProvisional"), true},
            {QStringLiteral("totalCount"), 43},
            {QStringLiteral("cursorIndex"), 0},
            {QStringLiteral("cursorEntryId"),
             QStringLiteral("sparse-final-entry-0")},
        };
        QVERIFY(session->applyExternalCatalog(
            finalWindow.mid(0, 40), 1, provisionalOptions));

        QObject *panel = createPanel(
            view, session, QStringLiteral("sparseFinalSession"),
            QStringLiteral("details"));
        QVERIFY(panel);
        auto *layout = panel->findChild<MasonryLayout *>(
            QStringLiteral("galleryViewportItem"));
        auto *model = qobject_cast<ZoinGallery::ExternalCatalogModel *>(
            session->model());
        QVERIFY(layout && model);
        QTRY_COMPARE_WITH_TIMEOUT(layout->count(), 43, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!layout->visibleIndexes().isEmpty(), 3000);

        QSignalSpy resetSpy(model, &QAbstractItemModel::modelReset);
        const quint64 commitsBefore = layout->delegateCommitRevision();
        QVariantMap finalOptions = provisionalOptions;
        finalOptions.remove(QStringLiteral("catalogProvisional"));
        finalOptions[QStringLiteral("totalCount")] = 30'000;

        QElapsedTimer timer;
        timer.start();
        layout->beginLayoutUpdate();
        QVERIFY(session->applyExternalCatalog(
            finalWindow, 2, finalOptions));
        layout->endLayoutUpdate();
        const qint64 elapsedNs = timer.nsecsElapsed();

        QCOMPARE(resetSpy.size(), 0);
        QCOMPARE(layout->count(), 30'000);
        QCOMPARE(model->materializedRows().size(), 48);
        QCOMPARE(layout->delegateCommitRevision(), commitsBefore + 1);
        QVERIFY2(elapsedNs < 33'000'000,
                 qPrintable(QStringLiteral(
                     "sparse 43-to-30k finalization took %1 ms")
                                .arg(elapsedNs / 1'000'000.0, 0, 'f', 3)));
        QVERIFY(layout->visibleIndexes().size() < 96);
        QVERIFY(layout->overscanIndexes().size() < 96);
        QVERIFY(layout->findChildren<BrickItem *>().size() < 128);

        runtime->shutdown();
    }

    void thumbnailPublicationWaitsForMasonryGeometry_data() {
        QTest::addColumn<QString>("mode");
        QTest::addColumn<QByteArray>("delay");
        QTest::addColumn<QSize>("imageSize");
        for (const auto &mode : {"masonry", "grid", "icons", "details", "columns"}) {
            for (const auto &delay : {QByteArray("0"), QByteArray("1000")}) {
                QTest::newRow(qPrintable(QString::fromLatin1(mode) + '-' + delay))
                    << QString::fromLatin1(mode) << delay << QSize(1170, 2532);
            }
        }
        QTest::newRow("masonry-placeholder-size")
            << QStringLiteral("masonry") << QByteArray("0") << QSize(1, 1);
        QTest::newRow("masonry-terminal-metadata-error")
            << QStringLiteral("masonry") << QByteArray("0") << QSize();
    }

    void thumbnailPublicationWaitsForMasonryGeometry() {
        QFETCH(QString, mode);
        QFETCH(QByteArray, delay);
        QFETCH(QSize, imageSize);
        const QByteArray previous = qgetenv("F4_GALLERY_ROW_DELAY_MS");
        const auto restore = qScopeGuard([previous] {
            if (previous.isNull()) qunsetenv("F4_GALLERY_ROW_DELAY_MS");
            else qputenv("F4_GALLERY_ROW_DELAY_MS", previous);
        });
        qputenv("F4_GALLERY_ROW_DELAY_MS", delay);
        QQuickView view;
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        auto *runtime = ZoinGallery::GalleryRuntime::install(view.engine(), options);
        auto *session = runtime->createExternalSession(QStringLiteral("publication"));
        QVariantList catalog = prefixedCatalog(QStringLiteral("publication"), 24);
        for (auto &value : catalog) {
            auto entry = value.toMap();
            entry[QStringLiteral("isImage")] = true;
            value = entry;
        }
        QVERIFY(session->applyExternalCatalog(catalog, 1, {
            {QStringLiteral("metadataDeferred"), true}}));
        auto *panel = createPanel(view, session, QStringLiteral("publicationSession"), mode);
        QVERIFY(panel);
        auto *layout = panel->findChild<MasonryLayout *>(QStringLiteral("galleryViewportItem"));
        QVERIFY(layout);
        QTRY_VERIFY(!layout->visibleIndexes().isEmpty());
        auto *file = session->model()->index(0, 0)
            .data(FileListModel::ImageFileRole).value<ImageFile *>();
        QVERIFY(file);
        // Simulate a retained/cached thumbnail arriving independently of metadata.
        file->setImageId(QStringLiteral("publication-fixture"));
        session->model()->dataChanged(session->model()->index(0, 0),
            session->model()->index(0, 0), {});
        auto thumbnail = [panel]() {
            return panel->findChild<QQuickItem *>(QStringLiteral("galleryThumbnail-0"));
        };
        QTRY_VERIFY(thumbnail());
        QTest::qWait(50);
        const QRectF before = layout->indexGeometry(0);
        if (mode == QStringLiteral("masonry"))
            QVERIFY2(thumbnail()->property("source").toUrl().isEmpty(),
                     "Thumbnail leaked before its masonry row geometry was committed");
        else
            QCOMPARE(thumbnail()->property("source").toUrl().toString(), file->imageIdUrl());

        if (mode == QStringLiteral("masonry")) {
            // The same cached URL must open immediately in fixed modes and
            // close again on return to a still-unresolved masonry row.
            for (const auto &fixed : {"grid", "icons", "details", "columns"}) {
                panel->setProperty("presentationMode", QString::fromLatin1(fixed));
                QTRY_VERIFY(thumbnail());
                QTRY_COMPARE(thumbnail()->property("source").toUrl().toString(), file->imageIdUrl());
            }
            panel->setProperty("presentationMode", mode);
            QTRY_VERIFY(thumbnail());
            QVERIFY(thumbnail()->property("source").toUrl().isEmpty());
        }

        QList<ImageInfo> infos;
        for (const auto &value : catalog) {
            ImageInfo info;
            info.path = QFileInfo(value.toMap().value(QStringLiteral("localPath")).toString()).absoluteFilePath();
            info.requestNamespace = session->sessionId();
            info.imageSize = imageSize;
            info.isCached = delay != "0";
            infos.append(info);
        }
        auto *decoder = runtime->findChild<DecodeManager *>();
        QVERIFY(decoder);
        decoder->imagesInfoReady(infos);
        if (mode == QStringLiteral("masonry")) {
            if (delay != "0") {
                QTest::qWait(200);
                QCOMPARE(layout->indexGeometry(0), before);
                QVERIFY(thumbnail()->property("source").toUrl().isEmpty());
            }
            if (imageSize.isValid() && imageSize.width() != imageSize.height())
                QTRY_VERIFY_WITH_TIMEOUT(layout->indexGeometry(0) != before, 2500);
        }
        QTRY_COMPARE(thumbnail()->property("source").toUrl().toString(), file->imageIdUrl());
        if (mode == QStringLiteral("masonry") && delay != "0") {
            QVERIFY(session->applyExternalCatalog(catalog, 2, {
                {QStringLiteral("metadataDeferred"), true},
                {QStringLiteral("currentPath"), QStringLiteral("/reentry")}}));
            QTRY_VERIFY(thumbnail());
            QVERIFY(thumbnail()->property("source").toUrl().isEmpty());
            QTRY_VERIFY_WITH_TIMEOUT(!thumbnail()->property("source").toUrl().isEmpty(), 2500);
        }
        runtime->shutdown();
    }

    void diagnosticMasonryPacesRowsAndReentry() {
        const QByteArray previous = qgetenv("F4_GALLERY_ROW_DELAY_MS");
        const auto restore = qScopeGuard([previous] {
            if (previous.isNull()) qunsetenv("F4_GALLERY_ROW_DELAY_MS");
            else qputenv("F4_GALLERY_ROW_DELAY_MS", previous);
        });
        qputenv("F4_GALLERY_ROW_DELAY_MS", "1000");
        QQuickView view;
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        auto *runtime = ZoinGallery::GalleryRuntime::install(view.engine(), options);
        auto *session = runtime->createExternalSession(QStringLiteral("slow-rows"));
        QVariantList catalog = prefixedCatalog(QStringLiteral("slow-rows"), 24);
        for (int row = 0; row < catalog.size(); ++row) {
            auto entry = catalog[row].toMap();
            entry[QStringLiteral("isImage")] = true;
            catalog[row] = entry;
        }
        const QVariantMap state{{QStringLiteral("metadataDeferred"), true}};
        QVERIFY(session->applyExternalCatalog(catalog, 1, state));
        auto *panel = createPanel(view, session, QStringLiteral("slowRowsSession"),
                                  QStringLiteral("masonry"));
        QVERIFY(panel);
        auto *layout = panel->findChild<MasonryLayout *>(QStringLiteral("galleryViewportItem"));
        auto *decoder = runtime->findChild<DecodeManager *>();
        QVERIFY(layout && decoder);
        QTRY_VERIFY(!layout->visibleIndexes().isEmpty());
        const QRectF before = layout->indexGeometry(0);
        QList<ImageInfo> infos;
        for (int row = 0; row < catalog.size(); ++row) {
            ImageInfo info;
            info.path = QFileInfo(catalog[row].toMap().value(QStringLiteral("localPath")).toString()).absoluteFilePath();
            info.requestNamespace = session->sessionId();
            info.imageSize = QSize(1170, 2532);
            info.isCached = true;
            infos.append(info);
        }
        decoder->imagesInfoReady(infos);
        QTest::qWait(300);
        QCOMPARE(layout->indexGeometry(0), before);
        QTRY_VERIFY_WITH_TIMEOUT(layout->indexGeometry(0) != before, 2000);
        const auto isPortrait = [layout](int row) {
            const auto rect = layout->indexGeometry(row);
            return rect.height() > 1.5 * rect.width();
        };
        QVERIFY(!isPortrait(23));
        QTest::qWait(300);
        QVERIFY(!isPortrait(23));
        QTRY_VERIFY_WITH_TIMEOUT(isPortrait(23), 6000);
        // Retained metadata must be replayable without clearing a user's cache.
        QVERIFY(session->applyExternalCatalog(catalog, 2, {
            {QStringLiteral("metadataDeferred"), true},
            {QStringLiteral("currentPath"), QStringLiteral("/reentry")},
        }));
        QVERIFY(!isPortrait(23));
        QTRY_VERIFY_WITH_TIMEOUT(isPortrait(23), 6000);
        runtime->shutdown();
    }

    void masonryMetadataCommitsCompleteRows() {
        QQuickView view;
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        auto *runtime = ZoinGallery::GalleryRuntime::install(view.engine(), options);
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(QStringLiteral("row-metadata"));
        QVariantList catalog = prefixedCatalog(QStringLiteral("row-metadata"), 13);
        for (int row = 0; row < catalog.size(); ++row) {
            QVariantMap entry = catalog[row].toMap();
            entry[QStringLiteral("isImage")] = true;
            catalog[row] = entry;
        }
        QVERIFY(session->applyExternalCatalog(catalog, 1, {
            {QStringLiteral("currentPath"), QStringLiteral("/DCIM/100APPLE")},
            {QStringLiteral("metadataDeferred"), true},
        }));
        QObject *panel = createPanel(view, session, QStringLiteral("rowMetadataSession"),
                                     QStringLiteral("masonry"));
        QVERIFY(panel);
        panel->setProperty("devicePixelRatio", view.devicePixelRatio());
        auto *layout = panel->findChild<MasonryLayout *>(QStringLiteral("galleryViewportItem"));
        auto *decoder = runtime->findChild<DecodeManager *>();
        QVERIFY(layout && decoder);
        QTRY_VERIFY(!layout->visibleIndexes().isEmpty());
        QTest::qWait(50);
        QList<QRectF> before;
        for (int row = 0; row < catalog.size(); ++row)
            before.append(layout->indexGeometry(row));
        int firstRowEnd = 1;
        while (firstRowEnd < before.size()
               && qAbs(before[firstRowEnd].bottom() - before[0].bottom()) < 1)
            ++firstRowEnd;
        QVERIFY(firstRowEnd > 1 && firstRowEnd < catalog.size());
        const auto deliver = [&](int row, bool failed = false, bool cached = false) {
            ImageInfo info;
            info.path = QFileInfo(catalog[row].toMap().value(QStringLiteral("localPath")).toString()).absoluteFilePath();
            info.sourceVersionToken = 0;
            info.fileSize = -1;
            info.requestNamespace = session->sessionId();
            info.imageSize = failed ? QSize() : QSize(1170, 2532);
            info.orientation = ExifOrientation::Horizontal;
            info.isCached = cached;
            info.isLast = true; // Submission order is not a completion barrier.
            decoder->imageInfoReady(info);
        };
        deliver(0);
        QTest::qWait(50);
        for (int row = 0; row < catalog.size(); ++row)
            QCOMPARE(layout->indexGeometry(row), before[row]);
        for (int row = firstRowEnd - 1; row > 0; --row)
            deliver(row);
        QTest::qWait(50);
        // A row of square placeholders is NOT a complete portrait row:
        // narrower real images will pull more entries into the same row.
        QCOMPARE(layout->indexGeometry(0), before[0]);
        const int finalRowStart = firstRowEnd + 1;
        for (int row = catalog.size() - 1; row >= finalRowStart; --row)
            deliver(row);
        QTRY_VERIFY(layout->indexGeometry(catalog.size() - 1).height()
                    > layout->indexGeometry(catalog.size() - 1).width());
        QCOMPARE(layout->indexGeometry(0), before[0]);
        // Resolve all remaining rows in reverse order, including an unreadable
        // image and the incomplete final visual row. Neither may block forever.
        for (int row = finalRowStart - 1; row >= firstRowEnd; --row)
            deliver(row, row == firstRowEnd);
        QTRY_VERIFY(layout->indexGeometry(0) != before[0]);
        QTRY_VERIFY(layout->indexGeometry(catalog.size() - 1).height()
                    > layout->indexGeometry(catalog.size() - 1).width());
        QTest::qWait(50);
        const qreal dpr = view.devicePixelRatio();
        int checkedLeaves = 0;
        for (const QVariant &value : layout->visibleIndexes()) {
            for (const QString &prefix : {QStringLiteral("galleryMasonryLabel-"),
                                          QStringLiteral("galleryFallbackIcon-")}) {
                auto *leaf = panel->findChild<QQuickItem *>(prefix + value.toString());
                if (!leaf || !leaf->isVisible())
                    continue;
                ++checkedLeaves;
                const QPointF origin = leaf->mapToItem(view.contentItem(), QPointF());
                QVERIFY2(qAbs(origin.x() * dpr - qRound(origin.x() * dpr)) < 0.001,
                         qPrintable(leaf->objectName()));
                QVERIFY2(qAbs(origin.y() * dpr - qRound(origin.y() * dpr)) < 0.001,
                         qPrintable(QStringLiteral("%1 physical y=%2")
                             .arg(leaf->objectName()).arg(origin.y() * dpr, 0, 'f', 6)));
                QCOMPARE(leaf->mapToItem(view.contentItem(), QPointF(1, 0)) - origin,
                         QPointF(1, 0));
                QCOMPARE(leaf->mapToItem(view.contentItem(), QPointF(0, 1)) - origin,
                         QPointF(0, 1));
            }
        }
        QVERIFY(checkedLeaves > 0);
        const QString capture = qEnvironmentVariable("F4_ROW_METADATA_CAPTURE");
        if (!capture.isEmpty())
            QVERIFY(view.grabWindow().save(capture));
        runtime->shutdown();
    }

    void densePromotionWithDeltaRestoresNaturalMasonrySizes() {
        QQuickView view;
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        auto *runtime = ZoinGallery::GalleryRuntime::install(view.engine(), options);
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(QStringLiteral("dense-promotion"));
        const QVariantList catalog = prefixedCatalog(QStringLiteral("promotion"), 136);
        QVariantMap state{
            {QStringLiteral("currentPath"), QStringLiteral("/DCIM/100APPLE")},
            {QStringLiteral("metadataDeferred"), true},
            {QStringLiteral("catalogRowsDeferred"), true},
            {QStringLiteral("totalCount"), catalog.size()},
        };
        QVERIFY(session->applyExternalCatalog(catalog.mid(0, 48), 1, state));
        QObject *panel = createPanel(view, session, QStringLiteral("promotionSession"),
                                     QStringLiteral("masonry"));
        QVERIFY(panel);
        auto *layout = panel->findChild<MasonryLayout *>(QStringLiteral("galleryViewportItem"));
        auto *model = qobject_cast<ZoinGallery::ExternalCatalogModel *>(session->model());
        QVERIFY(layout && model);
        QVERIFY(model->sparseCatalog());
        state[QStringLiteral("catalogRowsDeferred")] = false;
        state[QStringLiteral("catalogDelta")] = QVariantMap{
            {QStringLiteral("baseCatalogRevision"), 1},
            {QStringLiteral("oldTotalCount"), catalog.size()},
            {QStringLiteral("ranges"), QVariantList{QVariantMap{
                {QStringLiteral("oldIndex"), 0}, {QStringLiteral("index"), 0},
                {QStringLiteral("count"), catalog.size()},
            }}},
        };
        QVERIFY(session->applyExternalCatalog(catalog, 2, state));
        QVERIFY2(!model->sparseCatalog(), "completed catalog must stop using uniform sparse geometry");
        auto *decoder = runtime->findChild<DecodeManager *>();
        QVERIFY(decoder);
        QList<ImageInfo> metadata;
        for (int row : {0, 4}) {
            ImageInfo info;
            info.path = QFileInfo(catalog[row].toMap().value(QStringLiteral("localPath")).toString()).absoluteFilePath();
            info.sourceVersionToken = 0;
            info.fileSize = -1;
            info.requestNamespace = session->sessionId();
            info.imageSize = row == 0 ? QSize(1170, 2532) : QSize(2532, 1170);
            info.orientation = ExifOrientation::Horizontal;
            info.isCached = true;
            metadata.append(info);
        }
        decoder->imagesInfoReady(metadata);
        QTRY_VERIFY(layout->indexGeometry(0).height() > layout->indexGeometry(0).width());
        QTRY_VERIFY(layout->indexGeometry(4).width() > layout->indexGeometry(4).height());
        runtime->shutdown();
    }

    void sparseCatalogRendersMasonryGridAndIconsWithoutFullScene() {
        QQuickView view;
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        options.maxDecodeThreads = 2;
        auto *runtime = ZoinGallery::GalleryRuntime::install(
            view.engine(), options);
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(
            QStringLiteral("sparse-presentation-modes"));
        QVERIFY(session);

        constexpr int logicalCount = 30'000;
        const QVariantList preview = prefixedCatalog(
            QStringLiteral("sparse-modes"), 64);
        const QVariantMap sparseOptions{
            {QStringLiteral("currentPath"),
             QStringLiteral("C:/Windows/WinSxS")},
            {QStringLiteral("metadataDeferred"), true},
            {QStringLiteral("catalogRowsDeferred"), true},
            {QStringLiteral("totalCount"), logicalCount},
            {QStringLiteral("cursorIndex"), 0},
            {QStringLiteral("cursorEntryId"),
             QStringLiteral("sparse-modes-entry-0")},
        };
        QVERIFY(session->applyExternalCatalog(preview, 1, sparseOptions));

        QObject *panel = createPanel(
            view, session, QStringLiteral("sparseModesSession"),
            QStringLiteral("masonry"));
        QVERIFY(panel);
        auto *layout = panel->findChild<MasonryLayout *>(
            QStringLiteral("galleryViewportItem"));
        auto *model = qobject_cast<ZoinGallery::ExternalCatalogModel *>(
            session->model());
        QVERIFY(layout && model);
        QTRY_COMPARE_WITH_TIMEOUT(layout->count(), logicalCount, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!layout->visibleIndexes().isEmpty(), 3000);
        const QVariantList groups{
            QVariantMap{{QStringLiteral("key"), QStringLiteral("first")},
                        {QStringLiteral("title"), QStringLiteral("First")},
                        {QStringLiteral("startIndex"), 1},
                        {QStringLiteral("count"), 14999}},
            QVariantMap{{QStringLiteral("key"), QStringLiteral("second")},
                        {QStringLiteral("title"), QStringLiteral("Second")},
                        {QStringLiteral("startIndex"), 15000},
                        {QStringLiteral("count"), 15000}},
        };
        QVERIFY(panel->setProperty("groupDescriptors", groups));
        QVERIFY(panel->setProperty(
            "groupStateKey", QStringLiteral("sparse-presentation-groups")));
        QTRY_VERIFY_WITH_TIMEOUT(layout->groupForIndex(1) == 0
                                     && !layout->visibleGroupHeaders().isEmpty(),
                                 3000);

        const QList<QPair<QString, MasonryLayout::PresentationMode>> modes{
            {QStringLiteral("masonry"), MasonryLayout::Masonry},
            {QStringLiteral("grid"), MasonryLayout::Grid},
            {QStringLiteral("icons"), MasonryLayout::Icons},
        };
        for (const auto &[name, mode] : modes) {
            panel->setProperty("presentationMode", name);
            QTRY_COMPARE_WITH_TIMEOUT(layout->presentationMode(), mode, 3000);
            QTRY_VERIFY_WITH_TIMEOUT(!layout->visibleIndexes().isEmpty(),
                                     3000);

            const int row = layout->visibleIndexes().constFirst().toInt();
            QVERIFY(row >= 0 && row < preview.size());
            const QRectF geometry = layout->indexGeometry(row);
            QVERIFY(geometry.isValid() && !geometry.isEmpty());

            const QString objectName = mode == MasonryLayout::Masonry
                ? QStringLiteral("galleryMasonryLabel-%1").arg(row)
                : mode == MasonryLayout::Grid
                    ? QStringLiteral("galleryGridLabel-%1").arg(row)
                    : QStringLiteral("galleryIconsLabel-%1").arg(row);
            QQuickItem *label = nullptr;
            QTRY_VERIFY_WITH_TIMEOUT(
                (label = panel->findChild<QQuickItem *>(objectName))
                && label->isVisible()
                && !label->property("text").toString().isEmpty(), 3000);
            QCOMPARE(label->property("text").toString(),
                     session->entryNameAt(row));

            if (mode == MasonryLayout::Masonry) {
                // A retained slot can briefly have neither a valid visual
                // snapshot nor its heavyweight ImageFile facade while a
                // sparse page is handed over. The one-row session lookup is
                // the intended bounded fallback and must keep the label
                // paintable in that interval.
                auto *slot = qobject_cast<BrickItem *>(layout->itemAt(
                    geometry.center().x(), geometry.center().y()));
                QVERIFY(slot);
                slot->setProperty("model", QVariant());
                slot->setVisualFacadeReady(false);
                slot->setVisualRow({
                    {QStringLiteral("valid"), false},
                    {QStringLiteral("sourceIndex"), row},
                });
                QTRY_COMPARE_WITH_TIMEOUT(
                    label->property("text").toString(),
                    session->entryNameAt(row), 3000);
            }
        }

        QVERIFY(layout->setGroupCollapsed(QStringLiteral("first"), true));
        QTRY_VERIFY_WITH_TIMEOUT(layout->indexGeometry(1).isEmpty(), 3000);
        QCOMPARE(layout->nearestVisibleIndex(1, true), 15000);
        QVERIFY(layout->setGroupCollapsed(QStringLiteral("first"), false));

        // The logical catalog is large, but only the page supplied by the
        // host and the bounded active/overscan window may be materialized.
        QVERIFY(model->materializedRows().size() < 256);
        runtime->shutdown();
    }

    void sparseDetailsScrollBarUsesAnalyticExtentAndHomeIsBounded() {
        QQuickView view;
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        auto *runtime = ZoinGallery::GalleryRuntime::install(
            view.engine(), options);
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(
            QStringLiteral("details-scroll-metrics"));
        QVERIFY(session);

        constexpr int logicalCount = 29'291;
        const QVariantList preview = prefixedCatalog(
            QStringLiteral("details-analytic"), 64);
        const QVariantMap sparseOptions{
            {QStringLiteral("currentPath"),
             QStringLiteral("C:/Windows/WinSxS")},
            {QStringLiteral("metadataDeferred"), true},
            {QStringLiteral("catalogRowsDeferred"), true},
            {QStringLiteral("totalCount"), logicalCount},
            {QStringLiteral("cursorIndex"), 0},
            {QStringLiteral("cursorEntryId"),
             QStringLiteral("details-analytic-entry-0")},
        };
        QVERIFY(session->applyExternalCatalog(preview, 1, sparseOptions));

        QObject *panel = createPanel(
            view, session, QStringLiteral("detailsMetricsSession"),
            QStringLiteral("details"));
        QVERIFY(panel);
        panel->setProperty("showDetailsHeader", false);
        panel->setProperty("density", 24.2);
        panel->setProperty("height", 435.2);
        auto *panelItem = qobject_cast<QQuickItem *>(panel);
        auto *layout = panel->findChild<MasonryLayout *>(
            QStringLiteral("galleryViewportItem"));
        auto *externalModel =
            qobject_cast<ZoinGallery::ExternalCatalogModel *>(
                session->model());
        auto *scrollBar = panel->findChild<QQuickItem *>(
            QStringLiteral("galleryPanelScrollBar"));
        QVERIFY(panelItem && layout && externalModel && scrollBar);
        QVERIFY(!panel->findChild<QQuickItem *>(
            QStringLiteral("galleryDetailsScrollMetrics")));
        QTRY_COMPARE_WITH_TIMEOUT(layout->count(), logicalCount, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(layout->needScroll(), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(scrollBar->isVisible(), 3000);

        QTRY_VERIFY_WITH_TIMEOUT(
            layout->contentHeight() > logicalCount * layout->density(),
            3000);
        QTRY_VERIFY_WITH_TIMEOUT(
            qAbs(scrollBar->property("size").toReal()
                 - layout->height() / layout->contentHeight()) < 0.0001,
            3000);

        // A provisional streaming count must not expose a transient thumb.
        auto *handle = scrollBar->findChild<QQuickItem *>(
            QStringLiteral("galleryScrollBarHandle"));
        QVERIFY(handle);
        qInfo() << "large catalog thumb height" << handle->height();
        QVERIFY2(handle->height() >= 16, "Scrollbar thumb disappears in large directories");
        panel->setProperty("scrollBarsReady", false);
        QTRY_VERIFY_WITH_TIMEOUT(!scrollBar->isVisible(), 3000);
        panel->setProperty("scrollBarsReady", true);
        QTRY_VERIFY_WITH_TIMEOUT(scrollBar->isVisible(), 3000);

        const int lastIndex = logicalCount - 1;
        session->setCurrentIndex(lastIndex);
        QVERIFY(invokeEnsureCurrentVisible(panel, false, QVariant(1)));
        const qreal exactEndpoint = qMax<qreal>(
            0, layout->contentHeight() - layout->height());
        QTRY_COMPARE_WITH_TIMEOUT(layout->currentIndex(), lastIndex, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(
            qAbs(layout->contentY() - exactEndpoint) < 0.0001, 3000);

        // This is the production WinSxS regression: an auxiliary ListView
        // used to walk its 29k-row estimator synchronously when currentIndex
        // jumped backwards, blocking the Qt main thread for ~1.2 seconds.
        QElapsedTimer homeTimer;
        homeTimer.start();
        session->setCurrentIndex(0);
        QVERIFY(invokeEnsureCurrentVisible(panel, false, QVariant(-1)));
        const qint64 homeNs = homeTimer.nsecsElapsed();
        QVERIFY2(homeNs < 33'000'000,
                 qPrintable(QStringLiteral(
                     "29k Details End-to-Home jump took %1 ms")
                                .arg(homeNs / 1'000'000.0, 0, 'f', 3)));
        QCOMPARE(layout->currentIndex(), 0);
        QCOMPARE(layout->contentY(), qreal(0));
        QTRY_COMPARE_WITH_TIMEOUT(
            scrollBar->property("position").toReal(), qreal(0), 3000);

        const auto materializedCount = [&]() {
            return externalModel->findChildren<ImageFile *>(
                QString(), Qt::FindDirectChildrenOnly).size();
        };
        constexpr int MaxWarmPresentationFacades = 96;
        QTRY_VERIFY2_WITH_TIMEOUT(
            materializedCount() <= MaxWarmPresentationFacades,
            qPrintable(QStringLiteral(
                "29k Details catalog materialized %1 ImageFile objects")
                .arg(materializedCount())), 3000);
        qInfo() << "29k Details End-to-Home ms"
                << homeNs / 1'000'000.0
                << "materialized" << materializedCount();
        runtime->shutdown();
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
            QStringLiteral("galleryViewportItem"));
        auto *scrollBar = panel->findChild<QQuickItem *>(
            QStringLiteral("galleryPanelScrollBar"));
        QVERIFY(panelItem && layout && scrollBar);
        QTRY_COMPARE_WITH_TIMEOUT(layout->count(), 1, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!scrollBar->isVisible(), 3000);

        const qreal widthWithoutScrollBar = layout->width();
        const qreal leftInset = layout->x();
        const qreal rightInset = panelItem->width()
            - layout->x() - layout->width();
        QCOMPARE(leftInset, 0.0);
        QCOMPARE(rightInset, leftInset);
        QCOMPARE(layout->paddingLeft(), 6.0);
        QCOMPARE(layout->paddingRight(), 6.0);
        QCOMPARE(widthWithoutScrollBar, panelItem->width());

        // The embedded host adds eight pixels of internal panel padding.
        QVERIFY(panel->setProperty("contentHorizontalInset", 8.0));
        QTRY_COMPARE(layout->paddingRight(), 14.0);
        QVERIFY(session->applyExternalCatalog(plainCatalog(200), 2));
        QTRY_COMPARE_WITH_TIMEOUT(layout->count(), 200, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(scrollBar->isVisible(), 3000);
        QCOMPARE(layout->x(), leftInset);
        QCOMPARE(layout->width(), widthWithoutScrollBar);
        QCOMPARE(panelItem->width() - layout->x() - layout->width(),
                 rightInset);
        const qreal tileEdge = panelItem->width() - layout->paddingRight()
            - layout->spacing() / 2.0;
        const qreal gapCenter = (tileEdge + panelItem->width()) / 2;
        QVERIFY(qAbs(scrollBar->x() + scrollBar->width() / 2 - gapCenter)
                <= 0.5 / view.devicePixelRatio());
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QTest::qWait(100);
        const qreal dpr = view.devicePixelRatio();
        for (const auto &name : {"galleryScrollBarHandle", "galleryScrollBarTrack"}) {
            auto *leaf = scrollBar->findChild<QQuickItem *>(QString::fromLatin1(name));
            QVERIFY(leaf);
            const QPointF origin = leaf->mapToScene(QPointF());
            qInfo() << name << "DPR" << dpr << "physical origin" << origin * dpr;
            QVERIFY(qAbs(origin.x() * dpr - qRound(origin.x() * dpr)) < 0.01);
            QVERIFY(qAbs(origin.y() * dpr - qRound(origin.y() * dpr)) < 0.01);
            QCOMPARE(leaf->mapToScene(QPointF(1, 0)) - origin, QPointF(1, 0));
            QCOMPARE(leaf->mapToScene(QPointF(0, 1)) - origin, QPointF(0, 1));
        }
        QVERIFY(!view.grabWindow().isNull());
        if (qEnvironmentVariableIsSet("F4_SCROLLBAR_CAPTURE"))
            QVERIFY(view.grabWindow().save(qEnvironmentVariable("F4_SCROLLBAR_CAPTURE")));
        const QPoint grab = scrollBar->mapToScene(QPointF(scrollBar->width() / 2, 10)).toPoint();
        QTest::mouseMove(&view, grab);
        QTRY_VERIFY(scrollBar->property("hovered").toBool());
        QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, grab);
        QTRY_VERIFY(scrollBar->property("pressed").toBool());
        QTest::mouseMove(&view, grab + QPoint(0, 80));
        QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, grab + QPoint(0, 80));
        QTRY_VERIFY(layout->contentY() > 0);

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
            QStringLiteral("galleryViewportItem"));
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
