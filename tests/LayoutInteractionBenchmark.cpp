#include <ZoinGallery/GalleryRuntime.h>
#include <ZoinGallery/GallerySession.h>

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QEventLoop>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRectF>
#include <QSGRendererInterface>
#include <QTextStream>
#include <QThread>

#include <algorithm>
#include <functional>
#include <memory>

namespace {

bool waitFor(const std::function<bool()> &predicate, int timeoutMs) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QThread::yieldCurrentThread();
    }
    return predicate();
}

qint64 percentile(QList<qint64> values, qreal fraction) {
    if (values.isEmpty()) {
        return -1;
    }
    std::sort(values.begin(), values.end());
    const qsizetype index = qBound<qsizetype>(
        0, qRound64((values.size() - 1) * fraction), values.size() - 1);
    return values.at(index);
}

QVariantList syntheticCatalog(int count) {
    QVariantList entries;
    entries.reserve(count);
    for (int index = 0; index < count; ++index) {
        const bool folder = index % 11 == 0;
        const QString extension = folder
            ? QString() : (index % 3 == 0 ? QStringLiteral("cpp")
                                          : QStringLiteral("txt"));
        const QString baseName = QStringLiteral("synthetic-entry-%1")
                                     .arg(index, 6, 10, QLatin1Char('0'));
        const QString name = extension.isEmpty()
            ? baseName : baseName + QLatin1Char('.') + extension;
        entries.append(QVariantMap{
            {QStringLiteral("entryId"),
             QStringLiteral("synthetic:%1").arg(index)},
            {QStringLiteral("index"), index},
            {QStringLiteral("name"), name},
            {QStringLiteral("localPath"),
             QStringLiteral("/virtual/zoin-layout-benchmark/%1").arg(name)},
            {QStringLiteral("isDir"), folder},
            {QStringLiteral("isImage"), false},
            {QStringLiteral("selected"), index % 127 == 0},
            {QStringLiteral("mtimeNs"), qint64(1'700'000'000'000'000'000LL + index)},
            {QStringLiteral("size"), qint64(index) * 4096},
            {QStringLiteral("displayFields"), QVariantMap{
                 {QStringLiteral("displayBaseName"), baseName},
                 {QStringLiteral("displayExtension"), extension},
                 {QStringLiteral("sizeText"),
                  QStringLiteral("%1 KiB").arg(index * 4)},
                 {QStringLiteral("mtimeText"),
                  QStringLiteral("2026-08-08 12:%1")
                      .arg(index % 60, 2, 10, QLatin1Char('0'))},
                 {QStringLiteral("modeText"),
                  folder ? QStringLiteral("drwxr-xr-x")
                         : QStringLiteral("-rw-r--r--")},
             }},
        });
    }
    return entries;
}

int instantiatedEntryCount(QObject *root) {
    int result = 0;
    const QList<QObject *> objects = root->findChildren<QObject *>();
    for (QObject *object : objects) {
        if (object->objectName().startsWith(
                QStringLiteral("galleryFallbackIcon-"))) {
            ++result;
        }
    }
    return result;
}

