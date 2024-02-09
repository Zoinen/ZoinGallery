#ifndef IMAGEMODEL_H
#define IMAGEMODEL_H

#include <QSortFilterProxyModel>

class FileListModel;

class ImageModel : public QSortFilterProxyModel {
    Q_OBJECT
public:
    ImageModel(FileListModel *sourceModel);

    Q_INVOKABLE int mapToSourceRow(int proxyRow) const;
    Q_INVOKABLE int mapFromSourceRow(int sourceRow) const;

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    FileListModel *_sourceModel;
};

#endif // IMAGEMODEL_H
