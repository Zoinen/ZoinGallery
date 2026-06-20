#ifndef LAUNCHOPTIONS_H
#define LAUNCHOPTIONS_H

#include <QString>
#include <QStringList>

struct LaunchOptions {
    bool backgroundMode = false;
    QString filePath;
};

LaunchOptions parseLaunchOptions(const QStringList &arguments);

#endif // LAUNCHOPTIONS_H
