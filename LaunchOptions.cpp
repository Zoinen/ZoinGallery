#include "LaunchOptions.h"

LaunchOptions parseLaunchOptions(const QStringList &arguments) {
    LaunchOptions options;
    for (int i = 1; i < arguments.size(); ++i) {
        const QString &arg = arguments[i];
        if (arg == QStringLiteral("--background")) {
            options.backgroundMode = true;
        }
        else if (options.filePath.isEmpty() && !arg.startsWith(QLatin1Char('-'))) {
            options.filePath = arg;
            if (options.filePath.startsWith(QLatin1Char('"'))) {
                options.filePath = options.filePath.mid(1);
            }
            if (options.filePath.endsWith(QLatin1Char('"'))) {
                options.filePath.chop(1);
            }
        }
    }
    return options;
}
