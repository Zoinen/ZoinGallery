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
    std::atomic_bool blockResolve{false};
    std::atomic_int resolutions{0};
    QSharedPointer<std::atomic_int> leaseCount = QSharedPointer<std::atomic_int>::create(0);
    std::atomic_int &leases = *leaseCount;
    struct Lease final : ZoinGallery::DirectoryPreviewLease {
        QSharedPointer<std::atomic_int> count;
        explicit Lease(QSharedPointer<std::atomic_int> value) : count(std::move(value)) { ++*count; }
        ~Lease() override { --*count; }
    };
    ZoinGallery::DirectoryPreviewListing enumerate(
        const ZoinGallery::DirectorySourceDescriptor &,
        const QSharedPointer<ZoinGallery::ImageSourceCancellation> &cancel) override {
        ++enumerations;
        while (block && !cancel->isCanceled()) QThread::msleep(1);
        if (cancel->isCanceled() || fail) return {.error = QStringLiteral("failed")};
        return {names, QSharedPointer<Lease>::create(leaseCount), {}};
    }
    ZoinGallery::DirectoryPreviewResult resolve(
        const ZoinGallery::DirectorySourceDescriptor &,
        const ZoinGallery::DirectoryPreviewListing &, const QStringList &selected,
        const QSharedPointer<ZoinGallery::ImageSourceCancellation> &) override {
        ++resolutions;
        // Simulates a provider which ignores cancellation while resolving.
        while (blockResolve) QThread::msleep(1);
        QVariantList entries;
        for (const auto &name : selected)
            entries.append(QVariantMap{{"name", name}, {"localPath", QDir(imagePath).exists() ? QDir(imagePath).filePath(name) : imagePath}});
        return {entries, QSharedPointer<Lease>::create(leaseCount), {}};
    }
};

inline QVariantMap previewFolder(int index, int observation = 1) {
    const QString id = QStringLiteral("folder-%1").arg(index);
    return {{"entryId", id}, {"index", index}, {"name", id}, {"isDir", true},
        {"directorySource", QVariantMap{{"resourceId", id + QString::number(observation)},
            {"sourceKey", id}, {"version", QString::number(observation)}}}};
}
