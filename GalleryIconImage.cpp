#include "GalleryIconImage.h"
#include <QScopedValueRollback>

GalleryIconImage::GalleryIconImage(QQuickItem *parent)
    : QQuickIconImage(parent) {}

void GalleryIconImage::setSource(const QUrl &source) {
    _requestedSource = source;
    QQuickIconImage::setSource(source);
}

void GalleryIconImage::load() {
    if (_loading) return;
    if (source().isEmpty() && name().isEmpty()) {
        // Identity resets must clear outgoing pixels immediately.
        _loadPending = false;
        QScopedValueRollback<bool> loading(_loading, true);
        loadEmptyUrl();
        return;
    }
    _loadPending = true;
    polish();
}

void GalleryIconImage::updatePolish() {
    QQuickIconImage::updatePolish();
    if (!_loadPending) return;
    _loadPending = false;
    QScopedValueRollback<bool> loading(_loading, true);
    QQuickIconImage::load();
}
