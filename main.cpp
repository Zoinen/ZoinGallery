#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QElapsedTimer>
#include <QStandardPaths>
#include <QQuickStyle>
#include <QSettings>

#include "ViewerController.h"
#include "MainWindow.h"

int main(int argc, char *argv[])
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif

#if defined(Q_OS_WIN)
    qputenv("QT_QPA_PLATFORM", "windows:darkmode=2");
#endif

    QGuiApplication app(argc, argv);
    app.setOrganizationName("Zoin");
    app.setOrganizationDomain("zoingallery.com");
    app.setApplicationName("ZoinGallery");

    QQuickStyle::setStyle("ZGStyle");

    qmlRegisterType<MainWindow>("ZoinGallery.MainWindow", 1, 0, "MainWindow");
    qmlRegisterRevision<QWindow, 1>("ZoinGallery.MainWindow", 1, 0);
    qmlRegisterRevision<QQuickWindow, 1>("ZoinGallery.MainWindow", 1, 0);

    QQmlApplicationEngine engine;
    engine.addImportPath(":/");
    const QUrl url(QStringLiteral("qrc:/qml/main.qml"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated, &app, [url](QObject *obj, const QUrl &objUrl) {
        if (!obj && url == objUrl) {
            QCoreApplication::exit(-1);
        }
    }, Qt::QueuedConnection);

    ViewerController *controller = new ViewerController(&engine);
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
