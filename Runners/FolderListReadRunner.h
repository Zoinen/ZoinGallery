#ifndef FOLDERLISTREADRUNNER_H
#define FOLDERLISTREADRUNNER_H

#include "DecodeManager.h"

class FolderListReadRunner : public Runner {
    Q_OBJECT

public:
    FolderListReadRunner(const QString &path, int totalImages,
                         bool storeInCache, quint64 requestGeneration);

    RunnerType type() override { return RunnerType::FolderListRead; }
    void run() override;

signals:
    void folderListReady(const QString &path, const QList<FileInfo> &subfiles,
                         quint64 requestGeneration);
    void folderListFailed(const QString &path, const QString &errorText,
                          quint64 requestGeneration);

private:
    friend class DecodeManager;

    QString _path;
    int _totalImages;
    bool _storeInCache;
    quint64 _cacheGeneration;
    quint64 _requestGeneration;
};

#endif // FOLDERLISTREADRUNNER_H
