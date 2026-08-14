#ifndef STORAGELOCATIONS_H
#define STORAGELOCATIONS_H

#include <QString>

namespace ZoinGallery::StorageLocations {

// Storage is process scoped. A host must configure it before constructing any
// model that can touch the persistent caches. Repeated configuration with the
// same namespace is harmless; a different namespace is rejected so an already
// loaded database can never silently switch backing files.
bool configure(const QString &storageNamespace);
QString storageNamespace();

QString cacheRoot();
QString dataRoot();

// Kept public to the Core implementation/tests so storage isolation can be
// verified without mutating the process-wide namespace.
QString cacheRootForNamespace(const QString &storageNamespace);
QString dataRootForNamespace(const QString &storageNamespace);

} // namespace ZoinGallery::StorageLocations

#endif // STORAGELOCATIONS_H
