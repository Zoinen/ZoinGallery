#include <QApplication>
#include <QQmlApplicationEngine>
#include <QElapsedTimer>
#include <QQuickStyle>
#include <QSettings>
#include <QDebug>
#include <QImageReader>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTimer>

#if defined(Q_OS_MACOS)
#include <QAction>
#include <QMenu>
#include <QMenuBar>
#endif

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

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <mutex>

#if defined(Q_OS_WIN)
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace
{
QDate currentLogDate;
int lastStdoutRedirectError = 0;
int lastStderrRedirectError = 0;
std::mutex messageLogMutex;

#if defined(Q_OS_WIN)
HANDLE messageLogHandle = INVALID_HANDLE_VALUE;
HANDLE stdoutLogHandle = INVALID_HANDLE_VALUE;
HANDLE stderrLogHandle = INVALID_HANDLE_VALUE;
#else
FILE *messageLogFile = nullptr;
#endif

QString dailyLogFilePath(const QDate &date)
{
    const QString logFileName = QStringLiteral("ZoinGallery-%1.log").arg(date.toString(QStringLiteral("yyyy-MM-dd")));
    const QString relativeLogDirectory = QStringLiteral("logs/%1/%2")
                                            .arg(date.toString(QStringLiteral("yyyy")),
                                                 date.toString(QStringLiteral("MM")));

    const QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    const QString basePath = appDataPath.isEmpty()
                                 ? QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("logs"))
                                 : appDataPath;

    QDir baseDirectory(basePath);
    if (baseDirectory.mkpath(relativeLogDirectory)) {
        return baseDirectory.filePath(QStringLiteral("%1/%2").arg(relativeLogDirectory, logFileName));
    }

    QDir fallbackDirectory(QCoreApplication::applicationDirPath());
    fallbackDirectory.mkpath(relativeLogDirectory);
    return fallbackDirectory.filePath(QStringLiteral("%1/%2").arg(relativeLogDirectory, logFileName));
}

bool redirectStandardStreams(const QString &logFilePath)
{
    fflush(stdout);
    fflush(stderr);

#if defined(Q_OS_WIN)
    const std::wstring nativePath = QDir::toNativeSeparators(logFilePath).toStdWString();
    auto openLogHandle = [&nativePath]() -> HANDLE {
        return CreateFileW(nativePath.c_str(),
                           FILE_APPEND_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr,
                           OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL,
                           nullptr);
    };

    HANDLE newStdoutHandle = openLogHandle();
    const bool stdoutOpened = newStdoutHandle != INVALID_HANDLE_VALUE && SetStdHandle(STD_OUTPUT_HANDLE, newStdoutHandle);
    lastStdoutRedirectError = stdoutOpened ? 0 : static_cast<int>(GetLastError());

    HANDLE newStderrHandle = openLogHandle();
    const bool stderrOpened = newStderrHandle != INVALID_HANDLE_VALUE && SetStdHandle(STD_ERROR_HANDLE, newStderrHandle);
    lastStderrRedirectError = stderrOpened ? 0 : static_cast<int>(GetLastError());

    if (stdoutOpened && stderrOpened) {
        std::lock_guard<std::mutex> lock(messageLogMutex);
        HANDLE oldStdoutHandle = stdoutLogHandle;
        HANDLE oldStderrHandle = stderrLogHandle;
        stdoutLogHandle = newStdoutHandle;
        stderrLogHandle = newStderrHandle;
        if (oldStdoutHandle != INVALID_HANDLE_VALUE) {
            CloseHandle(oldStdoutHandle);
        }
        if (oldStderrHandle != INVALID_HANDLE_VALUE) {
            CloseHandle(oldStderrHandle);
        }
    } else {
        if (newStdoutHandle != INVALID_HANDLE_VALUE) {
            CloseHandle(newStdoutHandle);
        }
        if (newStderrHandle != INVALID_HANDLE_VALUE) {
            CloseHandle(newStderrHandle);
        }
    }
#else
    const QByteArray nativePath = QFile::encodeName(logFilePath);
    FILE *stdoutFile = freopen(nativePath.constData(), "a", stdout);
    FILE *stderrFile = freopen(nativePath.constData(), "a", stderr);
    lastStdoutRedirectError = stdoutFile == nullptr ? errno : 0;
    lastStderrRedirectError = stderrFile == nullptr ? errno : 0;
    const bool stdoutOpened = stdoutFile != nullptr;
    const bool stderrOpened = stderrFile != nullptr;
#endif

#if !defined(Q_OS_WIN)
    if (stdoutOpened) {
        setvbuf(stdout, nullptr, _IOLBF, 0);
    }
    if (stderrOpened) {
        setvbuf(stderr, nullptr, _IONBF, 0);
    }
#endif

    return stdoutOpened && stderrOpened;
}

