#ifndef DUMMYQWK_H
#define DUMMYQWK_H

#include <QQmlEngine>

static constexpr const char kModuleUri[] = "QWindowKit";

class DummyQWK : public QObject {
    Q_OBJECT

public:
    explicit DummyQWK(QObject *parent = nullptr) : QObject(parent) {}

    enum SystemButton {
        Unknown,
        WindowIcon,
        Help,
        Minimize,
        Maximize,
        Close,
    };
    Q_ENUM(SystemButton)

    Q_INVOKABLE void setup(QObject *) {}
    Q_INVOKABLE void setWindowAttribute(QString, bool) {}
    Q_INVOKABLE void setSystemButton(SystemButton button, QObject *item) {}
    Q_INVOKABLE void setTitleBar(QObject *item) {}
    Q_INVOKABLE void setHitTestVisible(const QObject *item, bool visible = true) {}
    Q_INVOKABLE void showSystemMenu(const QPoint &pos) {}

    static void registerTypes(QQmlEngine *engine) {
        Q_UNUSED(engine);

        static bool once = false;
        if (once) {
            return;
        }
        once = true;

        qmlRegisterType<DummyQWK>(kModuleUri, 1, 0, "WindowAgent");
        qmlRegisterModule(kModuleUri, 1, 0);
    }
};

#endif // DUMMYQWK_H
