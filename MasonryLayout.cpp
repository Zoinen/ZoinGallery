#include "MasonryLayout.h"
#include "FileListModel.h"

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlProperty>
#include <QQuickWindow>

int MasonryLayout::_spacing = 4; // Should be divisible by 4

static void registerMyQmlTypes() {
    qmlRegisterType<MasonryLayout>("ZoinGallery", 1, 0, "MasonryLayout");
    qmlRegisterType<BrickItem>("ZoinGallery", 1, 0, "BrickItem");
}

Q_COREAPP_STARTUP_FUNCTION(registerMyQmlTypes)


QRectF roundRect(const QRectF &rectF) {
    // Rounding to floor
    return QRect(rectF.x(), rectF.y(), rectF.width(), rectF.height());
}


MasonryLayout::MasonryLayout(QQuickItem *parent)
    : QQuickItem(parent) {
    _targetHeight = 200;
    _visibleStart = -1;
    _visibleEnd = -1;
    _topItem = 0;
    _topItemOffset = 0;
    setCurrentIndex(_topItem);
    _contentY = 0;
    _model = nullptr;
    _delegate = nullptr;
    _currentIndex = 0;
    _viewport = nullptr;
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
    if (!_viewport) {
        QQmlComponent component(qmlEngine(this));
        component.setData(R"QML(
        import QtQuick 2.15

        Item {
//            anchors.left: parent.left
//            anchors.right: parent.right
        }
        )QML", QUrl());
        if (component.status() != QQmlComponent::Ready) {
            qDebug() << "Error in component:" << component.status() << component.errors();
        }

        _viewport = qobject_cast<QQuickItem*>(component.create(QQmlEngine::contextForObject(this)));
        _viewport->setParentItem(this);
        _viewport->setParent(this);
    }

    if (!_delegate) {
        qDebug() << "Empty delegate";
    }

    if (_delegate->status() != QQmlComponent::Ready) {
        qDebug() << "Error in component:" << _delegate->status() << _delegate->errors();
    }

    BrickItem *object = qobject_cast<BrickItem*>(_delegate->create(QQmlEngine::contextForObject(this)));
    object->setParentItem(_viewport);
    object->setParent(_viewport);
    return object;
}

void MasonryLayout::rewrap() {
//    qDebug() << "rewrap" << width() << _bricks.size();
    calcLayout(_bricks, width(), _targetHeight, _spacing * window()->devicePixelRatio());

    if (_bricks.size()) {
        setContentHeight(_bricks.last().y + _bricks.last().normalizedSize.height());
    }
    else {
        setContentHeight(0);
    }

    qreal newContentY = _contentY;
    if (_topItem < _bricks.size()) {
        newContentY = qMax<int>(0, qMin<int>(_bricks[_topItem].y - _topItemOffset, contentHeight() - height()));
//        qDebug() << "new contentY" << newContentY << "top item" << _topItem << "offset" << _topItemOffset;
    }
    if (newContentY != _contentY) {
        setContentYInternal(newContentY);
    }
    else {
        updateProperties();
    }
}

QSizeF scaleToWidthWithSpacing(const QSizeF &size, qreal toWidth, int spacing) {
    qreal aspect = (size.width() - spacing) / (size.height() - spacing);
    return QSizeF(toWidth, (toWidth - spacing) / aspect + spacing);
}

QSizeF scaleToHeightWithSpacing(const QSizeF &size, qreal toHeight, int spacing) {
    qreal aspect = (size.width() - spacing) / (size.height() - spacing);
    return QSizeF((toHeight - spacing) * aspect + spacing, toHeight);
}

