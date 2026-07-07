#include "MacApplication.h"

#include <QAction>
#include <QByteArray>
#include <QDebug>
#include <QGuiApplication>
#include <QMenu>
#include <QMetaObject>
#include <QPoint>
#include <QRect>
#include <QScreen>
#include <QSettings>
#include <QString>
#include <QtGlobal>

#if defined(Q_OS_MACOS)
#import <AppKit/AppKit.h>

@interface ZGTrayMenuActionTarget : NSObject {
    QAction *_action;
}
- (instancetype)initWithAction:(QAction *)action;
- (void)performAction:(id)sender;
@end

@implementation ZGTrayMenuActionTarget
- (instancetype)initWithAction:(QAction *)action
{
    self = [super init];
    if (self) {
        _action = action;
    }
    return self;
}

- (void)performAction:(id)sender
{
    Q_UNUSED(sender);
    qInfo() << "[Shutdown] Native tray menu action selected"
            << (_action ? _action->text() : QString())
            << "enabled" << (_action ? _action->isEnabled() : false);
    if (_action && _action->isEnabled()) {
        QMetaObject::invokeMethod(_action, "trigger", Qt::QueuedConnection);
    }
}
@end

namespace
{
NSString *toNSString(const QString &value)
{
    return [NSString stringWithUTF8String:value.toUtf8().constData()];
}

QString nativeMenuText(QAction *action)
{
    QString text = action ? action->text() : QString();
    text.remove(QLatin1Char('&'));
    return text;
}

NSMenu *createNativeMenu(QMenu *qtMenu, NSMutableArray *targets)
{
    NSMenu *nativeMenu = [[NSMenu alloc] initWithTitle:qtMenu ? toNSString(qtMenu->title()) : @""];
    if (!qtMenu) {
        return nativeMenu;
    }

    for (QAction *action : qtMenu->actions()) {
        if (!action || !action->isVisible()) {
            continue;
        }

        if (action->isSeparator()) {
            [nativeMenu addItem:[NSMenuItem separatorItem]];
            continue;
        }

        NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:toNSString(nativeMenuText(action))
                                                      action:nil
                                               keyEquivalent:@""];
        [item setEnabled:action->isEnabled()];

        if (QMenu *subMenu = action->menu()) {
            NSMenu *nativeSubMenu = createNativeMenu(subMenu, targets);
            [item setSubmenu:nativeSubMenu];
            [nativeSubMenu release];
        }
        else {
            ZGTrayMenuActionTarget *target = [[ZGTrayMenuActionTarget alloc] initWithAction:action];
            [targets addObject:target];
            [item setTarget:target];
            [item setAction:@selector(performAction:)];
            [target release];

            if (action->isCheckable()) {
                [item setState:action->isChecked() ? NSControlStateValueOn : NSControlStateValueOff];
            }
        }

        [nativeMenu addItem:item];
        [item release];
    }

    return nativeMenu;
}

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

NSPoint qtScreenPointToCocoaPoint(const QPoint &qtPoint)
{
    QScreen *qtScreen = QGuiApplication::screenAt(qtPoint);
    if (!qtScreen) {
        qtScreen = QGuiApplication::primaryScreen();
    }

    NSScreen *cocoaScreen = nil;
    const NSPoint mouseLocation = [NSEvent mouseLocation];
    for (NSScreen *screen in [NSScreen screens]) {
        if (NSPointInRect(mouseLocation, [screen frame])) {
            cocoaScreen = screen;
            break;
        }
    }

    if (!cocoaScreen && qtScreen) {
        const QString qtScreenName = qtScreen->name();
        if (!qtScreenName.isEmpty()) {
            for (NSScreen *screen in [NSScreen screens]) {
                if (@available(macOS 10.15, *)) {
                    const QString cocoaScreenName = QString::fromUtf8([[screen localizedName] UTF8String]);
                    if (cocoaScreenName == qtScreenName) {
                        cocoaScreen = screen;
                        break;
                    }
                }
            }
        }
    }

    if (!cocoaScreen && qtScreen) {
        const QList<QScreen *> qtScreens = QGuiApplication::screens();
        const int screenIndex = qtScreens.indexOf(qtScreen);
        NSArray<NSScreen *> *cocoaScreens = [NSScreen screens];
        if (screenIndex >= 0 && screenIndex < static_cast<int>([cocoaScreens count])) {
            cocoaScreen = [cocoaScreens objectAtIndex:screenIndex];
        }
    }

    if (!cocoaScreen) {
        cocoaScreen = [NSScreen mainScreen];
    }
    if (!cocoaScreen) {
        return NSMakePoint(qtPoint.x(), qtPoint.y());
    }

    const QRect qtScreenGeometry = qtScreen ? qtScreen->geometry() : QRect();
    if (!qtScreenGeometry.isValid()) {
        return NSMakePoint(qtPoint.x(), qtPoint.y());
    }

    const NSRect cocoaFrame = [cocoaScreen frame];
    const qreal xWithinScreen = qtPoint.x() - qtScreenGeometry.x();
    const qreal yWithinScreen = qtPoint.y() - qtScreenGeometry.y();
    return NSMakePoint(cocoaFrame.origin.x + xWithinScreen,
                       cocoaFrame.origin.y + cocoaFrame.size.height - yWithinScreen);
}

QString dockIconMode()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("General"));
    const QString mode = settings.value(QStringLiteral("dockIconMode"), QStringLiteral("windowVisible")).toString();
    if (mode == QStringLiteral("always") ||
        mode == QStringLiteral("never") ||
        mode == QStringLiteral("windowVisible")) {
        return mode;
    }
    return QStringLiteral("windowVisible");
}
}

void setMacApplicationDockVisible(bool visible)
{
    [NSApp setActivationPolicy:visible ? NSApplicationActivationPolicyRegular
                                       : NSApplicationActivationPolicyAccessory];
}

void applyMacApplicationDockIconPolicy(bool windowVisible)
{
    const QString mode = dockIconMode();
    if (mode == QStringLiteral("always")) {
        setMacApplicationDockVisible(true);
    }
    else if (mode == QStringLiteral("never")) {
        setMacApplicationDockVisible(false);
    }
    else {
        setMacApplicationDockVisible(windowVisible);
    }
}

bool macMouseButtonsPressed()
{
    return [NSEvent pressedMouseButtons] != 0;
}

void showMacTrayMenu(QMenu *qtMenu, const QPoint &topLeft)
{
    if (!qtMenu) {
        return;
    }

    NSMutableArray *targets = [[NSMutableArray alloc] init];
    NSMenu *menu = createNativeMenu(qtMenu, targets);
    [menu popUpMenuPositioningItem:nil
                        atLocation:qtScreenPointToCocoaPoint(topLeft)
                            inView:nil];

    [menu release];
    [targets release];
}

QColorSpace macColorSpaceForScreen(QScreen *screen)
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