QJsonObject benchmarkMode(QQuickItem *panel, QObject *layout,
                          QQuickWindow *window, const QString &mode,
                          int expectedNativeMode, int cycles,
                          int timeoutMs, const QString &screenshotDirectory) {
    QElapsedTimer switchTimer;
    switchTimer.start();
    panel->setProperty("presentationMode", mode);
    const bool switched = waitFor([&] {
        return layout->property("presentationMode").toInt() ==
                   expectedNativeMode &&
               layout->property("count").toInt() > 0 &&
               !layout->property("visibleIndexes").toList().isEmpty();
    }, timeoutMs);
    window->update();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    const qint64 switchMs = switchTimer.elapsed();

    const qreal viewportHeight = layout->property("height").toReal();
    const qreal contentHeight = layout->property("contentHeight").toReal();
    const qreal maximumY = qMax<qreal>(0, contentHeight - viewportHeight);
    QList<qint64> scrollSamplesUs;
    scrollSamplesUs.reserve(cycles);
    for (int cycle = 0; cycle < cycles; ++cycle) {
        const qreal position = cycles <= 1
            ? maximumY
            : maximumY * qreal(cycle) / qreal(cycles - 1);
        QElapsedTimer sample;
        sample.start();
        layout->setProperty("contentY", position);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
        scrollSamplesUs.append(sample.nsecsElapsed() / 1000);
    }
    window->update();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);

    bool screenshotSaved = false;
    if (!screenshotDirectory.isEmpty()) {
        QDir().mkpath(screenshotDirectory);
        const QImage screenshot = window->grabWindow();
        screenshotSaved = !screenshot.isNull() && screenshot.save(
            QDir(screenshotDirectory).filePath(mode + QStringLiteral(".png")));
    }

    const qsizetype visibleCount =
        layout->property("visibleIndexes").toList().size();
    const qsizetype overscanCount =
        layout->property("overscanIndexes").toList().size();
    const int instantiated = instantiatedEntryCount(panel);
    // A normal GUI event-loop turn reclaims the outgoing Loader item after
    // the replacement frame is ready. Tight benchmark loops do not process
    // DeferredDelete automatically, so drain it outside the measured switch
    // before starting the next presentation.
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    return {
        {QStringLiteral("mode"), mode},
        {QStringLiteral("switched"), switched},
        {QStringLiteral("nativeMode"),
         layout->property("presentationMode").toInt()},
        {QStringLiteral("presentationSwitchPending"),
         panel->property("presentationSwitchPending").toBool()},
        {QStringLiteral("switchMs"), switchMs},
        {QStringLiteral("contentHeight"), contentHeight},
        {QStringLiteral("visibleCount"), visibleCount},
        {QStringLiteral("overscanCount"), overscanCount},
        {QStringLiteral("instantiatedEntryCount"), instantiated},
        {QStringLiteral("scrollCycles"), cycles},
        {QStringLiteral("scrollP50Us"),
         percentile(scrollSamplesUs, 0.50)},
        {QStringLiteral("scrollP95Us"),
         percentile(scrollSamplesUs, 0.95)},
        {QStringLiteral("scrollMaxUs"),
         scrollSamplesUs.isEmpty()
             ? -1 : *std::max_element(scrollSamplesUs.cbegin(),
                                      scrollSamplesUs.cend())},
        {QStringLiteral("screenshotSaved"), screenshotSaved},
    };
}

QRectF indexGeometry(QObject *layout, int index) {
    QRectF geometry;
    if (layout) {
        QMetaObject::invokeMethod(
            layout, "indexGeometry", Qt::DirectConnection,
            Q_RETURN_ARG(QRectF, geometry), Q_ARG(int, index));
    }
    return geometry;
}

