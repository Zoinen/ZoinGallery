#include "GalleryQuickSearchIndex.h"

#include <ZoinGallery/GalleryCatalogModel.h>

#include <QAbstractListModel>
#include <QTest>

namespace {

class NameModel final : public QAbstractListModel {
public:
    explicit NameModel(int count, QObject *parent = nullptr)
        : QAbstractListModel(parent), _count(count) {}

    int rowCount(const QModelIndex &parent = {}) const override {
        return parent.isValid() ? 0 : _count;
    }

    QVariant data(const QModelIndex &index, int role) const override {
        if (!index.isValid() || index.row() < 0 || index.row() >= _count) {
            return {};
        }
        if (role == Qt::UserRole + 1) {
            ++_nameReads;
            const QString marker = index.row() % 100 == 0
                ? QStringLiteral("needle-") : QStringLiteral("ordinary-");
            return marker + QString::number(index.row());
        }
        return {};
    }

    QHash<int, QByteArray> roleNames() const override {
        return {{Qt::UserRole + 1, QByteArrayLiteral("entryName")}};
    }

    int nameReads() const { return _nameReads; }

private:
    int _count = 0;
    mutable int _nameReads = 0;
};

class GalleryQuickSearchIndexTest final : public QObject {
    Q_OBJECT

private slots:
    void indexesOnceAndNarrowsOnlyPreviousCandidates() {
        constexpr int rowCount = 30000;
        NameModel source(rowCount);
        ZoinGallery::GalleryCatalogModel catalog;
        catalog.setSourceModel(&source);
        ZoinGallery::GalleryQuickSearchIndex index;
        index.setModel(&catalog);

        QVERIFY(index.setQuery(QStringLiteral("needle")));
        QCOMPARE(source.nameReads(), rowCount);
        QCOMPARE(index.indexedRowCount(), rowCount);
        QCOMPARE(index.lastVisitedRowCount(), rowCount);
        QCOMPARE(index.matchCount(), rowCount / 100);

        QVERIFY(index.setQuery(QStringLiteral("needle-12")));
        QCOMPARE(source.nameReads(), rowCount);
        QCOMPARE(index.lastVisitedRowCount(), rowCount / 100);

        const int readsBeforeNavigation = source.nameReads();
        QCOMPARE(index.nextMatch(-1, true, true, false), 1200);
        QCOMPARE(index.nextMatch(1200, true, true, false), 12000);
        QCOMPARE(index.nextMatch(12000, false, true, false), 1200);
        QCOMPARE(source.nameReads(), readsBeforeNavigation);
        QCOMPARE(index.lastVisitedRowCount(), rowCount / 100);
    }

    void returnsUnicodeCodePointOffsets() {
        class UnicodeModel final : public QAbstractListModel {
        public:
            int rowCount(const QModelIndex &parent = {}) const override {
                return parent.isValid() ? 0 : 1;
            }
            QVariant data(const QModelIndex &index, int role) const override {
                return index.isValid() && role == Qt::UserRole + 1
                    ? QVariant(QString::fromUtf8("a\xF0\x9F\x98\x80needle"))
                    : QVariant();
            }
            QHash<int, QByteArray> roleNames() const override {
                return {{Qt::UserRole + 1,
                         QByteArrayLiteral("entryName")}};
            }
        } source;
        ZoinGallery::GalleryCatalogModel catalog;
        catalog.setSourceModel(&source);
        ZoinGallery::GalleryQuickSearchIndex index;
        index.setModel(&catalog);
        QVERIFY(index.setQuery(QStringLiteral("needle")));
        const auto match = index.matchAt(0);
        QCOMPARE(match.utf16Start, 2);
        QCOMPARE(match.utf16Length, 6);
    }

    void rejectedExtensionPreservesPreviousQueryAndMatches() {
        NameModel source(1000);
        ZoinGallery::GalleryCatalogModel catalog;
        catalog.setSourceModel(&source);
        ZoinGallery::GalleryQuickSearchIndex index;
        index.setModel(&catalog);
        QVERIFY(index.setQuery(QStringLiteral("needle")));
        const int matches = index.matchCount();
        QVERIFY(!index.setQuery(QStringLiteral("needle-not-present")));
        QCOMPARE(index.query(), QStringLiteral("needle"));
        QCOMPARE(index.matchCount(), matches);
    }
};

} // namespace

QTEST_MAIN(GalleryQuickSearchIndexTest)
#include "GalleryQuickSearchIndexTest.moc"
