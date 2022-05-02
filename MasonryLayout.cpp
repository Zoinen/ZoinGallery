#include "MasonryLayout.h"

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlProperty>

#include "FileListModel.h"

static void registerMyQmlTypes() {
    qmlRegisterType<MasonryLayout>("ZoinGallery", 1, 0, "MasonryLayout");
    qmlRegisterType<BrickItem>("ZoinGallery", 1, 0, "BrickItem");
}

Q_COREAPP_STARTUP_FUNCTION(registerMyQmlTypes)


MasonryLayout::MasonryLayout(QQuickItem *parent)
    : QQuickItem(parent) {
    _targetHeight = 100;
    _visibleStart = -1;
    _visibleEnd = -1;
    _topItem = 10;//-1;
    setCurrentIndex(_topItem);
    _contentY = 0;
    _model = nullptr;
    _delegate = nullptr;
    _currentIndex = 0;
}

void MasonryLayout::componentComplete() {
    QQuickItem::componentComplete();

    connect(this, &MasonryLayout::widthChanged,
            this, &MasonryLayout::rewrap);

    connect(this, &MasonryLayout::heightChanged, this, [&] () {
        int newContentY = qMin<int>(_contentY, qMax<int>(0, contentHeight() - height()));
        if (newContentY != _contentY) {
            setContentYInternal(newContentY);
        }
        else {
            updateProperties();
        }
    });
}

BrickItem *MasonryLayout::createComponent() {
    if (!_delegate) {
        qDebug() << "Empty delegate";
    }

    if (_delegate->status() != QQmlComponent::Ready) {
        qDebug() << "Error in component:" << _delegate->status() << _delegate->errors();
    }

    BrickItem *object = qobject_cast<BrickItem*>(_delegate->create(QQmlEngine::contextForObject(this)));
    object->setParentItem(this);
    object->setParent(this);
    return object;
}

void MasonryLayout::rewrap() {
//    qDebug() << "rewrap" << width() << _bricks.size();
    int targetWidth = width();

    int currentRow = 0;
    int currentColumn = 0;
    qreal lastX = 0;
    qreal lastY = 0;

    int newContentY = _contentY;
    int contentYOffset = 0;
    if (_topItem != -1 && _topItem < _bricks.size()) {
        contentYOffset = _contentY - _bricks[_topItem].y;
//        qDebug() << "offset" << _topItem << contentYOffset << _contentY << _bricks[_topItem].y;
    }

    int rowTargetWidth = targetWidth;
    for (int i = 0; i < _bricks.size(); i++) {
        qreal divider = _bricks[i].originalSize.height() / qreal(_targetHeight);
        _bricks[i].normalizedSize = QSizeF(_bricks[i].originalSize.width() / divider, _bricks[i].originalSize.height() / divider);
        _bricks[i].row = currentRow;
        _bricks[i].column = currentColumn;
        _bricks[i].x = lastX;
        _bricks[i].y = lastY;

        if (_topItem == i) {
            newContentY = lastY + contentYOffset;
        }

        if (lastX + _bricks[i].normalizedSize.width() < rowTargetWidth) {
            lastX += _bricks[i].normalizedSize.width();
        }
        else if (!currentColumn) {
            qreal divider = _bricks[i].originalSize.width() / qreal(targetWidth);
            _bricks[i].normalizedSize = QSizeF(_bricks[i].originalSize.width() / divider, _bricks[i].originalSize.height() / divider);

            currentRow++;
            currentColumn = -1;
            lastX = 0;

            if (i != _bricks.size() - 1) {
                lastY += _bricks[i].normalizedSize.height();
            }
        }
        else {
            qreal stretchFactor = rowTargetWidth / qreal(lastX);
            for (int reverseI = i - 1; reverseI >= 0; reverseI--) {
                if (_bricks[reverseI].row != currentRow) {
                    break;
                }
                _bricks[reverseI].normalizedSize *= stretchFactor;
                _bricks[reverseI].x *= stretchFactor;
            }
            currentRow++;
            currentColumn = -1;
            lastX = 0;
            lastY += _targetHeight * stretchFactor;
            rowTargetWidth = targetWidth;
            i--;
        }

        currentColumn++;
    }
    if (_bricks.size()) {
        setContentHeight(lastY + _bricks.last().normalizedSize.height());
//        if (_pics.size() > 1 && _pics[_pics.size() - 2].row == _pics[_pics.size() - 1].row) {
//        }
//        else {
//            setContentHeight(lastY);
//        }
    }
    else {
        setContentHeight(0);
    }

    newContentY = qMin<int>(newContentY, qMax<int>(0, contentHeight() - height()));
    if (newContentY != _contentY) {
        setContentYInternal(newContentY);
    }
    else {
        updateProperties();
    }
}

