#include "LaunchOptions.h"
#include "PathArgument.h"

LaunchOptions parseLaunchOptions(const QStringList &arguments) {
    LaunchOptions options;
    for (int i = 1; i < arguments.size(); ++i) {
        const QString &arg = arguments[i];
        if (arg == QStringLiteral("--separate-instance")) {
            options.separateInstance = true;
        }
        else if (options.filePath.isEmpty() && !arg.startsWith(QLatin1Char('-'))) {
            options.filePath = normalizePathArgument(arg);
        }
    }
    return options;
}
