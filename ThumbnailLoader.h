#ifndef THUMBNAILLOADER_H
#define THUMBNAILLOADER_H

#include <QObject>
#include <QString>

class ThumbnailLoader {
public:
    ThumbnailLoader();

    static void init();

    QImage load(const QString &path);
};

#endif // THUMBNAILLOADER_H