QString rectToString(QRectF rect) {
    QRect rectI = rect.toRect();
    return QString("%1,%2\n%3x%4").arg(rectI.x()).arg(rectI.y()).arg(rectI.width()).arg(rectI.height());
}

void MasonryLayout::updateProperties() {
//    qDebug() << "update props";
    int newVisibleStart = -1;
    int newVisibleEnd = -1;
    QRectF boundingRect_ = boundingRect().adjusted(0, -_targetHeight*2, 0, _targetHeight*2);
    for (int i = 0; i < _bricks.size(); i++) {
        if (_bricks[i].viewGeometry(_contentY).intersects(boundingRect_)) {
            if (newVisibleStart == -1) {
                newVisibleStart = i;
            }
        }
        else {
            if (newVisibleStart != -1 && newVisibleEnd == -1) {
                newVisibleEnd = i;
                break;
            }
        }
    }
    if (newVisibleEnd == -1) {
        newVisibleEnd = _bricks.size() - 1;
    }

    if (_visibleStart != -1) {
        for (int i = _visibleStart; i <= _visibleEnd; i++) {
            if (i < newVisibleStart || i > newVisibleEnd) {
//                qDebug() << "hiding" << i;
                pushBrickItem(_bricks[i].item);
                _bricks[i].item->setVisible(false);
                _bricks[i].item = nullptr;
            }
        }
    }

    if (newVisibleStart != -1) {
        for (int i = newVisibleStart; i <= newVisibleEnd; i++) {
//            qDebug() << "showing" << i;
            bool instantMove = true;
            if (!_bricks[i].item) {
                _bricks[i].item = popBrickItem();
            }
            else if (_bricks[i].item->row() != _bricks[i].row || _bricks[i].item->column() != _bricks[i].column) {
                // TODO: Do this when column count in the row changes too
                instantMove = false;
            }
            _bricks[i].item->setRowColumn(_bricks[i].row, _bricks[i].column);
            if (!instantMove) {
                if (!_bricks[i].item->property("singleMoveAnimationEnabled").toBool()) {
                    QQmlProperty(_bricks[i].item, "singleMoveAnimationEnabled").write(true);
                }
            }
            _bricks[i].item->setGeometry(_bricks[i].viewGeometry(_contentY), false);
            _bricks[i].item->setVisible(true);
//            QQmlProperty(_pics[i].item, "text").write(QString("%1 [%2,%3]\n%4").arg(i).arg(_pics[i].row).arg(_pics[i].column).arg(rectToString(_pics[i].viewGeometry(_contentY))));
            if (_bricks[i].item->property("text").toString() != _bricks[i].image->text) {
                QQmlProperty(_bricks[i].item, "text").write(_bricks[i].image->text);
//                QQmlProperty(_bricks[i].item, "text").write(QString("%1x%2").arg(_bricks[i].row).arg(_bricks[i].column));
            }
            if (_bricks[i].item->property("index").toInt() != i) {
                QQmlProperty(_bricks[i].item, "index").write(i);
            }

            QString imageId;
            if (_bricks[i].image->imageId.isEmpty()) {
                imageId = "";
            }
            else {
                imageId = QString("image://thumbnails/") + _bricks[i].image->imageId;
            }
            if (_bricks[i].item->property("imageId").toString() != imageId) {
                QQmlProperty(_bricks[i].item, "imageId").write(imageId);
            }
        }
    }

    _visibleStart = newVisibleStart;
    _visibleEnd = newVisibleEnd;

    //    qDebug() << "visible delegates" << _usedBrickItems.size() << ", total" << _freeBrickItems.size();
}

