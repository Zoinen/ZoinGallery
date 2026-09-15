#pragma once

#include <ZoinGallery/ImageSourceProvider.h>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace ZoinGallery {

struct DirectorySourceDescriptor {
    QString resourceId;
    QString sourceKey;
    QString version;
    bool isValid() const { return !resourceId.isEmpty() && !sourceKey.isEmpty(); }
    bool operator==(const DirectorySourceDescriptor &) const = default;
};

class DirectoryPreviewLease {
public:
    virtual ~DirectoryPreviewLease() = default;
};

struct DirectoryPreviewListing {
    QStringList names;
    QSharedPointer<DirectoryPreviewLease> lease;
    QString error;
};

struct DirectoryPreviewResult {
    // Ordinary external-catalog rows with name and image source descriptor.
    QVariantList entries;
    QSharedPointer<DirectoryPreviewLease> lease;
    QString error;
};

// Called on a dedicated bounded directory executor, never an image decode
// worker or GUI thread. Lease destruction must be safe on any thread.
class DirectoryPreviewProvider {
public:
    virtual ~DirectoryPreviewProvider() = default;
    virtual DirectoryPreviewListing enumerate(
        const DirectorySourceDescriptor &source,
        const QSharedPointer<ImageSourceCancellation> &cancel) = 0;
    virtual DirectoryPreviewResult resolve(
        const DirectorySourceDescriptor &source, const DirectoryPreviewListing &listing,
        const QStringList &names, const QSharedPointer<ImageSourceCancellation> &cancel) = 0;
};

}
