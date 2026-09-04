#include <ZoinGallery/GalleryIconResolver.h>

#include <QTest>

class GalleryIconResolverTest final : public QObject {
    Q_OBJECT

private slots:
    void extractsOnlyBundledSemanticKeys() {
        ZoinGallery::GalleryIconResolver resolver;
        QCOMPARE(resolver.keyFromSource(QStringLiteral(
                     "qrc:/AnyHost/icons/lucide/folder-up.svg")),
                 QStringLiteral("folder-up"));
        QCOMPARE(resolver.keyFromSource(QStringLiteral(
                     "qrc:/AnyHost/icons/lucide-gallery/file.svg?x=1")),
                 QStringLiteral("file"));
        QVERIFY(resolver.keyFromSource(QStringLiteral(
                    "file:///custom/folder-up.svg")).isEmpty());
        QVERIFY(resolver.keyFromSource(QStringLiteral(
                    "qrc:/custom/folder-up.svg")).isEmpty());
    }

    void hostPrefixesResolveSemanticKeys() {
        ZoinGallery::GalleryIconResolver resolver;
        resolver.setCompactPrefix(QStringLiteral("qrc:/Host/lucide"));
        resolver.setLargePrefix(QStringLiteral("qrc:/Host/lucide-gallery/"));
        QCOMPARE(resolver.resolve(QStringLiteral("folder-up"), {}, false,
                                  true, false, true),
                 QStringLiteral("qrc:/Host/lucide/folder-up.svg"));
        QCOMPARE(resolver.resolve(QStringLiteral("folder-up"), {}, true,
                                  true, false, true),
                 QStringLiteral(
                     "qrc:/Host/lucide-gallery/folder-up.svg"));
    }

    void customSourcesRemainOpaque() {
        ZoinGallery::GalleryIconResolver resolver;
        const QString custom = QStringLiteral("file:///theme/custom.svg");
        QCOMPARE(resolver.resolve({}, custom, true, false, false, false),
                 custom);
        QVERIFY(!resolver.isMonochrome({}, custom));
    }

    void providerLucideRouteIsNotReplacedByStaticResource() {
        ZoinGallery::GalleryIconResolver resolver;
        resolver.setCompactPrefix(QStringLiteral("qrc:/host/lucide"));
        const QString source = QStringLiteral(
            "image://icons/lucide/ZmlsZQ?size=128&dpr=2&revision=1");
        QVERIFY(!resolver.keyFromSource(source).isEmpty());
        QVERIFY(resolver.isMonochrome({}, source));
        QCOMPARE(resolver.resolve({}, source, false, false, false, false),
                 source);
        QCOMPARE(resolver.resolve({}, source, true, false, false, false),
                 source);
    }

    void retargetsOnlySupportedImageProviders() {
        ZoinGallery::GalleryIconResolver resolver;
        const QString source = QStringLiteral(
            "image://f4/file/key?size=16&dpr=1");
        const QString result = resolver.retargetProviderSource(
            source, 24, 1.5, QStringLiteral("#AABBCC"), true);
        QVERIFY(result.contains(QStringLiteral("size=24")));
        QVERIFY(result.contains(QStringLiteral("dpr=1.5")));
        QVERIFY(result.contains(QStringLiteral("color=%23AABBCC")));
        QCOMPARE(resolver.retargetProviderSource(
                     QStringLiteral("file:///tmp/icon.svg"), 24, 2.0,
                     QStringLiteral("#fff"), true),
                 QStringLiteral("file:///tmp/icon.svg"));
    }
};

QTEST_MAIN(GalleryIconResolverTest)
#include "GalleryIconResolverTest.moc"