void MasonryLayout::setContentHeight(int newContentHeight) {
    if (_contentHeight == newContentHeight) {
        return;
    }
    _contentHeight = newContentHeight;
    emit contentHeightChanged();
}

void MasonryLayout::onDataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight, const QVector<int> &roles) {
    int index = topLeft.row();
    if (index < _bricks.size()) {
        if (roles.contains(FileListModel::ImageIdRole)) {
            qreal oldAspect = _bricks[index].originalSize.width() / _bricks[index].originalSize.height();
            qreal newAspect = _bricks[index].image->fullSize.width() / qreal(_bricks[index].image->fullSize.height());

            if (!qFuzzyCompare(oldAspect, newAspect)) {
                // Todo: rewrap only from the current row
                rewrap();
                updateProperties();
                //                _bricks[index].item->setGeometry(_bricks[index].viewGeometry(_contentY));
                //                QQmlProperty(_bricks[index].item, "imageId").write(QString("image://thumbnails/") + _bricks[index].image->imageId);
            }
            else {
                QQmlProperty(_bricks[index].item, "imageId").write(QString("image://thumbnails/") + _bricks[index].image->imageId);
            }
        }
        if (roles.contains(FileListModel::ImageFullSizeRole)) {
            _bricks[index].originalSize = _bricks[index].image->fullSize;
        }
    }
}

void MasonryLayout::onModelReset() {
    _visibleStart = -1;
    _visibleEnd = -1;
    _topItem = 10;
    setCurrentIndex(_topItem);
    for (auto it = _usedBrickItems.begin(); it != _usedBrickItems.end(); ++it) {
        (*it)->setVisible(false);
    }
    _freeBrickItems.unite(_usedBrickItems);
    _usedBrickItems.clear();

    _bricks.clear();
    for (int i = 0; i < _model->rowCount(); i++) {
        FileListModel::ImageFile *imageFile = _model->data(_model->index(i), FileListModel::ImageFileRole).value<FileListModel::ImageFile *>();
        QSize imgSize = imageFile->fullSize;
        if (imgSize.isEmpty()) {
            imgSize = QSize(3, 2);
        }
        _bricks.append(MasonryBrick(imageFile, imgSize));
    }
    emit modelChanged();
    rewrap();
}

void MasonryLayout::pushBrickItem(BrickItem *item) {
    _usedBrickItems.remove(item);
    _freeBrickItems.insert(item);
}

BrickItem *MasonryLayout::popBrickItem() {
    BrickItem *item = nullptr;
    if (_freeBrickItems.size() > 0) {
        item = *_freeBrickItems.begin();
        _freeBrickItems.remove(item);
    }
    else {
        item = createComponent();
    }
    _usedBrickItems.insert(item);
    return item;
}

int MasonryLayout::targetHeight() const {
    return _targetHeight;
}

void MasonryLayout::setTargetHeight(int newTargetHeight) {
    if (_targetHeight == newTargetHeight) {
        return;
    }
    _targetHeight = newTargetHeight;
    rewrap();
    emit targetHeightChanged();
}

int MasonryLayout::contentY() const {
    return _contentY;
}

void MasonryLayout::setContentY(int newContentY) {
    setContentYInternal(newContentY);
    for (_topItem = _visibleStart; _topItem < _visibleEnd && _topItem < _bricks.size(); _topItem++) {
        if (_bricks[_topItem].viewGeometry(_contentY).y() >= 0) {
            break;
        }
    }
//    _topItem = _visibleStart;
    setCurrentIndex(_topItem);
//    qDebug() << "top is" << _topItem;
}

