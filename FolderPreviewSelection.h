#pragma once

#include "NaturalSort.h"
#include "ThumbnailLoader.h"

inline QList<FileInfo> selectFolderPreviewImages(const QList<FileInfo> &entries, int limit = 16) {
    QList<FileInfo> images;
    for (const auto &entry : entries)
        if (!entry.isDirectory && ThumbnailLoader::isFormatSupported(entry.name))
            images.append(entry);
    sortFileInfosNaturally(images);
    if (limit < 0 || images.size() <= limit) return images;
    QList<FileInfo> sampled;
    for (int i = 0; i < limit; ++i)
        sampled.append(images.at(qsizetype(i) * images.size() / limit));
    return sampled;
}

inline QStringList selectFolderPreviewNames(const QStringList &names) {
    QList<FileInfo> entries;
    for (const auto &name : names) { FileInfo item; item.name = name; item.isDirectory = false; entries.append(item); }
    QStringList selected;
    for (const auto &item : selectFolderPreviewImages(entries)) selected.append(item.name);
    return selected;
}
