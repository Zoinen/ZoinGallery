#ifndef GALLERYICONIMAGE_H
#define GALLERYICONIMAGE_H

#include <QtQuickControls2Impl/private/qquickiconimage_p.h>

// Commit icon raster requests after a row's source, size and tint bindings settle.
class GalleryIconImage : public QQuickIconImage {
    Q_OBJECT
    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged)
public:
    explicit GalleryIconImage(QQuickItem *parent = nullptr);
    QUrl source() const { return _requestedSource; }
    void setSource(const QUrl &source) override;

protected:
    void load() override;
    void updatePolish() override;

private:
    bool _loadPending = false;
    bool _loading = false;
    QUrl _requestedSource;
};

#endif
