#pragma once

#include "DecodeManager.h"
#include "ExternalCatalogModel.h"

namespace ZoinGallery {

class ExternalCatalogMetadataPlanner final {
public:
    explicit ExternalCatalogMetadataPlanner(ExternalCatalogModel &model);

    void run();

private:
    using Request = DecodeManager::VersionedImageInfoRequest;

    bool rowNeedsMetadata(int row) const;
    bool hasEligibleRow(const QList<int> &rows) const;
    bool prepareCapacity();
    void appendRow(int row, QList<Request> &requests,
                   bool panelThumbnailRequest);
    void drainRows(QList<int> &rows, QList<Request> &requests,
                   bool panelThumbnailRequest);
    void drainCurrentViewer(QList<int> &rows, QList<Request> &requests);
    bool serviceProbeBarrier();
    void collectCatalogRows();
    void submitRequests();

    ExternalCatalogModel &m_model;
    qsizetype m_available = 0;
    QList<Request> m_highRequests;
    QList<Request> m_backgroundRequests;
};

} // namespace ZoinGallery
