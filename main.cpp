#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QElapsedTimer>
#include <QStandardPaths>
#include <QQuickStyle>
#include <QSettings>
#include <QImageReader>

#include "ViewerController.h"
#include "MainWindow.h"

#if defined(__USE_QWK)
#include <QWKQuick/qwkquickglobal.h>
#else
#include "DummyQWK.h"
#endif


int main(int argc, char *argv[])
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif

#if defined(Q_OS_WIN)
    // qputenv("QT_QPA_PLATFORM", "windows:darkmode=2");
#endif
    //qputenv("QSG_INFO", "1");
    qputenv("QSG_RHI_BACKEND", "d3d12");
    // qputenv("QSG_NO_VSYNC", "1");

    QGuiApplication app(argc, argv);
    app.setOrganizationName("Zoin");
    app.setOrganizationDomain("zoingallery.com");
    app.setApplicationName("ZoinGallery");

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
    const QUrl url(QStringLiteral("qrc:/qml/main.qml"));
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

    engine.load(url);

    QSettings set;
    QString savedPath = set.value("currentPath").toString();

    if (qApp->arguments().size() > 1) {
        QString path = qApp->arguments()[1].replace("\"", "");
        controller->cd(path);
    }
    else if (!savedPath.isEmpty()) {
        controller->cd(savedPath);
    }
    else {
        controller->cd(QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).first());
    }

    return app.exec();
}
