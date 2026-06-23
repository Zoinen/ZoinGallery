#ifndef NATURALSORT_H
#define NATURALSORT_H

#include "ImageFile.h"

#include <QCollator>
#include <QFileInfo>
#include <QStringList>

#include <algorithm>

inline QCollator naturalNameCollator() {
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    return collator;
}

inline bool naturalNameLess(const QCollator &collator, const QString &left, const QString &right) {
    const int result = collator.compare(left, right);
    if (result != 0) {
        return result < 0;
    }
    return QString::compare(left, right, Qt::CaseSensitive) < 0;
}

inline void sortNamesNaturally(QStringList &names) {
    const QCollator collator = naturalNameCollator();
    std::sort(names.begin(), names.end(), [&collator] (const QString &left, const QString &right) {
        return naturalNameLess(collator, left, right);
    });
}

inline void sortFileInfosNaturally(QList<QFileInfo> &files) {
    const QCollator collator = naturalNameCollator();
    std::sort(files.begin(), files.end(), [&collator] (const QFileInfo &left, const QFileInfo &right) {
        return naturalNameLess(collator, left.fileName(), right.fileName());
    });
}

inline void sortFileInfosNaturally(QList<FileInfo> &files) {
    const QCollator collator = naturalNameCollator();
    std::sort(files.begin(), files.end(), [&collator] (const FileInfo &left, const FileInfo &right) {
        return naturalNameLess(collator, left.name, right.name);
    });
}

#endif // NATURALSORT_H
