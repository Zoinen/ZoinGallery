#pragma once

#include <QSharedPointer>
#include <QTemporaryDir>

class QThreadPool;

namespace ZoinGallery::Internal {

// An explicit executor must outlive every directory handle and queued cleanup.
QSharedPointer<QTemporaryDir> createVectorSourceDirectory(QThreadPool *executor = nullptr);

}