bool openMessageLogSink(const QString &logFilePath)
{
#if defined(Q_OS_WIN)
    const std::wstring nativePath = QDir::toNativeSeparators(logFilePath).toStdWString();
    HANDLE newHandle = CreateFileW(nativePath.c_str(),
                                   FILE_APPEND_DATA,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                   nullptr,
                                   OPEN_ALWAYS,
                                   FILE_ATTRIBUTE_NORMAL,
                                   nullptr);
    if (newHandle == INVALID_HANDLE_VALUE) {
        return false;
    }

    std::lock_guard<std::mutex> lock(messageLogMutex);
    HANDLE oldHandle = messageLogHandle;
    messageLogHandle = newHandle;
    if (oldHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(oldHandle);
    }
#else
    const QByteArray nativePath = QFile::encodeName(logFilePath);
    FILE *newFile = fopen(nativePath.constData(), "a");
    if (newFile == nullptr) {
        return false;
    }
    setvbuf(newFile, nullptr, _IONBF, 0);

    std::lock_guard<std::mutex> lock(messageLogMutex);
    FILE *oldFile = messageLogFile;
    messageLogFile = newFile;
    if (oldFile != nullptr) {
        fclose(oldFile);
    }
#endif

    return true;
}

void writeMessageLogLine(const QByteArray &line)
{
    std::lock_guard<std::mutex> lock(messageLogMutex);

#if defined(Q_OS_WIN)
    if (messageLogHandle != INVALID_HANDLE_VALUE) {
        DWORD bytesWritten = 0;
        WriteFile(messageLogHandle, line.constData(), static_cast<DWORD>(line.size()), &bytesWritten, nullptr);
        WriteFile(messageLogHandle, "\n", 1, &bytesWritten, nullptr);
    }
#else
    if (messageLogFile != nullptr) {
        fprintf(messageLogFile, "%s\n", line.constData());
        fflush(messageLogFile);
    }
#endif
}

void logMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    const QByteArray formattedMessage = qFormatLogMessage(type, context, message).toUtf8();
    writeMessageLogLine(formattedMessage);

    if (type == QtFatalMsg) {
        std::abort();
    }
}

void updateDailyLogFile()
{
    const QDate today = QDate::currentDate();
    if (currentLogDate == today) {
        return;
    }

    const QString logFilePath = dailyLogFilePath(today);
    const bool messageLogOpened = openMessageLogSink(logFilePath);
    if (messageLogOpened) {
        currentLogDate = today;
        writeMessageLogLine(QStringLiteral("Logging to %1").arg(QDir::toNativeSeparators(logFilePath)).toUtf8());
    }
    const bool streamsRedirected = redirectStandardStreams(logFilePath);
    if (messageLogOpened && !streamsRedirected) {
        qWarning() << "Failed to redirect stdout/stderr to the daily log file"
                   << "stdout error" << lastStdoutRedirectError
                   << "stderr error" << lastStderrRedirectError;
    }
}

void installDailyLogRedirection(QObject *parent)
{
    qInstallMessageHandler(logMessageHandler);
    updateDailyLogFile();

    auto *logRolloverTimer = new QTimer(parent);
    QObject::connect(logRolloverTimer, &QTimer::timeout, []() {
        updateDailyLogFile();
    });
    logRolloverTimer->start(60 * 1000);
}

#if defined(Q_OS_MACOS)
void installMacSettingsMenu(MainWindow *mainWindow)
{
    auto *menuBar = new QMenuBar();
    auto *settingsMenu = menuBar->addMenu(QStringLiteral("Settings"));
    QAction *settingsAction = settingsMenu->addAction(QStringLiteral("Settings..."));
    QObject::connect(settingsAction, &QAction::triggered, mainWindow, [mainWindow]() {
        emit mainWindow->openSettingsRequested();
    });
}
#endif
}

int main(int argc, char *argv[])
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif

#if defined(Q_OS_WIN)
    // qputenv("QT_QPA_PLATFORM", "windows:darkmode=2");
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
    installDailyLogRedirection(&app);

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

    if (const QList<QObject *> roots = engine.rootObjects(); !roots.isEmpty()) {
        if (auto *mainWindow = qobject_cast<MainWindow *>(roots.first())) {
#if defined(Q_OS_MACOS)
            installMacSettingsMenu(mainWindow);
#endif
        }
    }

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
