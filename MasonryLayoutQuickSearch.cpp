#include "MasonryLayoutQuickSearch.h"
#include "MasonryLayout.h"
#include "FileListModel.h"

MasonryLayoutQuickSearch::MasonryLayoutQuickSearch(MasonryLayout *parent)
    : QObject(parent) {
    _validator = new QuickSearchValidator(this);
}

int MasonryLayoutQuickSearch::nextImage(bool forward, bool forceMoveToNext) {
    if (_mask.isEmpty()) {
        return masonryLayout()->_currentIndex;
    }

    int nextIndex = masonryLayout()->_currentIndex;
    bool foundMatch = false;
    bool didWrap = false;
    for (int i = masonryLayout()->_currentIndex + (forceMoveToNext ? (forward ? 1 : -1) : 0); i >= 0 && i < masonryLayout()->_bricks.size(); i += (forward ? 1 : -1)) {
        if (indexMatches(i, _mask)) {
            nextIndex = i;
            foundMatch = true;
            break;
        }
    }
    if (!foundMatch) {
        for (int i = (forward ? 0 : masonryLayout()->_bricks.size() - 1); i >= 0 && i < masonryLayout()->_bricks.size() && i != masonryLayout()->_currentIndex; i += (forward ? 1 : -1)) {
            if (indexMatches(i, _mask)) {
                nextIndex = i;
                foundMatch = true;
                didWrap = true;
                break;
            }
        }
    }
    if (didWrap) {
//        qDebug() << "<<<< WRAP";
    }
    if (!forceMoveToNext) {
        setMatches(foundMatch);
    }
    return nextIndex;
}

bool MasonryLayoutQuickSearch::hasResults(const QString &search) {
    if (search.isEmpty()) {
        return true;
    }

    int foundMatches = 0;
    for (int i = 0; i < masonryLayout()->_bricks.size(); i++) {
        if (indexMatches(i, search)) {
            foundMatches++;
        }
    }
    if (foundMatches) {
        _matchesInfo = QString("%1/%2").arg(foundMatches).arg(masonryLayout()->_bricks.size());
        emit matchesInfoChanged();
    }

    return foundMatches != 0;
}

bool MasonryLayoutQuickSearch::indexMatches(int index, const QString &search) {
    if (index < 0 || index > masonryLayout()->_bricks.size() - 1) {
        return false;
    }
    return masonryLayout()->indexText(index).contains(
        search, Qt::CaseInsensitive);
}

void MasonryLayoutQuickSearch::updateItemsText() {
    // Search matching uses the lightweight per-row name. Apply rich-text
    // highlighting only to ImageFile facades that already exist; requesting
    // ImageFileRole here used to materialize an entire large catalog on every
    // repeated cursor move while a mask was active.
    for (int i = 0; i < masonryLayout()->_bricks.size(); ++i) {
        updateItemText(i);
    }
}

void MasonryLayoutQuickSearch::updateItemText(int index) {
    if (index < 0 || index >= masonryLayout()->_bricks.size()) {
        return;
    }
    ImageFile *imageFile = masonryLayout()->_bricks[index].image;
    if (!imageFile) {
        return;
    }
    if (_mask.isEmpty()) {
        imageFile->setSearchText(QString());
        return;
    }

    const int matchStart = imageFile->fileName().indexOf(
        _mask, 0, Qt::CaseInsensitive);
    if (matchStart < 0) {
        imageFile->setSearchText(imageFile->fileName());
        return;
    }
    QString matchedText = imageFile->fileName();
    matchedText.insert(matchStart + _mask.size(), "</font>");
    const QString color = index == masonryLayout()->_currentIndex
        ? QStringLiteral("#ff9632") : QStringLiteral("#ffff00");
    matchedText.insert(
        matchStart,
        QStringLiteral(
            "<font color=\"#000000\" style=\"background-color: %1;\">")
            .arg(color));
    imageFile->setSearchText(matchedText);
}

MasonryLayout *MasonryLayoutQuickSearch::masonryLayout() {
    return static_cast<MasonryLayout *>(parent());
}

const QString &MasonryLayoutQuickSearch::mask() const {
    return _mask;
}

void MasonryLayoutQuickSearch::setMask(const QString &newMask) {
    if (_mask == newMask) {
        return;
    }
    _mask = newMask;
    emit maskChanged();

    updateItemsText();
}

bool MasonryLayoutQuickSearch::matches() const {
    return _matches;
}

void MasonryLayoutQuickSearch::setMatches(bool newMatches) {
    if (_matches == newMatches) {
        return;
    }
    _matches = newMatches;
    emit matchesChanged();
}

QValidator *MasonryLayoutQuickSearch::validator() const {
    return _validator;
}

QuickSearchValidator::QuickSearchValidator(MasonryLayoutQuickSearch *parent) :
    QValidator(parent) {
}

QValidator::State QuickSearchValidator::validate(QString &input, int &pos) const {
    MasonryLayoutQuickSearch *quickSearch = static_cast<MasonryLayoutQuickSearch *>(parent());
    if (quickSearch->hasResults(input)) {
        return Acceptable;
    }
    return Invalid;
}

const QString &MasonryLayoutQuickSearch::matchesInfo() const {
    return _matchesInfo;
}
