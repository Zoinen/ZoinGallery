#pragma once

#include <ZoinGallery/DirectoryPreviewProvider.h>
#include <QHash>
#include <QObject>
#include <QThreadPool>

namespace ZoinGallery {
class ExternalCatalogModel;
class ExternalDirectoryPreviews final : public QObject {
public:
    ExternalDirectoryPreviews(ExternalCatalogModel *catalog, QSharedPointer<DirectoryPreviewProvider> provider, QSharedPointer<QThreadPool> pool);
    ~ExternalDirectoryPreviews() override;
    void demand(const QList<int> &rows);
    void synchronize();
    bool available(const QString &id) const;
    ExternalCatalogModel *model(const QString &id) const;
    void clear();
    void setCacheMode(int mode);
private:
    struct Record {
        DirectorySourceDescriptor source;
        ExternalCatalogModel *model = nullptr;
        QSharedPointer<DirectoryPreviewLease> lease;
        QSharedPointer<ImageSourceCancellation> cancel;
        quint64 serial = 0, touched = 0;
        bool demanded = false, settled = false, available = false;
    };
    void request(const QString &id, const QSharedPointer<Record> &record);
    void publish(const QString &id);
    void retire(const QString &id);
    ExternalCatalogModel *_catalog;
    QSharedPointer<DirectoryPreviewProvider> _provider;
    QSharedPointer<QThreadPool> _pool;
    QHash<QString, QSharedPointer<Record>> _records;
    QList<int> _demand;
    quint64 _clock = 0;
    bool _scheduled = false;
    bool _clearing = false;
    bool _retryScheduled = false;
    int _cacheMode = 1;
};
}
