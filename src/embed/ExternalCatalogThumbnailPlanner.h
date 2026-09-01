#pragma once

#include "ExternalCatalogModel.h"

namespace ZoinGallery {

class ExternalCatalogThumbnailPlanner final {
public:
    ExternalCatalogThumbnailPlanner(
        ExternalCatalogModel &model,
        const QList<ImageDecodeRequest> &requests);

    void run();

private:
    using Entry = ExternalCatalogModel::Entry;

    bool resolveRequest(ImageDecodeRequest &request, int &row,
                        Entry *&entry, ImageFile *&item);
    void serviceProbeBarrier(int row, const Entry &entry,
                             const ImageDecodeRequest &request);
    void configureRequest(ImageDecodeRequest &request,
                          const Entry &entry) const;
    void clearSupersededTarget(const Entry &entry,
                               ImageFile *item,
                               const ImageDecodeRequest &request);
    void traceAdmission(const ImageDecodeRequest &request, int row,
                        const QString &outcome,
                        const QString &providerId = QString()) const;
    void admitRequest(ImageDecodeRequest request);
    void submit();

    ExternalCatalogModel &m_model;
    const QList<ImageDecodeRequest> &m_requests;
    QList<ImageDecodeRequest> m_submitted;
};

} // namespace ZoinGallery