void MasonryLayout::calcLayout(QList<MasonryBrick> &bricks, int canvasWidth, int rowTargetHeight, int spacing) {
    int currentRow = 0;
    int currentColumn = 0;
    qreal lastX = 0;
    qreal lastY = 0;

    for (int i = 0; i < bricks.size(); i++) {
        bricks[i].normalizedSize = scaleToHeightWithSpacing(bricks[i].originalSize, rowTargetHeight, spacing);
        bricks[i].row = currentRow;
        bricks[i].column = currentColumn;
        bricks[i].x = lastX;
        bricks[i].y = lastY;

        bool forceNewline = bricks[i].forceNewLine;
        bricks[i].forceNewLine = false;

        if (lastX + bricks[i].normalizedSize.width() < canvasWidth && !forceNewline) {
            lastX += bricks[i].normalizedSize.width();
        }
        else if (!currentColumn) {
            bricks[i].normalizedSize = scaleToWidthWithSpacing(bricks[i].originalSize, canvasWidth, spacing);

            currentRow++;
            currentColumn = -1;
            lastX = 0;

            if (i != bricks.size() - 1) {
                lastY += bricks[i].normalizedSize.height();
            }
        }
        else {
            int bricksInRow = bricks[i-1].column + 1;

            qreal totalWidthWithoutSpacing = lastX - (bricksInRow * spacing);
            qreal stretchFactor = (canvasWidth - bricksInRow * spacing) / totalWidthWithoutSpacing;
            qreal newRowHeight = (rowTargetHeight - spacing) * stretchFactor + spacing;

            for (int rowIndex = i - bricksInRow; rowIndex < i; rowIndex++) {
                bricks[rowIndex].normalizedSize = scaleToHeightWithSpacing(bricks[rowIndex].normalizedSize, newRowHeight, spacing);
                if (bricks[rowIndex].column) {
                    bricks[rowIndex].x = bricks[rowIndex - 1].x + bricks[rowIndex - 1].normalizedSize.width();
                }
            }
            currentRow++;
            currentColumn = -1;
            lastX = 0;
            lastY += newRowHeight;
            i--;
        }

        currentColumn++;
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
    QRectF boundingRect_ = boundingRect().adjusted(0, -_targetHeight*2 + _contentY, 0, _targetHeight*2 + _contentY);

    int currentRow = -1;
    for (int i = 0; i < _bricks.size(); i++) {
        if (currentRow != _bricks[i].row) {
            currentRow = _bricks[i].row;

            if (_bricks[i].geometry().intersects(boundingRect_)) {
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
    }
    if (newVisibleEnd == -1) {
        newVisibleEnd = _bricks.size() - 1;
    }

    QSet<BrickItem *> itemsToHide;
    if (_visibleStart != -1) {
        for (int i = _visibleStart; i <= _visibleEnd; i++) {
            if (i < newVisibleStart || i > newVisibleEnd) {
                pushBrickItem(_bricks[i].item);
                itemsToHide.insert(_bricks[i].item);
                _bricks[i].item = nullptr;
            }
        }
    }

    if (newVisibleStart != -1) {
        for (int i = newVisibleStart; i <= newVisibleEnd; i++) {
            if (!_bricks[i].item) {
                _bricks[i].item = popBrickItem();
            }
            _bricks[i].item->setRowColumn(_bricks[i].row, _bricks[i].column);

            if (roundRect(_bricks[i].item->geometry()) != roundRect(_bricks[i].geometry())) {
                _bricks[i].item->setGeometry(_bricks[i].geometry(), false);
            }

            if (itemsToHide.contains(_bricks[i].item)) {
                itemsToHide.remove(_bricks[i].item);
            }
            else {
                if (!_bricks[i].item->isVisible()) {
                    _bricks[i].item->setVisible(true);
                }
            }

            if (_bricks[i].item->property("text").toString() != _bricks[i].image->path) {
                _bricks[i].item->setProperty("text", _bricks[i].image->path);
            }
            if (_bricks[i].item->property("index").toInt() != i) {
                _bricks[i].item->setProperty("index", i);
            }

            QString imageId;
            if (_bricks[i].image->imageId.isEmpty()) {
                imageId = "";
            }
            else {
                imageId = QString("image://thumbnails/") + _bricks[i].image->imageId;
            }
            if (_bricks[i].item->property("imageId").toString() != imageId) {
                _bricks[i].item->setProperty("imageId", imageId);
            }
        }
    }

    _visibleStart = newVisibleStart;
    _visibleEnd = newVisibleEnd;

    for (BrickItem *item : itemsToHide) {
        item->setVisible(false);
    }
}

void MasonryLayout::setContentHeight(int newContentHeight) {
    if (_contentHeight == newContentHeight) {
        return;
    }
    _contentHeight = newContentHeight;
    if (_viewport) {
        _viewport->setHeight(_contentHeight);
    }
    emit contentHeightChanged();
}

void MasonryLayout::onDataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight, const QVector<int> &roles) {
    int index = topLeft.row();
    if (index < _bricks.size()) {
        if (roles.contains(FileListModel::ImageIdRole)) {
            if (_bricks[index].item) {
                _bricks[index].item->setProperty("imageId", QString("image://thumbnails/") + _bricks[index].image->imageId);
            }
        }
        if (roles.contains(FileListModel::ImageFullSizeRole)) {
            pushToCurrentRow(index);
        }
    }
}

void MasonryLayout::pushToCurrentRow(int index) {
    bool flushMode = index >= _bricks.count();
    if (!_currentLoadingRow.count() || index - _currentLoadingRow.last().globalIndex > 1) {
        int lastIndex = -1;
        if (_currentLoadingRow.count()) {
            lastIndex = _currentLoadingRow.last().globalIndex;
        }
        int indexToInsert = _currentLoadingRow.count();
        for (int i = index - 1; i >= 0; i--) {
            if (i > lastIndex) {
//                qDebug() << "adding index" << i << "from" << index << i << lastIndex;
                _currentLoadingRow.insert(indexToInsert, _bricks[i]);
                _currentLoadingRow[indexToInsert].globalIndex = i;
            }
            else {
                break;
            }
        }
    }
    if (!flushMode) {
        _currentLoadingRow.append(MasonryBrick(_bricks[index].image->fullSize.width(), _bricks[index].image->fullSize.height()));
        _currentLoadingRow.last().globalIndex = index;
    }

//    qDebug() << "==";
//    for (int k = 0; k < _currentLoadingRow.count(); k++) {
//        qDebug() << "In row" << _currentLoadingRow[k].globalIndex;
//    }
//    qDebug() << "==";

    calcLayout(_currentLoadingRow, width(), _targetHeight, _spacing * window()->devicePixelRatio());
    if (_currentLoadingRow.last().row > 0 || flushMode) {
//        qDebug() << "REWRAP";
        QList<ThumbnailReadRequest> requests;
        for (int i = 0; i < _currentLoadingRow.size(); i++) {
            if (_currentLoadingRow[i].row != _currentLoadingRow.last().row || flushMode) {
                int updIndex = _currentLoadingRow[i].globalIndex;
                if (_bricks[updIndex].image && _bricks[updIndex].image->fullSize.isValid()) {
                    _bricks[updIndex].originalSize = _bricks[updIndex].image->fullSize;

                    QSize thumbnailSize = _currentLoadingRow[i].thumbnailSize() * window()->devicePixelRatio();
                    requests.append(ThumbnailReadRequest(_bricks[updIndex].image->path, thumbnailSize));
                }
            }
            else {
                int forceNewLineFrom = _currentLoadingRow[i].globalIndex;
                _bricks[forceNewLineFrom].forceNewLine = true;
                for (int delIndex = 0; delIndex < i; delIndex++) {
                    _currentLoadingRow.removeFirst();
                }
                break;
            }
        }
        rewrap();
        updateProperties();
        static_cast<FileListModel *>(_model)->addRequestThumbnails(requests);
    }
}

void MasonryLayout::onThumbnailReadFinished() {
    pushToCurrentRow(_bricks.count());
}

void MasonryLayout::onModelReset() {
    _currentLoadingRow.clear();
    _visibleStart = -1;
    _visibleEnd = -1;
    _topItem = 0;
    setCurrentIndex(_topItem);
    for (auto it = _usedBrickItems.begin(); it != _usedBrickItems.end(); ++it) {
        (*it)->setVisible(false);
    }
    _freeBrickItems.unite(_usedBrickItems);
    _usedBrickItems.clear();

    _bricks.clear();
    qDebug() << "model reset";
    for (int i = 0; i < _model->rowCount(); i++) {
        ImageFile *imageFile = _model->data(_model->index(i), FileListModel::ImageFileRole).value<ImageFile *>();
        QSize imgSize = imageFile->fullSize;
        if (imgSize.isEmpty()) {
            imgSize = QSize(3, 2);
        }
        _bricks.append(MasonryBrick(imageFile, imgSize));
    }
    emit modelChanged();
    rewrap();
    if (_viewport) {
        _viewport->setY(-_contentY);
    }

    static_cast<FileListModel *>(_model)->requestThumbnails(QSize(_targetHeight * 3 / 2, _targetHeight) * window()->devicePixelRatio());
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
//        qDebug() << "create component";
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
    if (_visibleStart != -1) {
        for (_topItem = _visibleStart; _topItem < _visibleEnd && _topItem < _bricks.size(); _topItem++) {
            if (_bricks[_topItem].y >= _contentY) {
                break;
            }
        }
    }
    if (_topItem != -1 && _topItem < _bricks.count()) {
        _topItemOffset = _bricks[_topItem].y - _contentY;
//        qDebug() << "set contentY" << newContentY << "top item" << _topItem << "offset" << _topItemOffset;
    }
//    _topItem = _visibleStart;
//    setCurrentIndex(_topItem);
}

void MasonryLayout::setContentYInternal(int newContentY) {
    static int depth = 0;
    depth++;
    if (_contentY == newContentY || depth > 1) {
//        qDebug() << "skip";
        depth--;
        return;
    }
//    qDebug() << "contentY" << _contentY << "->" << newContentY;
    _contentY = newContentY;
    if (_viewport) {
        _viewport->setY(-_contentY);
    }

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
    rect = roundRect(rect);

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

    if (oldGeometry != rect && !instantMove) {
#if QT_VERSION < QT_VERSION_CHECK(6, 3, 0)
        geometryChanged(oldGeometry, rect);
#else
        geometryChange(oldGeometry, rect);
#endif
    }
}

QRectF BrickItem::geometry() const {
    return QRectF(x(), y(), width(), height());
}

int BrickItem::row() const {
    return _row;
}

int BrickItem::column() const {
    return _column;
}

#if QT_VERSION < QT_VERSION_CHECK(6, 3, 0)
void BrickItem::geometryChanged(const QRectF &newGeometry, const QRectF &oldGeometry) {
    if (!_isChangingGeometry) {
        QQuickItem::geometryChanged(newGeometry, oldGeometry);
    }
}
#else
void BrickItem::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) {
    if (!_isChangingGeometry) {
        QQuickItem::geometryChange(newGeometry, oldGeometry);
    }
}
#endif

MasonryLayout::MasonryBrick::MasonryBrick(int width, int height)
    : originalSize(QSize(width, height)), x(0), y(0), row(0), column(0), item(nullptr), image(nullptr), globalIndex(-1),
      forceNewLine(false) {
}

MasonryLayout::MasonryBrick::MasonryBrick(ImageFile *image_, QSizeF originalSize_)
    : originalSize(originalSize_), x(0), y(0), row(0), column(0), item(nullptr), image(image_), globalIndex(-1),
      forceNewLine(false) {
}

QRectF MasonryLayout::MasonryBrick::geometry() const {
    return QRectF(QPointF(x, y), normalizedSize);
}

QSize MasonryLayout::MasonryBrick::thumbnailSize() const {
    return roundRect(geometry()).toRect().size() - QSize(MasonryLayout::spacing(), MasonryLayout::spacing());
}

QAbstractListModel *MasonryLayout::model() const {
    return _model;
}

void MasonryLayout::setModel(QAbstractListModel *newModel) {
    FileListModel *fileModel = static_cast<FileListModel *>(newModel);
    if (_model) {
        disconnect(_model, &QAbstractListModel::dataChanged,
                   this, &MasonryLayout::onDataChanged);
        disconnect(_model, &QAbstractListModel::modelReset,
                   this, &MasonryLayout::onModelReset);
        disconnect(fileModel, &FileListModel::thumbnailReadFinished,
                   this, &MasonryLayout::onThumbnailReadFinished);
    }
    _model = newModel;
    connect(_model, &QAbstractListModel::dataChanged,
            this, &MasonryLayout::onDataChanged);
    connect(_model, &QAbstractListModel::modelReset,
            this, &MasonryLayout::onModelReset);
    connect(fileModel, &FileListModel::thumbnailReadFinished,
               this, &MasonryLayout::onThumbnailReadFinished);

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

int MasonryLayout::spacing() {
    return _spacing;
}

void MasonryLayout::setSpacing(int newSpacing) {
    if (_spacing == newSpacing)
        return;
    _spacing = newSpacing;
    emit spacingChanged();
}
