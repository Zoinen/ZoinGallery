#pragma once
#include <ZoinGallery/DirectoryPreviewProvider.h>
#include <QThread>
#include <QDir>
#include <atomic>

class DirectoryPreviewFixture final : public ZoinGallery::DirectoryPreviewProvider {
public:
    QStringList names;
    QString imagePath;
    std::atomic_int enumerations{0};
    std::atomic_bool fail{false};
    std::atomic_bool block{false};
    std::atomic_int leases{0};
    struct Lease final : ZoinGallery::DirectoryPreviewLease {
        std::atomic_int *count;
        explicit Lease(std::atomic_int *value) : count(value) { ++*count; }
        ~Lease() override { --*count; }
    };
    ZoinGallery::DirectoryPreviewListing enumerate(
        const ZoinGallery::DirectorySourceDescriptor &,
        const QSharedPointer<ZoinGallery::ImageSourceCancellation> &cancel) override {
        ++enumerations;
        while (block && !cancel->isCanceled()) QThread::msleep(1);
        if (cancel->isCanceled() || fail) return {.error = QStringLiteral("failed")};
        return {names, QSharedPointer<Lease>::create(&leases), {}};
    }
    ZoinGallery::DirectoryPreviewResult resolve(
        const ZoinGallery::DirectorySourceDescriptor &,
        const ZoinGallery::DirectoryPreviewListing &, const QStringList &selected,
        const QSharedPointer<ZoinGallery::ImageSourceCancellation> &) override {
        QVariantList entries;
        for (const auto &name : selected)
            entries.append(QVariantMap{{"name", name}, {"localPath", QDir(imagePath).exists() ? QDir(imagePath).filePath(name) : imagePath}});
        return {entries, QSharedPointer<Lease>::create(&leases), {}};
    }
};

inline QVariantMap previewFolder(int index, int observation = 1) {
    const QString id = QStringLiteral("folder-%1").arg(index);
    return {{"entryId", id}, {"index", index}, {"name", id}, {"isDir", true},
        {"directorySource", QVariantMap{{"resourceId", id + QString::number(observation)},
            {"sourceKey", id}, {"version", QString::number(observation)}}}};
}
