#include "BackgroundInstance.h"

#include "ViewerController.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>

#if defined(Q_OS_WIN)
#include <windows.h>
#endif

namespace {
constexpr int kConnectTimeoutMs = 300;

#if defined(Q_OS_WIN)
QString instanceMetadataPath() {
    const QString tempPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    const QString basePath = tempPath.isEmpty() ? QDir::tempPath() : tempPath;
    return QDir(basePath).filePath(BackgroundInstance::serverName() + QStringLiteral(".pid"));
}

void writeInstanceMetadata() {
    QSaveFile file(instanceMetadataPath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return;
    }

    file.write(QByteArray::number(QCoreApplication::applicationPid()));
    file.write("\n");
    file.commit();
}

void removeInstanceMetadata() {
    QFile::remove(instanceMetadataPath());
}

void allowRunningInstanceForegroundActivation() {
    QFile file(instanceMetadataPath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    bool ok = false;
    const unsigned long processId = QString::fromUtf8(file.readAll()).trimmed().toULong(&ok);
    if (!ok || processId == 0) {
        return;
    }

    AllowSetForegroundWindow(static_cast<DWORD>(processId));
}
#else
void writeInstanceMetadata() {
}

void removeInstanceMetadata() {
}

void allowRunningInstanceForegroundActivation() {
}
#endif
}

QString BackgroundInstance::serverName() {
    return QStringLiteral("ZoinGallery-background-%1").arg(QString::fromLocal8Bit(qgetenv("USERNAME")));
}

bool BackgroundInstance::tryForwardToRunningInstance(const QString &filePath) {
    QLocalSocket socket;
    socket.connectToServer(serverName());
    if (!socket.waitForConnected(kConnectTimeoutMs)) {
        return false;
    }

    allowRunningInstanceForegroundActivation();

    const QByteArray message = filePath.toUtf8() + '\n';
    if (socket.write(message) != message.size() || !socket.waitForBytesWritten(kConnectTimeoutMs)) {
        return false;
    }

    return true;
}

BackgroundInstance::BackgroundInstance(ViewerController *controller, QObject *parent)
    : QObject(parent)
    , _controller(controller)
    , _server(nullptr) {
}

BackgroundInstance::~BackgroundInstance() {
    stopServer();
}

bool BackgroundInstance::startServer() {
    stopServer();

    _server = new QLocalServer(this);
    QLocalServer::removeServer(serverName());
    if (!_server->listen(serverName())) {
        delete _server;
        _server = nullptr;
        return false;
    }

    writeInstanceMetadata();
    connect(_server, &QLocalServer::newConnection, this, &BackgroundInstance::handleConnection);
    return true;
}

void BackgroundInstance::stopServer() {
    qInfo() << "[Shutdown] BackgroundInstance::stopServer begin"
            << "hasServer" << (_server != nullptr);
    removeInstanceMetadata();

    if (!_server) {
        qInfo() << "[Shutdown] BackgroundInstance::stopServer end: no server";
        return;
    }

    _server->close();
    QLocalServer::removeServer(serverName());
    _server->deleteLater();
    _server = nullptr;
    qInfo() << "[Shutdown] BackgroundInstance::stopServer end";
}

void BackgroundInstance::handleConnection() {
    if (!_server || !_controller) {
        return;
    }

    QLocalSocket *socket = _server->nextPendingConnection();
    if (!socket) {
        return;
    }

    connect(socket, &QLocalSocket::readyRead, socket, [this, socket] () {
        const QByteArray line = socket->readLine();
        socket->disconnectFromServer();

        const QString path = QString::fromUtf8(line.trimmed());
        if (path.isEmpty()) {
            QTimer::singleShot(0, _controller, &ViewerController::activateFromExternal);
        }
        else {
            QTimer::singleShot(0, _controller, [this, path] () {
                _controller->openExternalFile(path);
            });
        }
        socket->deleteLater();
    });
}
