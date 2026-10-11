#include "MasonryLayout.h"

qreal MasonryLayout::paddingLeft() const {
    return _paddingLeft;
}

void MasonryLayout::setPaddingLeft(qreal newPaddingLeft) {
    if (qFuzzyCompare(_paddingLeft, newPaddingLeft))
        return;
    _paddingLeft = newPaddingLeft;
    if (_layoutUpdateDepth > 0) {
        _layoutUpdateNeedsPositionViewport = true;
    }
    else {
        positionViewport();
    }
    requestRewrap(false);
    emit paddingLeftChanged();
}

qreal MasonryLayout::paddingRight() const {
    return _paddingRight;
}

void MasonryLayout::setPaddingRight(qreal newPaddingRight) {
    if (qFuzzyCompare(_paddingRight, newPaddingRight))
        return;
    _paddingRight = newPaddingRight;
    requestRewrap(false);
    emit paddingRightChanged();
}

qreal MasonryLayout::paddingTop() const {
    return _paddingTop;
}

void MasonryLayout::setPaddingTop(qreal newPaddingTop) {
    if (qFuzzyCompare(_paddingTop, newPaddingTop))
        return;

    _paddingTop = newPaddingTop;
    _topItemOffset = _paddingTop;
    requestRewrap(false);
    emit paddingTopChanged();
}

qreal MasonryLayout::paddingBottom() const {
    return _paddingBottom;
}

void MasonryLayout::setPaddingBottom(qreal newPaddingBottom) {
    if (qFuzzyCompare(_paddingBottom, newPaddingBottom))
        return;
    _paddingBottom = newPaddingBottom;
    requestRewrap(false);
    emit paddingBottomChanged();
}

qreal MasonryLayout::width() const {
    return qMax(0.0, qIsInf(QQuickItem::width()) ? 0 : QQuickItem::width());
}

int MasonryLayout::listRowHeight() const {
    return _listRowHeight;
}

QQuickItem *MasonryLayout::viewport() const {
    return _viewport;
}

qreal MasonryLayout::contentHeight() const {
    return _contentHeight;
}
