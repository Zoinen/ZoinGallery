#ifndef GALLERYNATIVETEXTMETRICS_H
#define GALLERYNATIVETEXTMETRICS_H

#include <QCache>
#include <QFont>
#include <QObject>

// Optical centering for NativeRendering leaves. Outline metrics alone do not
// describe the hinted, integer-pixel ink at a fractional window DPR.
class GalleryNativeTextMetrics : public QObject {
    Q_OBJECT
public:
    explicit GalleryNativeTextMetrics(QObject *parent = nullptr)
        : QObject(parent), _centers(128) {}

    Q_INVOKABLE qreal inkCenter(const QFont &font, const QString &text, qreal dpr);

private:
    QCache<QByteArray, qreal> _centers;
};

#endif
