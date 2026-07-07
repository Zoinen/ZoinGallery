#include <QByteArray>
#include <QColorSpace>
#include <QGuiApplication>
#include <QList>
#include <QScreen>
#include <QString>

#if defined(Q_OS_MACOS)
#import <AppKit/AppKit.h>

namespace
{
NSScreen *cocoaScreenForQtScreen(QScreen *qtScreen)
{
    if (!qtScreen) {
        qtScreen = QGuiApplication::primaryScreen();
    }

    if (qtScreen) {
        const QString qtScreenName = qtScreen->name();
        if (!qtScreenName.isEmpty()) {
            for (NSScreen *screen in [NSScreen screens]) {
                if (@available(macOS 10.15, *)) {
                    const QString cocoaScreenName = QString::fromUtf8([[screen localizedName] UTF8String]);
                    if (cocoaScreenName == qtScreenName) {
                        return screen;
                    }
                }
            }
        }

        const QList<QScreen *> qtScreens = QGuiApplication::screens();
        const int screenIndex = qtScreens.indexOf(qtScreen);
        NSArray<NSScreen *> *cocoaScreens = [NSScreen screens];
        if (screenIndex >= 0 && screenIndex < static_cast<int>([cocoaScreens count])) {
            return [cocoaScreens objectAtIndex:screenIndex];
        }
    }

    return [NSScreen mainScreen];
}
}

QColorSpace macDisplayColorSpaceForScreen(QScreen *screen)
{
    NSScreen *cocoaScreen = cocoaScreenForQtScreen(screen);
    if (!cocoaScreen) {
        return {};
    }

    NSColorSpace *colorSpace = [cocoaScreen colorSpace];
    if (!colorSpace) {
        return {};
    }

    NSData *iccProfileData = [colorSpace ICCProfileData];
    if (!iccProfileData || [iccProfileData length] == 0) {
        return {};
    }

    const QByteArray iccProfile(reinterpret_cast<const char *>([iccProfileData bytes]),
                                static_cast<qsizetype>([iccProfileData length]));
    return QColorSpace::fromIccProfile(iccProfile);
}
#endif
