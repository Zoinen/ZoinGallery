#ifndef IMAGEMODEL_H
#define IMAGEMODEL_H

#include <QSortFilterProxyModel>

class ImageModel : public QSortFilterProxyModel {
    Q_OBJECT
public:
    ImageModel(QAbstractItemModel *sourceModel);

    Q_INVOKABLE int mapToSourceRow(int proxyRow) const;
    Q_INVOKABLE int mapFromSourceRow(int sourceRow) const;
    Q_INVOKABLE int mapToViewRow(int imageRow) const;
    Q_INVOKABLE int mapFromViewRow(int viewRow) const;

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
};

#endif // IMAGEMODEL_H
