#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QElapsedTimer>
#include <QStandardPaths>
#include <QQuickStyle>

#include "ViewerController.h"

int main(int argc, char *argv[])
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif
    QGuiApplication app(argc, argv);

    QQuickStyle::setStyle("ZGStyle");

    QQmlApplicationEngine engine;
    engine.addImportPath(":/");
    const QUrl url(QStringLiteral("qrc:/qml/main.qml"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
                     &app, [url](QObject *obj, const QUrl &objUrl) {
        if (!obj && url == objUrl)
            QCoreApplication::exit(-1);
    }, Qt::QueuedConnection);

    ViewerController *controller = new ViewerController(&engine);
    engine.load(url);

//    QQuickView *view = new QQuickView;
//    QQmlEngine *engine = view->engine();

//    ViewerController *controller = new ViewerController(engine);
//    view->setSource(url);

//    view->setGeometry(600, 400, 500, 500);
//    view->show();

    if (qApp->arguments().size() > 1) {
        QString path = qApp->arguments()[1].replace("\"", "");
        controller->cd(path);
    }
    else {
        controller->cd(QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).first());
    }

    return app.exec();
}
