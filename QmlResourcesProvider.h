#ifndef QMLRESOURCESPROVIDER_H
#define QMLRESOURCESPROVIDER_H

#include <QQuickImageProvider>

class QmlResourcesProvider : public QQuickImageProvider {
public:
    QmlResourcesProvider(const QString &prefix);

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    QString _prefix;
};

#endif // QMLRESOURCESPROVIDER_H
