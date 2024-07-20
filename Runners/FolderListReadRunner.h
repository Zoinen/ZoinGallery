#ifndef FOLDERLISTREADRUNNER_H
#define FOLDERLISTREADRUNNER_H

#include "DecodeManager.h"

class FolderListReadRunner : public Runner {
    Q_OBJECT

public:
    FolderListReadRunner(const QString &path, int totalImages);

    RunnerType type() override { return RunnerType::FolderListRead; }
    void run() override;

signals:
    void folderListReady(const QString &path, const QList<FileInfo> &subfiles);

private:
    friend class DecodeManager;

    QString _path;
    int _totalImages;
};

#endif // FOLDERLISTREADRUNNER_H
