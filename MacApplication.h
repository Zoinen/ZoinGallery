#ifndef MACAPPLICATION_H
#define MACAPPLICATION_H

#include <QtGlobal>
#include <QColorSpace>

class QPoint;
class QMenu;
class QScreen;

#if defined(Q_OS_MACOS)
void setMacApplicationDockVisible(bool visible);
void applyMacApplicationDockIconPolicy(bool windowVisible);
bool macMouseButtonsPressed();
void showMacTrayMenu(QMenu *menu, const QPoint &topLeft);
QColorSpace macColorSpaceForScreen(QScreen *screen);
#else
inline void setMacApplicationDockVisible(bool) {}
inline void applyMacApplicationDockIconPolicy(bool) {}
inline bool macMouseButtonsPressed() { return false; }
inline QColorSpace macColorSpaceForScreen(QScreen *) { return {}; }
#endif

#endif // MACAPPLICATION_H
