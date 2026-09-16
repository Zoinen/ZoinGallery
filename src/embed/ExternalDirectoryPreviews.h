#pragma once

#include <ZoinGallery/DirectoryPreviewProvider.h>
#include <QHash>
#include <QSet>
#include <QObject>
#include <QThreadPool>
#include "DirectoryPreviewCache.h"
#include "DirectoryPreviewScheduler.h"

namespace ZoinGallery {
class ExternalCatalogModel;
class ExternalDirectoryPreviews final : public QObject {
public:
    ExternalDirectoryPreviews(ExternalCatalogModel *catalog, QSharedPointer<DirectoryPreviewProvider> provider,
        QSharedPointer<DirectoryPreviewCache> cache, QSharedPointer<DirectoryPreviewScheduler> scheduler);
    ~ExternalDirectoryPreviews() override;
    void demand(const QList<int> &rows);
    void synchronize();
    void invalidateSources(const QVariantList &entries, bool replacingCatalog);
    ExternalCatalogModel *model(const QString &id) const;
    void clear();
    void setCacheMode(int mode);
    bool usesCache() const { return _cacheMode != 0; }
private:
    struct Record {
        DirectorySourceDescriptor source;
        ExternalCatalogModel *model = nullptr;
        QSharedPointer<DirectoryPreviewLease> lease;
        QSharedPointer<ImageSourceCancellation> cancel;
        quint64 serial = 0, touched = 0;
        DirectoryPreviewState state = DirectoryPreviewState::Unknown;
        qint64 queuedNs = 0;
        bool demanded = false, settled = false;
    };
    void request(const QString &id, const QSharedPointer<Record> &record);
    void publish(const QString &id);
    void retire(const QString &id);
    void capture(const QSharedPointer<Record> &record);
    void restore(const QString &id, const QSharedPointer<Record> &record);
    void flushPublications();
    ExternalCatalogModel *_catalog;
    QSharedPointer<DirectoryPreviewProvider> _provider;
    QSharedPointer<DirectoryPreviewCache> _cache;
    QSharedPointer<DirectoryPreviewScheduler> _scheduler;
    QHash<QString, QSharedPointer<Record>> _records;
    QList<int> _demand;
    quint64 _clock = 0;
    bool _scheduled = false;
    bool _clearing = false;
    QSet<QString> _publications;
    int _cacheMode = 1;
};
}
