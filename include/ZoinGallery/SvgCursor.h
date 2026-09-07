#pragma once

#include <QString>

class SvgCursor {
public:
    static void setOverrideCursor(const QString &path = QString(),
                                  qreal dp = 1, qreal rotation = 0);
    // Shared browser-style middle-button cursor used by panel and document
    // scrolling. Keeping path selection here prevents the two presenters
    // from drifting apart (and keeps the native cursor replacement reusable).
    static void setScrollingModeCursor(bool scrollingMode, int direction,
                                       qreal dp = 1);

private:
    static bool _cursorOverridden;
};
