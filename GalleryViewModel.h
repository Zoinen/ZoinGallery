#ifndef GALLERYVIEWMODEL_H
#define GALLERYVIEWMODEL_H

#include <QCollator>
#include <QSortFilterProxyModel>
#include <QVariantList>

#include "FileListModel.h"

class GalleryViewModel : public QSortFilterProxyModel, public ThumbnailsRequestInterface {
    Q_OBJECT
    Q_PROPERTY(bool selectedOnly READ selectedOnly WRITE setSelectedOnly NOTIFY selectedOnlyChanged)
    Q_PROPERTY(int sortMode READ sortMode WRITE setSortMode NOTIFY sortModeChanged)
    Q_PROPERTY(QString sortModeLabel READ sortModeLabel NOTIFY sortModeChanged)
    Q_PROPERTY(QVariantList sortModeOptions READ sortModeOptions CONSTANT)

public:
    enum SortMode {
        NameAscending = 0,
        NameDescending,
        ModifiedAscending,
        ModifiedDescending,
        ExtensionAscending,
        ExtensionDescending,
        SizeAscending,
        SizeDescending
    };
    Q_ENUM(SortMode)

    explicit GalleryViewModel(FileListModel *sourceModel, QObject *parent = nullptr);

    void setSourceModel(QAbstractItemModel *sourceModel) override;

    bool selectedOnly() const;
    void setSelectedOnly(bool selectedOnly);

    int sortMode() const;
    void setSortMode(int sortMode);
    QString sortModeLabel() const;
    QVariantList sortModeOptions() const;

    Q_INVOKABLE int mapToSourceRow(int viewRow) const;
    Q_INVOKABLE int mapFromSourceRow(int sourceRow) const;
    Q_INVOKABLE QVariantList mapToSourceRows(const QVariantList &viewRows) const;
    Q_INVOKABLE QVariantList viewerPrefetchSourceRows(
        int currentViewRow, int imageCount = 16) const;
    Q_INVOKABLE QVariantList sourceRowsForViewRange(int anchorViewRow, int targetViewRow, bool includeTarget) const;
    Q_INVOKABLE int nearestVisibleRow(int sourceRow) const;

    ImageFile *rootItem() const override;
    void decodeImages(const QList<ImageDecodeRequest> &requests) override;
    void cancelAllRunners() override;
    void cancelAllDecodeRunners() override;
    bool preserveViewStateOnReset() const override;

signals:
    void selectedOnlyChanged();
    void sortModeChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
    FileListModel *fileListModel() const;
    static SortMode normalizeSortMode(int sortMode);
    static bool isReverse(SortMode sortMode);
    static QString labelForSortMode(SortMode sortMode);
    void refreshRowsFilter();
    int compareItems(const ImageFile *leftItem, const ImageFile *rightItem) const;
    int compareNatural(const QString &left, const QString &right) const;
    int compareDateTime(const QDateTime &left, const QDateTime &right) const;
    int compareInteger(qint64 left, qint64 right) const;

    bool _selectedOnly = false;
    SortMode _sortMode = NameAscending;
    QCollator _collator;
};

#endif // GALLERYVIEWMODEL_H
