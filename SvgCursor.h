#ifndef SVGCURSOR_H
#define SVGCURSOR_H

#include <QString>

class SvgCursor {
public:
    static void setOverrideCursor(const QString &path = QString(), qreal dp = 1, qreal rotation = 0);

private:
    static bool _cursorOverridden;
};

#endif // SVGCURSOR_H
