#ifndef ZOINGALLERY_MEDIATIMINGTRACE_H
#define ZOINGALLERY_MEDIATIMINGTRACE_H

#include <ZoinGallery/ImageSourceProvider.h>

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <QVariantMap>

#include <chrono>
#include <ctime>
#include <utility>

namespace ZoinGallery::MediaTimingTrace {

inline bool enabled()
{
    // Timing mode is selected before startup. Cache the gate so production
    // decode paths pay only one environment lookup for the whole process.
    static const bool traceEnabled =
        qEnvironmentVariableIsSet("F4_MEDIA_TIMING_TRACE");
    return traceEnabled;
}

inline qint64 monotonicNanoseconds()
{
#if defined(Q_OS_DARWIN) || defined(Q_OS_LINUX)
    // Go uses this same kernel clock, making events directly comparable across
    // the launcher and Qt host without a wall-clock calibration handshake.
    timespec timestamp{};
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &timestamp) == 0) {
        return static_cast<qint64>(timestamp.tv_sec) * 1000000000LL
            + static_cast<qint64>(timestamp.tv_nsec);
    }
#endif
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

inline QVariantMap sourceFields(const ImageSourceDescriptor &source)
{
    return {
        {QStringLiteral("resourceId"), source.resourceId},
        {QStringLiteral("sourceKey"), source.sourceKey},
        {QStringLiteral("contentVersion"), source.contentVersion},
        {QStringLiteral("displayName"), source.displayName},
        {QStringLiteral("mimeType"), source.mimeType},
        {QStringLiteral("storageClass"), source.storageClass},
        {QStringLiteral("accessProfile"), source.accessProfile},
        {QStringLiteral("sourceBytes"), source.size},
    };
}

inline QVariantMap mergedFields(QVariantMap base, const QVariantMap &extra)
{
    for (auto it = extra.cbegin(); it != extra.cend(); ++it) {
        base.insert(it.key(), it.value());
    }
    return base;
}

inline void eventAt(const QString &name, qint64 monotonicNs,
                    QVariantMap fields = {})
{
    if (!enabled()) {
        return;
    }
    QJsonObject object = QJsonObject::fromVariantMap(fields);
    object.insert(QStringLiteral("schema"), QStringLiteral("f4.media.v1"));
    object.insert(QStringLiteral("event"), name);
    object.insert(QStringLiteral("monotonicNs"), monotonicNs);
    object.insert(QStringLiteral("pid"),
                  QCoreApplication::applicationPid());
    QString threadName = QThread::currentThread()->objectName();
    if (threadName.isEmpty()) {
        const QCoreApplication *application = QCoreApplication::instance();
        threadName = application && QThread::currentThread() ==
                application->thread()
            ? QStringLiteral("qt-main") : QStringLiteral("unnamed");
    }
    object.insert(QStringLiteral("thread"), threadName);
    const QByteArray json = QJsonDocument(object).toJson(
        QJsonDocument::Compact);
    // Navigation profiling also needs the library-internal catalog phases.
    // Keep this opt-in and share the host's existing output file so a live
    // cross-process trace does not depend on a console sink being present.
    static const QString outputPath = qEnvironmentVariable(
        "F4_NAV_BENCHMARK_QT_OUTPUT");
    if (outputPath.isEmpty()) {
        qInfo().noquote() << "F4_MEDIA_TIMING_TRACE" << json;
    }
    else {
        static QMutex outputMutex;
        static QFile *output = []() -> QFile * {
            auto *file = new QFile(qEnvironmentVariable(
                "F4_NAV_BENCHMARK_QT_OUTPUT"));
            if (!file->open(QIODevice::WriteOnly | QIODevice::Append)) {
                delete file;
                return nullptr;
            }
            if (QCoreApplication *application =
                    QCoreApplication::instance()) {
                QObject::connect(
                    application, &QCoreApplication::aboutToQuit,
                    application, [file]() { file->flush(); },
                    Qt::DirectConnection);
            }
            return file;
        }();
        const QMutexLocker locker(&outputMutex);
        if (output) {
            output->write("F4_NAV_BENCHMARK_TRACE ");
            output->write(json);
            output->write("\n");
        }
        else {
            qInfo().noquote() << "F4_MEDIA_TIMING_TRACE" << json;
        }
    }
}

inline void event(const QString &name, QVariantMap fields = {})
{
    eventAt(name, monotonicNanoseconds(), std::move(fields));
}

class Span final
{
public:
    explicit Span(QString name, QVariantMap fields = {})
        : m_name(std::move(name))
        , m_fields(std::move(fields))
        , m_startedNs(enabled() ? monotonicNanoseconds() : 0)
    {
        if (m_startedNs != 0) {
            eventAt(m_name + QStringLiteral(".begin"), m_startedNs,
                    m_fields);
        }
    }

    ~Span()
    {
        finish();
    }

    Span(const Span &) = delete;
    Span &operator=(const Span &) = delete;

    void set(const QString &key, const QVariant &value)
    {
        if (m_startedNs != 0) {
            m_fields.insert(key, value);
        }
    }

    void finish(QVariantMap fields = {})
    {
        if (m_startedNs == 0 || m_finished) {
            return;
        }
        m_finished = true;
        const qint64 finishedNs = monotonicNanoseconds();
        fields = mergedFields(m_fields, fields);
        fields.insert(QStringLiteral("durationNs"),
                      finishedNs - m_startedNs);
        eventAt(m_name + QStringLiteral(".end"), finishedNs,
                std::move(fields));
    }

private:
    QString m_name;
    QVariantMap m_fields;
    qint64 m_startedNs = 0;
    bool m_finished = false;
};

} // namespace ZoinGallery::MediaTimingTrace

#endif // ZOINGALLERY_MEDIATIMINGTRACE_H
