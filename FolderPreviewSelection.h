#pragma once

#include "NaturalSort.h"
#include "ThumbnailLoader.h"
#include <QFileInfo>
#include <QSet>
#include <utility>

inline bool isVideoPreviewFormat(const QString &name) {
    static const QSet<QString> formats{
        QStringLiteral("mp4"), QStringLiteral("mkv"),
        QStringLiteral("webm"), QStringLiteral("avi"),
        QStringLiteral("mov"), QStringLiteral("m4v"),
        QStringLiteral("mpg"), QStringLiteral("mpeg"),
        QStringLiteral("wmv"), QStringLiteral("flv"),
        QStringLiteral("ts"), QStringLiteral("m2ts"),
        QStringLiteral("ogv"), QStringLiteral("3gp"),
        QStringLiteral("vob"), QStringLiteral("mts"),
        QStringLiteral("mxf"), QStringLiteral("f4v"),
        QStringLiteral("3g2"), QStringLiteral("ogm"),
        QStringLiteral("divx"), QStringLiteral("asf"),
        QStringLiteral("rm"), QStringLiteral("rmvb")};
    return formats.contains(QFileInfo(name).suffix().toLower());
}

inline QList<FileInfo> selectFolderPreviewImages(const QList<FileInfo> &entries, int limit = 16) {
    QList<FileInfo> images;
    for (const auto &entry : entries) {
        if (entry.isDirectory || (!ThumbnailLoader::isFormatSupported(entry.name)
                                  && !isVideoPreviewFormat(entry.name))) {
            continue;
        }
        FileInfo selected = entry;
        if (isVideoPreviewFormat(selected.name)) {
            selected.thumbnailKind = QStringLiteral("video");
        }
        images.append(std::move(selected));
    }
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
