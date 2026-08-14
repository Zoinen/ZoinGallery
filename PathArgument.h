#ifndef PATHARGUMENT_H
#define PATHARGUMENT_H

#include <QString>

// Normalize paths supplied by a host or command line.  The file-access-free
// variant is used by external catalog sessions, where the host remains the
// authority for whether a path can be materialized.
QString normalizePathArgument(const QString &path);
QString normalizePathArgumentWithoutFileAccess(const QString &path);

#endif // PATHARGUMENT_H
