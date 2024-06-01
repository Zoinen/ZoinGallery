#ifndef PERSISTENTFOLDERCACHERUNNERS_H
#define PERSISTENTFOLDERCACHERUNNERS_H

#include "DecodeManager.h"

struct CachedFolderInfo {
    QString path;
    QStringList subImages;
};


class PersistentFolderCacheAddRunner : public Runner {
    Q_OBJECT

public:
    RunnerType type() override { return RunnerType::PersistentFolderCacheAdd; }
    void addToCache(const QString &path, const QStringList &subImages);
};


class PersistentFolderCacheRetrieveRunner : public Runner {
    Q_OBJECT

public:
    RunnerType type() override { return RunnerType::PersistentFolderCacheRetrieve; }
    void requestFromCache(const QString &path, const QDateTime &lastModified);

signals:
    void cachedSubImagesAvailable(const QString &path, const QStringList &subImages);
};


#endif // PERSISTENTFOLDERCACHERUNNERS_H