QJsonObject benchmarkVisibleModeSwitches(
    QQuickItem *panel, QObject *layout,
    ZoinGallery::GallerySession *session, QQuickWindow *window,
    int anchorIndex, int cycles, int timeoutMs) {
    if (!panel || !layout || !session || !window || anchorIndex < 0) {
        return {{QStringLiteral("ready"), false}};
    }

    session->setCurrentIndex(anchorIndex);
    panel->setProperty("presentationMode", QStringLiteral("details"));
    const bool initialReady = waitFor([&] {
        return layout->property("presentationMode").toInt() == 2
            && !panel->property("presentationSwitchPending").toBool()
            && indexGeometry(layout, anchorIndex).isValid();
    }, timeoutMs);
    if (!initialReady) {
        return {{QStringLiteral("ready"), false}};
    }
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    const QRectF initialGeometry = indexGeometry(layout, anchorIndex);
    layout->setProperty("contentY", qMax<qreal>(
        0, initialGeometry.top()
               - layout->property("height").toReal() * 0.35));
    session->setPanelScrollOffset(
        layout->property("contentY").toReal());
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);

    QList<qint64> samplesUs;
    samplesUs.reserve(cycles);
    QJsonArray transitions;
    bool allSwitched = true;
    bool anchorAlwaysVisible = true;
    int maximumInstantiated = 0;
    QString from = QStringLiteral("details");
    for (int cycle = 0; cycle < cycles; ++cycle) {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        const QVariantList beforeVisible =
            layout->property("visibleIndexes").toList();
        const QString to = cycle % 2 == 0
            ? QStringLiteral("grid") : QStringLiteral("details");
        const int nativeMode = cycle % 2 == 0 ? 3 : 2;
        QElapsedTimer timer;
        timer.start();
        panel->setProperty("presentationMode", to);
        const bool switched = waitFor([&] {
            return layout->property("presentationMode").toInt()
                       == nativeMode
                && !panel->property("presentationSwitchPending").toBool()
                && !layout->property("visibleIndexes").toList().isEmpty();
        }, timeoutMs);
        window->update();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        const qint64 durationUs = timer.nsecsElapsed() / 1000;
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        samplesUs.append(durationUs);

        const QRectF geometry = indexGeometry(layout, anchorIndex);
        const qreal contentY = layout->property("contentY").toReal();
        const qreal viewportHeight = layout->property("height").toReal();
        const bool anchorVisible = geometry.isValid()
            && geometry.bottom() > contentY
            && geometry.top() < contentY + viewportHeight;
        const int instantiated = instantiatedEntryCount(panel);
        const QVariantList afterVisible =
            layout->property("visibleIndexes").toList();
        maximumInstantiated = qMax(maximumInstantiated, instantiated);
        allSwitched = allSwitched && switched;
        anchorAlwaysVisible = anchorAlwaysVisible && anchorVisible;
        transitions.append(QJsonObject{
            {QStringLiteral("from"), from},
            {QStringLiteral("to"), to},
            {QStringLiteral("durationUs"), durationUs},
            {QStringLiteral("switched"), switched},
            {QStringLiteral("anchorVisible"), anchorVisible},
            {QStringLiteral("instantiatedEntryCount"), instantiated},
            {QStringLiteral("beforeVisibleFirst"),
             beforeVisible.isEmpty() ? -1 : beforeVisible.constFirst().toInt()},
            {QStringLiteral("beforeVisibleLast"),
             beforeVisible.isEmpty() ? -1 : beforeVisible.constLast().toInt()},
            {QStringLiteral("afterVisibleFirst"),
             afterVisible.isEmpty() ? -1 : afterVisible.constFirst().toInt()},
            {QStringLiteral("afterVisibleLast"),
             afterVisible.isEmpty() ? -1 : afterVisible.constLast().toInt()},
        });
        from = to;
    }

    return {
        {QStringLiteral("ready"), true},
        {QStringLiteral("anchorIndex"), anchorIndex},
        {QStringLiteral("cycles"), cycles},
        {QStringLiteral("allSwitched"), allSwitched},
        {QStringLiteral("anchorAlwaysVisible"), anchorAlwaysVisible},
        {QStringLiteral("firstSwitchUs"),
         samplesUs.isEmpty() ? -1 : samplesUs.constFirst()},
        {QStringLiteral("switchP50Us"), percentile(samplesUs, 0.50)},
        {QStringLiteral("switchP95Us"), percentile(samplesUs, 0.95)},
        {QStringLiteral("switchMaxUs"),
         samplesUs.isEmpty()
             ? -1 : *std::max_element(samplesUs.cbegin(), samplesUs.cend())},
        {QStringLiteral("maximumInstantiatedEntryCount"),
         maximumInstantiated},
        {QStringLiteral("transitions"), transitions},
    };
}

} // namespace

