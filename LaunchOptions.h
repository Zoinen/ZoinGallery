#ifndef LAUNCHOPTIONS_H
#define LAUNCHOPTIONS_H

#include <QString>
#include <QStringList>

struct LaunchOptions {
    bool separateInstance = false;
    QString filePath;
};

QString normalizePathArgument(const QString &path);
QString normalizePathArgumentWithoutFileAccess(const QString &path);
LaunchOptions parseLaunchOptions(const QStringList &arguments);

#endif // LAUNCHOPTIONS_H
