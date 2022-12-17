#ifndef MASONRYLAYOUTQUICKSEARCH_H
#define MASONRYLAYOUTQUICKSEARCH_H

#include <QObject>
#include <QValidator>

class MasonryLayout;
class MasonryLayoutQuickSearch;

class QuickSearchValidator : public QValidator {
public:
    QuickSearchValidator(MasonryLayoutQuickSearch *parent = nullptr);

    State validate(QString &input, int &pos) const override;
};

class MasonryLayoutQuickSearch : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString mask READ mask WRITE setMask NOTIFY maskChanged)
    Q_PROPERTY(bool matches READ matches WRITE setMatches NOTIFY matchesChanged)
    Q_PROPERTY(QValidator* validator READ validator NOTIFY validatiorChanged)
    Q_PROPERTY(QString matchesInfo READ matchesInfo NOTIFY matchesInfoChanged)

public:
    explicit MasonryLayoutQuickSearch(MasonryLayout *parent = nullptr);

    Q_INVOKABLE int nextImage(bool forward, bool forceMoveToNext);
    bool hasResults(QString search);

    const QString &mask() const;
    void setMask(const QString &newMask);

    bool matches() const;
    void setMatches(bool newMatches);

    QValidator *validator() const;

    const QString &matchesInfo() const;

    void updateVisuals();
    QString indexTextWithQuickSearchApplied(int index);

signals:
    void maskChanged();
    void matchesChanged();
    void validatiorChanged();
    void matchesInfoChanged();

private:
    bool indexMatches(int index, QString search);
    MasonryLayout *masonryLayout();

    QString _mask;
    bool _matches;
    QValidator *_validator;
    QString _matchesInfo;

};

#endif // MASONRYLAYOUTQUICKSEARCH_H
