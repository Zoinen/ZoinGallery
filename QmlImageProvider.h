#ifndef QMLIMAGEPROVIDER_H
#define QMLIMAGEPROVIDER_H

#include "ProviderImageStore.h"

#include <QQuickImageProvider>
#include <QSharedPointer>

class QmlImageProvider : public QQuickImageProvider {
public:
    QmlImageProvider(
        const QString &prefix,
        QSharedPointer<ProviderImageStore> providerImageStore);

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    QString _prefix;
    QSharedPointer<ProviderImageStore> _providerImageStore;
};

#endif // QMLIMAGEPROVIDER_H
