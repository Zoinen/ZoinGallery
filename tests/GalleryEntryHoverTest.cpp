#include <ZoinGallery/GalleryRuntime.h>
#include <ZoinGallery/GallerySession.h>

#include <QColor>
#include <QImage>
#include <QPainter>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QSGRendererInterface>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QtTest>

namespace {

QColor composite(const QColor &background, const QColor &overlay)
{
    QImage pixel(1, 1, QImage::Format_ARGB32_Premultiplied);
    pixel.fill(background);
    QPainter painter(&pixel);
    painter.fillRect(pixel.rect(), overlay);
    painter.end();
    return pixel.pixelColor(0, 0);
}

bool sameColor(const QColor &actual, const QColor &expected)
{
    return qAbs(actual.red() - expected.red()) <= 2
        && qAbs(actual.green() - expected.green()) <= 2
        && qAbs(actual.blue() - expected.blue()) <= 2
        && qAbs(actual.alpha() - expected.alpha()) <= 2;
}

QObject *createPanel(QQuickView &view, ZoinGallery::GallerySession *session,
                     const QString &mode, int columns)
{
    view.engine()->addImportPath(QStringLiteral(ZOIN_TEST_QML_IMPORT_PATH));
    view.engine()->rootContext()->setContextProperty(
        QStringLiteral("hoverSession"), session);
    auto *component = new QQmlComponent(view.engine(), &view);
    component->setData(QString(R"QML(
        pragma ComponentBehavior: Bound
        import QtQuick
        import ZoinGallery 1.0
        Rectangle {
            width: 760
            height: 480
            color: "#101820"
            GalleryPanel {
                objectName: "hoverTestPanel"
                anchors.fill: parent
                anchors.margins: 20
                session: hoverSession
                presentationMode: "%1"
                columnCount: %2
                devicePixelRatio: Window.window ? Window.window.devicePixelRatio : 1
                autoFocus: false
                animateLayoutChanges: false
                showCursor: true
                theme: GalleryThemePalette {
                    panelBackground: "#18242c"
                    cursor: "#285d8f"
                    cursorBackground: "#316452"
                    cursorBorder: "#b1bcd1"
                    cardCursorBorder: "#b1bcd1"
                    itemBackground: "#253541"
                    itemHover: "#408accea"
                    selection: "#eac04b"
                    previewBackdrop: "transparent"
                    labelBackground: "transparent"
                }
            }
        }
    )QML").arg(mode).arg(columns).toUtf8(),
                       QUrl(QStringLiteral("inline:EntryHover.qml")));
    if (component->isLoading()) {
        QSignalSpy status(component, &QQmlComponent::statusChanged);
        status.wait(5000);
    }
    if (!component->isReady()) {
        qWarning().noquote() << component->errorString();
        return nullptr;
    }
    QObject *root = component->create();
    if (!root) {
        qWarning().noquote() << component->errorString();
        return nullptr;
    }
    view.setContent(QUrl(QStringLiteral("inline:EntryHover.qml")), component, root);
    view.show();
    return root->findChild<QObject *>(QStringLiteral("hoverTestPanel"));
}

} // namespace

class GalleryEntryHoverTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    }

    void selectionBordersAreConsistent_data()
    {
        QTest::addColumn<QString>("mode");
        QTest::addColumn<int>("columns");
        for (const QString &mode : {QStringLiteral("masonry"),
                 QStringLiteral("columns"), QStringLiteral("details"),
                 QStringLiteral("grid"), QStringLiteral("icons")}) {
            QTest::newRow(qPrintable(mode)) << mode << 2;
        }
        QTest::newRow("three-columns") << QStringLiteral("columns") << 3;
    }

    void selectionBordersAreConsistent()
    {
        QFETCH(QString, mode);
        QFETCH(int, columns);
        QQuickView view;
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        auto *runtime = ZoinGallery::GalleryRuntime::install(view.engine(), options);
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(QStringLiteral("selection-border"));
        QVERIFY(session);
        QVERIFY(session->applyExternalCatalog({
            QVariantMap{{QStringLiteral("entryId"), QStringLiteral("cursor")},
                        {QStringLiteral("index"), 0}, {QStringLiteral("name"), QStringLiteral("cursor.txt")}},
            QVariantMap{{QStringLiteral("entryId"), QStringLiteral("marked")},
                        {QStringLiteral("index"), 1}, {QStringLiteral("name"), QStringLiteral("marked.txt")}},
        }, 1));
        QVERIFY(session->applyExternalState(QStringLiteral("cursor"), 0,
                                            {QStringLiteral("marked")}, 1));
        QObject *panel = createPanel(view, session, mode, columns);
        QVERIFY(panel);
        QTest::mouseMove(&view, QPoint(4, 4));
        QTRY_VERIFY(!panel->property("pathViewportPlacementPending").toBool());
        auto *theme = panel->property("theme").value<QObject *>();
        QVERIFY(theme);
        QQuickItem *surface = nullptr;
        QTRY_VERIFY((surface = panel->findChild<QQuickItem *>(
            QStringLiteral("gallerySelectionSurface-1"))));
        const qreal dpr = view.devicePixelRatio();
        const auto verifyBorderPixels = [&] {
            const QRectF rect = surface->mapRectToScene(surface->boundingRect());
            for (const qreal coordinate : {rect.left(), rect.top(), rect.right(), rect.bottom()}) {
                const qreal physical = coordinate * dpr;
                QVERIFY2(qAbs(physical - qRound(physical)) < 0.001,
                         qPrintable(QStringLiteral("Selection edge at %1 physical px, DPR %2")
                                        .arg(physical, 0, 'f', 6).arg(dpr)));
            }
            const QColor border = theme->property("selection").value<QColor>();
            QImage frame;
            QTRY_VERIFY(!(frame = view.grabWindow()).isNull());
            for (int y = 0; y < qRound(dpr); ++y) {
                QVERIFY(sameColor(frame.pixelColor(qRound(rect.center().x() * dpr),
                                                   qRound(rect.top() * dpr) + y), border));
            }
        };
        verifyBorderPixels();
        // All modes have the same thin selection outline by default.
        QCOMPARE(surface->property("visualBorderWidth").toReal(), 1.0);
        QCOMPARE(surface->property("visualBorderColor").value<QColor>(),
                 theme->property("selection").value<QColor>());
        const QColor base = surface->property("color").value<QColor>();
        const QRectF entryGeometry = surface->parentItem()->mapRectToScene(
            surface->parentItem()->boundingRect());
        QSignalSpy actions(session, &ZoinGallery::GallerySession::actionRequested);
        QSignalSpy catalogChanges(session, &ZoinGallery::GallerySession::catalogRevisionChanged);
        for (const bool visible : {false, true, false}) {
            QVERIFY(theme->setProperty("showSelectionBorders", visible));
            QCOMPARE(surface->property("visualBorderWidth").toReal(), visible ? 1.0 : 0.0);
            QCOMPARE(surface->property("color").value<QColor>(), base);
            QCOMPARE(surface->parentItem()->property("itemTextColor").value<QColor>(),
                     theme->property("markedText").value<QColor>());
            QCOMPARE(surface->parentItem()->mapRectToScene(surface->parentItem()->boundingRect()),
                     entryGeometry);
        }
        QVERIFY(theme->setProperty("selection", QColor(QStringLiteral("#4ce19b"))));
        QVERIFY(theme->setProperty("showSelectionBorders", true));
        verifyBorderPixels();
        QCOMPARE(catalogChanges.size(), 0);
        QCOMPARE(actions.size(), 0);

        session->setCurrentIndex(1);
        QTRY_VERIFY(!panel->property("cursorChromeTransitionActive").toBool());
        QCOMPARE(surface->property("visualBorderWidth").toReal(), 1.0);
        QCOMPARE(surface->property("visualBorderColor").value<QColor>(),
                 theme->property("selection").value<QColor>());
        QVERIFY(theme->setProperty("showSelectionBorders", false));
        // Text-only selection never hides the independent keyboard cursor.
        QCOMPARE(surface->property("visualBorderWidth").toReal(), 1.0);
        QCOMPARE(surface->property("visualBorderColor").value<QColor>(),
                 theme->property(mode == QStringLiteral("details")
                                     ? "cursorBorder" : "cardCursorBorder").value<QColor>());
        QVERIFY(panel->setProperty("showCursor", false));
        QVERIFY(theme->setProperty("showSelectionBorders", true));
        QCOMPARE(surface->property("visualBorderWidth").toReal(), 1.0);
        verifyBorderPixels();
        QVERIFY(theme->setProperty("showSelectionBorders", false));
        QCOMPARE(surface->property("visualBorderWidth").toReal(), 0.0);
        runtime->shutdown();
    }

    void hoverComposesWithSelection_data()
    {
        QTest::addColumn<QString>("mode");
        QTest::addColumn<int>("columns");
        QTest::addColumn<bool>("selected");
        QTest::addColumn<bool>("current");
        QTest::addColumn<bool>("semanticBackground");
        QTest::addColumn<bool>("showBorders");
        for (const QString &mode : {QStringLiteral("masonry"),
                 QStringLiteral("columns"), QStringLiteral("details"),
                 QStringLiteral("grid"), QStringLiteral("icons")}) {
            for (const int columns : {2, 3}) {
                if (columns == 3 && mode != QStringLiteral("columns"))
                    continue;
                for (const bool selected : {false, true}) {
                    for (const bool current : {false, true}) {
                        for (const bool showBorders : {false, true}) {
                            const QByteArray name = QStringLiteral("%1-%2-marked%3-current%4-borders%5")
                                .arg(mode).arg(columns).arg(selected).arg(current).arg(showBorders).toUtf8();
                            QTest::newRow(name.constData())
                                << mode << columns << selected << current << false << showBorders;
                        }
                    }
                }
            }
        }
        QTest::newRow("details-semantic-normal")
            << QStringLiteral("details") << 2 << false << false << true << true;
        QTest::newRow("details-semantic-cursor")
            << QStringLiteral("details") << 2 << false << true << true << true;
        QTest::newRow("details-semantic-marked-cursor")
            << QStringLiteral("details") << 2 << true << true << true << true;
    }

    void hoverComposesWithSelection()
    {
        QFETCH(QString, mode);
        QFETCH(int, columns);
        QFETCH(bool, selected);
        QFETCH(bool, current);
        QFETCH(bool, semanticBackground);
        QFETCH(bool, showBorders);
        QQuickView view;
        ZoinGallery::RuntimeOptions options;
        options.persistentCache = false;
        auto *runtime = ZoinGallery::GalleryRuntime::install(view.engine(), options);
        QVERIFY(runtime);
        auto *session = runtime->createExternalSession(QStringLiteral("entry-hover"));
        QVERIFY(session);
        QVariantList entries;
        for (int row = 0; row < 4; ++row) {
            entries.append(QVariantMap{
                {QStringLiteral("entryId"), QStringLiteral("entry-%1").arg(row)},
                {QStringLiteral("index"), row},
                {QStringLiteral("name"), QStringLiteral("file-%1.txt").arg(row)},
                {QStringLiteral("isDir"), false},
                {QStringLiteral("isImage"), false},
            });
        }
        if (semanticBackground) {
            QVariantMap entry = entries.at(1).toMap();
            entry.insert(QStringLiteral("highlightStyle"), QVariantMap{
                {current ? QStringLiteral("cursor") : QStringLiteral("normal"),
                 QVariantMap{{QStringLiteral("background"), QStringLiteral("#543a67")}}},
                {QStringLiteral("selectedCursor"),
                 QVariantMap{{QStringLiteral("background"), QStringLiteral("#b74025")}}},
            });
            entries[1] = entry;
        }
        QVERIFY(session->applyExternalCatalog(entries, 1));
        const int cursor = current ? 1 : 0;
        QVERIFY(session->applyExternalState(QStringLiteral("entry-%1").arg(cursor),
            cursor, selected ? QStringList{QStringLiteral("entry-1")} : QStringList{}, 1));
        QObject *panel = createPanel(view, session, mode, columns);
        QVERIFY(panel);
        auto *theme = panel->property("theme").value<QObject *>();
        QVERIFY(theme);
        QVERIFY(theme->setProperty("showSelectionBorders", showBorders));
        QQuickItem *surface = nullptr;
        QTRY_VERIFY((surface = panel->findChild<QQuickItem *>(
            QStringLiteral("gallerySelectionSurface-1"))));
        QTest::mouseMove(&view, QPoint(4, 4));
        QTRY_VERIFY(!panel->property("cursorChromeTransitionActive").toBool());
        QTRY_VERIFY(!panel->property("pathViewportPlacementPending").toBool());
        QCOMPARE(surface->parentItem()->property("selected").toBool(), selected);
        QCOMPARE(surface->parentItem()->property("current").toBool(), current);
        QCOMPARE(panel->property("devicePixelRatio").toReal(), view.devicePixelRatio());
        const QVariant border = surface->property("visualBorderColor");
        const QVariant borderWidth = surface->property("visualBorderWidth");
        const QVariant text = surface->parentItem()->property("itemTextColor");
        QSignalSpy actions(session, &ZoinGallery::GallerySession::actionRequested);
        QSignalSpy cursorChanges(session, &ZoinGallery::GallerySession::currentIndexChanged);
        QSignalSpy selectionChanges(session, &ZoinGallery::GallerySession::selectionRevisionChanged);
        QSignalSpy catalogChanges(session, &ZoinGallery::GallerySession::catalogRevisionChanged);
        const auto surfaceColor = [surface] { return surface->property("color").value<QColor>(); };
        const QPoint point = surface->mapToScene(
            QPointF(surface->width() - 12, surface->height() / 2)).toPoint();
        const auto renderedColor = [&view, point] {
            const QImage frame = view.grabWindow();
            return frame.isNull() ? QColor() : frame.pixelColor(
                qRound(point.x() * frame.width() / qreal(view.width())),
                qRound(point.y() * frame.height() / qreal(view.height())));
        };

        for (int palette = 0; palette < 3; ++palette) {
            if (palette == 1) {
                // Change the active palette while the pointer remains over the row.
                QVERIFY(theme->setProperty("cursor", QColor(QStringLiteral("#65367e"))));
                QVERIFY(theme->setProperty("cursorBackground", QColor(QStringLiteral("#4f3672"))));
                QVERIFY(theme->setProperty("itemBackground", QColor(QStringLiteral("#34263e"))));
                QVERIFY(theme->setProperty("itemHover", QColor(QStringLiteral("#6086e4ad"))));
            } else if (palette == 2) {
                QVERIFY(theme->setProperty("cursor", QColor(QStringLiteral("#802e507b"))));
                QVERIFY(theme->setProperty("cursorBackground", QColor(QStringLiteral("#803f3268"))));
                QVERIFY(theme->setProperty("itemBackground", QColor(QStringLiteral("#80523748"))));
                QVERIFY(theme->setProperty("itemHover", QColor(QStringLiteral("#508fafde"))));
            }
            const QColor base = semanticBackground ? QColor(QStringLiteral("#543a67"))
                : current ? theme->property(mode == QStringLiteral("details")
                                                ? "cursorBackground" : "cursor").value<QColor>()
                : (mode == QStringLiteral("masonry") || mode == QStringLiteral("grid"))
                    ? theme->property("itemBackground").value<QColor>() : QColor(Qt::transparent);
            const QColor hover = composite(base, theme->property("itemHover").value<QColor>());
            QTest::mouseMove(&view, point);
            QTRY_COMPARE(panel->property("hoveredIndex").toInt(), 1);
            QTRY_VERIFY_WITH_TIMEOUT(sameColor(surfaceColor(), hover), 1000);
            QVERIFY(surfaceColor() != base);
            const QColor background = theme->property("panelBackground").value<QColor>();
            QTRY_VERIFY_WITH_TIMEOUT(sameColor(renderedColor(), composite(background, hover)), 1000);
            QCOMPARE(surface->property("visualBorderColor"), border);
            QCOMPARE(surface->property("visualBorderWidth"), borderWidth);
            QCOMPARE(surface->parentItem()->property("itemTextColor"), text);

            QTest::mouseMove(&view, QPoint(4, 4));
            QTRY_COMPARE(panel->property("hoveredIndex").toInt(), -1);
            QCOMPARE(surfaceColor(), base);
            QTRY_VERIFY_WITH_TIMEOUT(sameColor(renderedColor(), composite(background, base)), 1000);
            QTest::mouseMove(&view, point);
            QTRY_COMPARE(panel->property("hoveredIndex").toInt(), 1);
        }
        QCOMPARE(cursorChanges.size(), 0);
        QCOMPARE(selectionChanges.size(), 0);
        QCOMPARE(catalogChanges.size(), 0);
        QCOMPARE(actions.size(), 0);
        runtime->shutdown();
    }
};

QTEST_MAIN(GalleryEntryHoverTest)
#include "GalleryEntryHoverTest.moc"
