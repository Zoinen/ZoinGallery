#include "PathArgument.h"

#include <QFileInfo>

namespace
{
QString trimWrappingQuotes(QString path)
{
    path = path.trimmed();
    if (path.startsWith(QLatin1Char('"'))) {
        path = path.mid(1);
    }
    if (path.endsWith(QLatin1Char('"'))) {
        path.chop(1);
    }
    return path.trimmed();
}

QString unescapeBackslashEscapes(const QString &path)
{
    QString result;
    result.reserve(path.size());
    bool escaped = false;
    for (const QChar character : path) {
        if (escaped) {
            result.append(character);
            escaped = false;
        }
        else if (character == QLatin1Char('\\')) {
            escaped = true;
        }
        else {
            result.append(character);
        }
    }
    if (escaped) {
        result.append(QLatin1Char('\\'));
    }
    return result;
}
}

QString normalizePathArgument(const QString &path)
{
    const QString trimmedPath = trimWrappingQuotes(path);
    if (QFileInfo::exists(trimmedPath)) {
        return trimmedPath;
    }

    const QString unescapedPath = unescapeBackslashEscapes(trimmedPath);
    if (unescapedPath != trimmedPath && QFileInfo::exists(unescapedPath)) {
        return unescapedPath;
    }

    return trimmedPath;
}

QString normalizePathArgumentWithoutFileAccess(const QString &path)
{
    return trimWrappingQuotes(path);
}