int main(int argc, char **argv) {
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    QGuiApplication application(argc, argv);
    QCoreApplication::setApplicationName(
        QStringLiteral("ZoinGalleryLayoutInteractionBenchmark"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Synthetic large-catalog GalleryPanel benchmark"));
    parser.addHelpOption();
    const QCommandLineOption countOption(
        QStringList{QStringLiteral("n"), QStringLiteral("count")},
        QStringLiteral("Synthetic catalog entry count."),
        QStringLiteral("count"), QStringLiteral("10000"));
    const QCommandLineOption cyclesOption(
        QStringList{QStringLiteral("c"), QStringLiteral("cycles")},
        QStringLiteral("Scroll interaction samples per mode."),
        QStringLiteral("cycles"), QStringLiteral("240"));
    const QCommandLineOption timeoutOption(
        QStringList{QStringLiteral("timeout-ms")},
        QStringLiteral("Per-stage timeout."), QStringLiteral("ms"),
        QStringLiteral("10000"));
    const QCommandLineOption screenshotOption(
        QStringList{QStringLiteral("screenshot-dir")},
        QStringLiteral("Save one visual QA frame for every mode."),
        QStringLiteral("directory"));
    const QCommandLineOption strictOption(
        QStringList{QStringLiteral("strict")},
        QStringLiteral("Fail if virtualization or interaction guardrails are exceeded."));
    const QCommandLineOption pagedOption(
        QStringList{QStringLiteral("paged")},
        QStringLiteral("Apply only a bounded initial catalog window while "
                       "advertising the exact logical row count."));
    const QCommandLineOption initialWindowOption(
        QStringList{QStringLiteral("initial-window")},
        QStringLiteral("Initial row payload used with --paged."),
        QStringLiteral("rows"), QStringLiteral("64"));
    const QCommandLineOption switchCyclesOption(
        QStringList{QStringLiteral("switch-cycles")},
        QStringLiteral("Visible Details/Grid mode-switch samples."),
        QStringLiteral("cycles"), QStringLiteral("8"));
    parser.addOptions({countOption, cyclesOption, timeoutOption,
                       screenshotOption, strictOption, pagedOption,
                       initialWindowOption, switchCyclesOption});
    parser.process(application);

    const int count = qMax(1, parser.value(countOption).toInt());
    const int cycles = qMax(1, parser.value(cyclesOption).toInt());
    const int timeoutMs = qMax(100, parser.value(timeoutOption).toInt());
    const bool paged = parser.isSet(pagedOption);
    const int initialWindow = qBound(
        1, parser.value(initialWindowOption).toInt(), count);
    const int switchCycles = qMax(
        1, parser.value(switchCyclesOption).toInt());

    QQmlEngine engine;
    engine.addImportPath(QStringLiteral(ZOIN_BENCH_QML_IMPORT_PATH));
    ZoinGallery::RuntimeOptions runtimeOptions;
    runtimeOptions.persistentCache = false;
    runtimeOptions.maxDecodeThreads = 2;
    runtimeOptions.thumbnailCacheByteBudget = 32 * 1024 * 1024;
    auto *runtime = ZoinGallery::GalleryRuntime::install(
        &engine, runtimeOptions);
    if (!runtime) {
        QTextStream(stderr) << "Could not install GalleryRuntime\n";
        return 2;
    }
    auto *session = runtime->createExternalSession(
        QStringLiteral("layout-benchmark"));
    if (!session) {
        QTextStream(stderr) << "Could not create external session\n";
        return 2;
    }

    const int catalogPayloadCount = paged ? initialWindow : count;
    const QVariantList catalog = syntheticCatalog(catalogPayloadCount);
    QVariantMap catalogOptions;
    if (paged) {
        catalogOptions = {
            {QStringLiteral("currentPath"),
             QStringLiteral("/virtual/zoin-layout-benchmark")},
            {QStringLiteral("metadataDeferred"), true},
            {QStringLiteral("catalogRowsDeferred"), true},
            {QStringLiteral("totalCount"), count},
            {QStringLiteral("cursorIndex"), 0},
            {QStringLiteral("cursorEntryId"),
             QStringLiteral("synthetic:0")},
        };
    }
    QElapsedTimer catalogTimer;
    catalogTimer.start();
    const bool catalogApplied = session->applyExternalCatalog(
        catalog, 1, catalogOptions);
    const qint64 catalogApplyMs = catalogTimer.elapsed();
    session->setCurrentIndex(0);

    engine.rootContext()->setContextProperty(
        QStringLiteral("benchmarkSession"), session);
    QQmlComponent component(&engine);
    component.setData(R"QML(
        import QtQuick
        import ZoinGallery 1.0
        GalleryPanel {
            width: 1280
            height: 800
            session: benchmarkSession
            autoFocus: false
            showCursor: true
            theme: GalleryThemePalette {
                panelBackground: "#121820"
                text: "#e7edf5"
                mutedText: "#8e9baa"
                cursor: "#1587bf"
                selection: "#d3a335"
            }
        }
    )QML", QUrl(QStringLiteral("benchmark:/GalleryPanel.qml")));

    waitFor([&] {
        return component.status() != QQmlComponent::Loading;
    }, timeoutMs);
    if (component.status() != QQmlComponent::Ready) {
        QTextStream(stderr) << component.errorString() << '\n';
        runtime->shutdown();
        return 2;
    }

    QElapsedTimer createTimer;
    createTimer.start();
    std::unique_ptr<QObject> created(component.create(engine.rootContext()));
    const qint64 qmlCreateMs = createTimer.elapsed();
    if (!created) {
        QTextStream(stderr) << component.errorString() << '\n';
        return 2;
    }
    auto *panel = qobject_cast<QQuickItem *>(created.get());
    if (!panel) {
        QTextStream(stderr) << "GalleryPanel root is not a QQuickItem\n";
        return 2;
    }

    QQuickWindow window;
    window.setTitle(QStringLiteral("ZoinGallery layout benchmark"));
    window.resize(1280, 800);
    panel->setParentItem(window.contentItem());
    window.show();

    QObject *layout = panel->findChild<QObject *>(
        QStringLiteral("galleryViewportItem"));
    QElapsedTimer firstLayoutWaitTimer;
    firstLayoutWaitTimer.start();
    const bool firstLayoutReady = layout && waitFor([&] {
        return layout->property("count").toInt() == count &&
               !layout->property("visibleIndexes").toList().isEmpty();
    }, timeoutMs);
    // Component creation can synchronously complete the first layout. Report
    // end-to-end QML creation-to-ready latency rather than a misleading zero
    // millisecond post-create wait in that fast path.
    const qint64 firstLayoutMs =
        qmlCreateMs + firstLayoutWaitTimer.elapsed();

    QJsonArray modes;
    QJsonObject visibleModeSwitches;
    if (layout) {
        const QList<QPair<QString, int>> modeValues{
            {QStringLiteral("masonry"), 0},
            {QStringLiteral("columns"), 1},
            {QStringLiteral("details"), 2},
            {QStringLiteral("grid"), 3},
            {QStringLiteral("icons"), 4},
        };
        for (const auto &[mode, nativeMode] : modeValues) {
            modes.append(benchmarkMode(
                panel, layout, &window, mode, nativeMode, cycles,
                timeoutMs, parser.value(screenshotOption)));
        }
        const int anchorIndex = paged
            ? qMin(catalogPayloadCount - 1, 32) : count / 2;
        visibleModeSwitches = benchmarkVisibleModeSwitches(
            panel, layout, session, &window, anchorIndex,
            switchCycles, timeoutMs);
    }

    QJsonObject report{
        {QStringLiteral("catalogCount"), count},
        {QStringLiteral("pagedCatalog"), paged},
        {QStringLiteral("catalogPayloadCount"), catalogPayloadCount},
        {QStringLiteral("catalogApplied"), catalogApplied},
        {QStringLiteral("catalogApplyMs"), catalogApplyMs},
        {QStringLiteral("qmlCreateMs"), qmlCreateMs},
        {QStringLiteral("firstLayoutReady"), firstLayoutReady},
        {QStringLiteral("firstLayoutMs"), firstLayoutMs},
        {QStringLiteral("thumbnailCacheBudgetBytes"),
         runtime->thumbnailCacheByteBudget()},
        {QStringLiteral("thumbnailCacheRetainedBytes"),
         runtime->thumbnailCacheRetainedBytes()},
        {QStringLiteral("modes"), modes},
        {QStringLiteral("visibleModeSwitches"), visibleModeSwitches},
    };
    QTextStream(stdout) << QJsonDocument(report).toJson(
        QJsonDocument::Indented);

    bool guardrailsPassed = catalogApplied && firstLayoutReady &&
        modes.size() == 5;
    for (const QJsonValue &value : modes) {
        const QJsonObject mode = value.toObject();
        guardrailsPassed = guardrailsPassed &&
            mode.value(QStringLiteral("switched")).toBool() &&
            mode.value(QStringLiteral("instantiatedEntryCount")).toInt() <=
                qMin(count, 1000) &&
            mode.value(QStringLiteral("scrollP95Us")).toInteger() < 50'000;
    }
    guardrailsPassed = guardrailsPassed
        && visibleModeSwitches.value(QStringLiteral("ready")).toBool()
        && visibleModeSwitches.value(
               QStringLiteral("allSwitched")).toBool()
        && visibleModeSwitches.value(
               QStringLiteral("anchorAlwaysVisible")).toBool()
        && visibleModeSwitches.value(
               QStringLiteral("firstSwitchUs")).toInteger() < 33'000
        && visibleModeSwitches.value(
               QStringLiteral("switchP95Us")).toInteger() < 33'000;

    window.hide();
    panel->setParentItem(nullptr);
    created.reset();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    runtime->shutdown();
    return parser.isSet(strictOption) && !guardrailsPassed ? 1 : 0;
}
