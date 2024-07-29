#ifndef RECURSIVEFOLDERSCANNER_H
#define RECURSIVEFOLDERSCANNER_H

#include "DecodeManager.h"

class RecursiveFolderScanner : public Runner {
    Q_OBJECT

public:
    RecursiveFolderScanner(const QString &root);

    RunnerType type() override { return RunnerType::RecursiveFolderScanner; }
    void run() override;

signals:
    void scanImages(const QStringList &images);

private:
    friend class DecodeManager;

    QString _root;
    QList<QStringList> _foldersToDecode;
};

#endif // RECURSIVEFOLDERSCANNER_H
