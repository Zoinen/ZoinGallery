#pragma once

#include <QSize>
#include <QtGui/private/qtguiglobal_p.h>

namespace ZoinGallery {

inline bool canDecodeThumbnailAsRgb32(QSize source, QSize bounds)
{
    // Qt uses the same area filter for RGB888 and RGB32 above this
    // threshold. At smaller sizes RGB888 may use the painter's bilinear path.
    return QT_CONFIG(qtgui_threadpool) && !bounds.isEmpty()
        && (source.width() > bounds.width() || source.height() > bounds.height())
        && qint64(source.width()) * source.height() >= (1 << 20);
}

}
