#include <QApplication>
#include <QQmlApplicationEngine>
#include <QElapsedTimer>
#include <QQuickStyle>
#include <QSettings>
#include <QDebug>
#include <QImageReader>

#include "ViewerController.h"
#include "MainWindow.h"
#include "LaunchOptions.h"
#include "BackgroundInstance.h"

#if defined(__USE_QWK)
#include <QWKQuick/qwkquickglobal.h>
#else
#include "DummyQWK.h"
#endif
#include <QDirIterator>

#if defined(Q_OS_WIN)
#include <windows.h>
#include <cstdio>
#endif

int main(int argc, char *argv[])
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif

#if defined(Q_OS_WIN)
    // qputenv("QT_QPA_PLATFORM", "windows:darkmode=2");
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        (void)freopen("CONOUT$", "w", stdout);
        (void)freopen("CONOUT$", "w", stderr);
    }
    qputenv("QT_FORCE_STDERR_LOGGING", "1");
#endif
    //qputenv("QSG_INFO", "1");
#if defined(Q_OS_WIN)
    qputenv("QSG_RHI_BACKEND", "d3d12");
#endif
    // qputenv("QSG_NO_VSYNC", "1");

    QApplication app(argc, argv);
    app.setOrganizationName("Zoin");
    app.setOrganizationDomain("zoingallery.com");
    app.setApplicationName("ZoinGallery");

    const LaunchOptions launchOptions = parseLaunchOptions(app.arguments());

    if (launchOptions.backgroundMode) {
        if (BackgroundInstance::tryForwardToRunningInstance(launchOptions.filePath)) {
            return 0;
        }
        app.setQuitOnLastWindowClosed(false);
    }

    QQuickStyle::setStyle("ZGStyle");
#if defined(__USE_QWK)
    QQuickWindow::setDefaultAlphaBuffer(true);
#endif

    qmlRegisterType<MainWindow>("ZoinGallery.MainWindow", 1, 0, "MainWindow");
    qmlRegisterRevision<QWindow, 1>("ZoinGallery.MainWindow", 1, 0);
    qmlRegisterRevision<QQuickWindow, 1>("ZoinGallery.MainWindow", 1, 0);

    QImageReader::setAllocationLimit(0);

    QQmlApplicationEngine engine;
    engine.addImportPath(":/");
    engine.addImportPath(":/ZoinGallery");
    const QUrl url(QStringLiteral("qrc:/ZoinGallery/qml/main.qml"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated, &app, [url](QObject *obj, const QUrl &objUrl) {
        if (!obj && url == objUrl) {
            QCoreApplication::exit(-1);
        }
    }, Qt::QueuedConnection);

    ViewerController *controller = new ViewerController(&engine);

#if defined(__USE_QWK)
    qDebug() << "Using QWK";
    QWK::registerTypes(&engine);
#else
    qDebug() << "Using dummy QWK";
    DummyQWK::registerTypes(&engine);
#endif

    if (!launchOptions.filePath.isEmpty()) {
        controller->setStartupFilePath(launchOptions.filePath);
    }

    if (launchOptions.backgroundMode) {
        controller->setBackgroundMode(true);
    }

    engine.load(url);

    if (launchOptions.backgroundMode) {
        const QList<QObject *> roots = engine.rootObjects();
        if (!roots.isEmpty()) {
            if (auto *mainWindow = qobject_cast<MainWindow *>(roots.first())) {
                controller->initializeBackgroundMode(mainWindow);
            }
        }
    }

    return app.exec();
}
