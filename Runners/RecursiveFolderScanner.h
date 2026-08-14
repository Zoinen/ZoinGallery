#ifndef RECURSIVEFOLDERSCANNER_H
#define RECURSIVEFOLDERSCANNER_H

#include "DecodeManager.h"

class RecursiveFolderScanner : public Runner {
    Q_OBJECT

public:
    RecursiveFolderScanner(const QString &root,
                           const QString &requestNamespace = QString());

    RunnerType type() override { return RunnerType::RecursiveFolderScanner; }
    void run() override;
    QString requestNamespace() const override { return _requestNamespace; }

signals:
    void scanImages(const QStringList &images);

private:
    friend class DecodeManager;

    QString _root;
    QString _requestNamespace;
    QList<QStringList> _foldersToDecode;
};

#endif // RECURSIVEFOLDERSCANNER_H