void MasonryLayout::setContentYInternal(int newContentY) {
    static int depth = 0;
    depth++;
//    qDebug() << "set content y" << newContentY;
    if (_contentY == newContentY || depth > 1) {
//        qDebug() << "skip";
        depth--;
        return;
    }
//    qDebug() << "contentY" << _contentY << "->" << newContentY;
    _contentY = newContentY;
    updateProperties();
    emit contentYChanged();
    depth--;
}

int MasonryLayout::contentHeight() const {
    return _contentHeight;
}

BrickItem::BrickItem(QQuickItem *parent)
    : QQuickItem(parent) {
    _isChangingGeometry = false;
}

void BrickItem::setRowColumn(int row, int column) {
    _row = row;
    _column = column;
}

void BrickItem::setGeometry(QRectF rect, bool instantMove) {
    if (!isVisible() || instantMove) {
        QRectF oldGeometry(x(), y(), width(), height());
        _isChangingGeometry = true;

        if (x() != rect.x()) {
            setX(rect.x());
        }
        if (y() != rect.y()) {
            setY(rect.y());
        }
        if (width() != rect.width()) {
            setWidth(rect.width());
        }
        if (height() != rect.height()) {
            setHeight(rect.height());
        }

        _isChangingGeometry = false;

        if (oldGeometry != rect) {
            geometryChanged(oldGeometry, rect);
        }
    }
    else {
        if (x() != rect.x()) {
            QQmlProperty(this, "x").write(rect.x());
        }
        if (y() != rect.y()) {
            QQmlProperty(this, "y").write(rect.y());
        }
        if (width() != rect.width()) {
            QQmlProperty(this, "width").write(rect.width());
        }
        if (height() != rect.height()) {
            QQmlProperty(this, "height").write(rect.height());
        }
    }
}

int BrickItem::row() const {
    return _row;
}

int BrickItem::column() const {
    return _column;
}

void BrickItem::geometryChanged(const QRectF &newGeometry, const QRectF &oldGeometry) {
    if (!_isChangingGeometry) {
        // Qt 6.3 this got broken
        QQuickItem::geometryChanged(newGeometry, oldGeometry);
    }
}

MasonryLayout::MasonryBrick::MasonryBrick(int width, int height)
    : originalSize(QSize(width, height)), x(0), y(0), row(0), column(0), item(nullptr), image(nullptr) {
}

MasonryLayout::MasonryBrick::MasonryBrick(FileListModel::ImageFile *image_, QSizeF originalSize_)
    : originalSize(originalSize_), x(0), y(0), row(0), column(0), item(nullptr), image(image_) {
}

QRectF MasonryLayout::MasonryBrick::viewGeometry(int contentY) const {
    return QRectF(QPointF(x, y - contentY), normalizedSize);
}

QAbstractListModel *MasonryLayout::model() const {
    return _model;
}

void MasonryLayout::setModel(QAbstractListModel *newModel) {
    if (_model) {
        disconnect(_model, &QAbstractListModel::dataChanged,
                   this, &MasonryLayout::onDataChanged);
        disconnect(_model, &QAbstractListModel::modelReset,
                   this, &MasonryLayout::onModelReset);
    }
    _model = newModel;
    connect(_model, &QAbstractListModel::dataChanged,
            this, &MasonryLayout::onDataChanged);
    connect(_model, &QAbstractListModel::modelReset,
            this, &MasonryLayout::onModelReset);

    onModelReset();
}

int MasonryLayout::currentIndex() const {
    return _currentIndex;
}

void MasonryLayout::setCurrentIndex(int newCurrentIndex) {
    if (_currentIndex == newCurrentIndex) {
        return;
    }
    _currentIndex = newCurrentIndex;
    emit currentIndexChanged();
}
