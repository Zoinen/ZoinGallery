#ifndef BACKGROUNDINSTANCE_H
#define BACKGROUNDINSTANCE_H

#include <QObject>

class QLocalServer;
class ViewerController;

class BackgroundInstance : public QObject {
    Q_OBJECT

public:
    static QString serverName();
    static bool tryForwardToRunningInstance(const QString &filePath);

    explicit BackgroundInstance(ViewerController *controller, QObject *parent = nullptr);
    ~BackgroundInstance() override;

    bool startServer();
    void stopServer();

private:
    void handleConnection();

    ViewerController *_controller;
    QLocalServer *_server;
};

#endif // BACKGROUNDINSTANCE_H
