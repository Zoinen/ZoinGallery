#include "MasonryLayout.h"
#include "FileListModel.h"
#include "MasonryLayoutQuickSearch.h"
#include "SvgCursor.h"

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlProperty>
#include <QSettings>
#include <QQuickWindow>

static void registerMyQmlTypes() {
    qmlRegisterType<MasonryLayout>("ZoinGallery", 1, 0, "MasonryLayout");
    qmlRegisterType<BrickItem>("ZoinGallery", 1, 0, "BrickItem");
}

Q_COREAPP_STARTUP_FUNCTION(registerMyQmlTypes)


QRectF roundRect(const QRectF &rectF) {
    // Rounding to floor
    return rectF.toRect();
    //return QRect(rectF.x(), rectF.y(), rectF.width(), rectF.height());
}

MasonryLayout::MasonryLayout(QQuickItem *parent)
    : QQuickItem(parent) {
    _targetHeight = 150;//87;//84;//30;
    _visibleStart = -1;
    _visibleEnd = -1;
    _topItem = 0;
    _topItemOffset = 0;
    _contentY = 0;
    _model = nullptr;
    _delegate = nullptr;
    _currentIndex = 0;
    _viewport = nullptr;
    _needScroll = false;
    _dp = 1;
    _spacing = 12; // Should be divisible by 4
    QSettings set;
    _listView = set.value("listView", false).toBool();
    _showTransparentGrid = set.value("showTransparentGrid", true).toBool();
    _imageCount = 0;
    _listRowHeight = 30;

    _currentScrollingMode = false;
    _currentScrollingDirection = -2;

    _paddingLeft = 0;
    _paddingRight = 0;
    _paddingTop = 0;
    _paddingBottom = 0;

    _quickSearch = new MasonryLayoutQuickSearch(this);
}

void MasonryLayout::componentComplete() {
    QQuickItem::componentComplete();

    connect(this, &MasonryLayout::widthChanged,
            this, &MasonryLayout::rewrap);

    connect(this, &MasonryLayout::widthChanged, this, [&] () {
        if (width() > 0 && dynamic_cast<ThumbnailsRequestInterface *>(_model)->isRenderRequested()) {
            startRender();
        }
    });

    connect(this, &MasonryLayout::heightChanged, this, [&] () {
        int newContentY = qMin<qreal>(_contentY, qMax<qreal>(0, contentHeight() - height()));
        if (newContentY != _contentY) {
            setContentYInternal(newContentY);
        }
        else {
            updateProperties();
        }
        updateNeedScroll();
    });
}

QQuickItem *MasonryLayout::itemAt(qreal x, qreal y) const {
    for (int i = 0; i < _bricks.size(); i++) {
        if (_bricks[i].geometry().contains(x, y)) {
            return _bricks[i].item;
        }
    }
    return nullptr;
}

int MasonryLayout::indexAt(qreal x, qreal y) const {
    for (int i = 0; i < _bricks.size(); i++) {
        if (_bricks[i].geometry().contains(x, y)) {
            return i;
        }
    }
    return -1;
}

QRectF MasonryLayout::indexGeometry(int index) const {
    if (index >= 0 && index < _bricks.size()) {
        if (_bricks[index].row == _bricks[_bricks.size() - 1].row) {
            return _bricks[index].geometry().adjusted(0, 0, 0, _paddingBottom);
        }
        if (!_bricks[index].row) {
            return _bricks[index].geometry().adjusted(0, -_paddingTop, 0, 0);
        }
        return _bricks[index].geometry();
    }
    return QRectF();
}

QString MasonryLayout::indexImage(int index) const {
    if (index >= 0 && index < _bricks.size()) {
        QString imageId;
        if (!_bricks[index].image->imageId.isEmpty()) {
            imageId = QString("image://thumbnails/") + _bricks[index].image->imageId;
        }
        return imageId;
    }
    return QString();
}

QString MasonryLayout::indexText(int index) const {
    if (index >= 0 && index < _bricks.size() && _bricks[index].item) {
        return _bricks[index].item->property("text").toString();
    }
    return QString();
}

QSize MasonryLayout::indexOriginalSize(int index) const {
    if (index >= 0 && index < _bricks.size()) {
        return _bricks[index].originalSize.toSize();
    }
    return QSize();
}

QVariantMap MasonryLayout::indexExif(int index) const {
    if (index >= 0 && index < _bricks.size() && _bricks[index].image) {
        return _bricks[index].image->exif;
    }
    return QVariantMap();
}

int MasonryLayout::nextImageIndex(bool forward, bool moveToEnd) {
    int nextIndex = _currentIndex;
    for (int i = _currentIndex + (forward ? 1 : -1); i >= 0 && i < _bricks.size(); i += (forward ? 1 : -1)) {
        if (_bricks[i].image->isImage) {
            nextIndex = i;
            if (!moveToEnd) {
                break;
            }
        }
    }
    return nextIndex;
}

void MasonryLayout::reReadAndDecodeThumbnails() {
    _currentLoadingRow.clear();
    dynamic_cast<ThumbnailsRequestInterface *>(_model)->requestThumbnails(dp(QSizeF(_targetHeight * 3.0 / 2, _targetHeight)), true, _listView ? 16 : 4);
    emit layoutReset();
}

void MasonryLayout::zoomIn() {
    zoom(true);
}

void MasonryLayout::zoomOut() {
    zoom(false);
}

void MasonryLayout::setScrollingMode(bool scrollingMode, int direction) {
    if (_currentScrollingMode == scrollingMode && _currentScrollingDirection == direction) {
        return;
    }

    if (scrollingMode) {
        QString path;
        if (direction == -1) {
            path = ":/resources/ScrollModeUp.svg";
        }
        else if (direction == 1) {
            path = ":/resources/ScrollModeDown.svg";
        }
        else {
            path = ":/resources/ScrollMode.svg";
        }
        SvgCursor::setOverrideCursor(path, dpValue());
    }
    else {
        SvgCursor::setOverrideCursor();
    }
    _currentScrollingMode = scrollingMode;
    _currentScrollingDirection = direction;
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
        _viewport->setX(_paddingLeft);
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

bool MasonryLayout::isEmbedded() const {
    return _model && dynamic_cast<ThumbnailsRequestInterface *>(_model)->rootItem() != nullptr;
}

void MasonryLayout::rewrap() {
//    qDebug() << "rewrap" << width() << _bricks.size();
    int currentIndexOffset = -1;
    if (_currentIndex != -1 && _currentIndex >= _visibleStart && _currentIndex <= _visibleEnd) {
        currentIndexOffset = _contentY - _bricks[_currentIndex].y;
    }

    calcLayout(_bricks, width() - _paddingLeft - _paddingRight, _targetHeight, _spacing, !_listView, _paddingTop, !isEmbedded());
//    qDebug() << "--------------------";
//    for (int i = 0; i < _bricks.size(); i++) {
//        qDebug() << _bricks[i].image->path << _bricks[i].originalSize << _bricks[i].normalizedSize;
//    }


    if (_bricks.size()) {
        setContentHeight(_bricks.last().y + _bricks.last().normalizedSize.height() + _paddingBottom);
    }
    else {
        setContentHeight(0);
    }

    qreal newContentY = _contentY;
    // If selected index is on screen, we keep view relative to it. Otherwise, we keep top item
    if (currentIndexOffset != -1) {
        newContentY = qMax<qreal>(0, qMin<qreal>(_bricks[_currentIndex].y + currentIndexOffset, contentHeight() - height()));
    }
    else {
        if (_topItem < _bricks.size()) {
            newContentY = qMax<qreal>(0, qMin<qreal>(_bricks[_topItem].y - _topItemOffset, contentHeight() - height()));
        }
    }
    if (newContentY != _contentY) {
        setContentYInternal(newContentY);
    }
    else {
        updateProperties();
    }
}

QSizeF scaleToWidthWithSpacing(const QSizeF &size, qreal toWidth, int spacing) {
    qreal aspect = size.width() / size.height();
    return QSizeF(toWidth, (toWidth - spacing) / aspect + spacing);
}

QSizeF scaleToHeightWithSpacing(const QSizeF &size, qreal toHeight, int spacing) {
    qreal aspect = size.width() / size.height();
    return QSizeF((toHeight - spacing) * aspect + spacing, toHeight);
}

qreal MasonryLayout::scaleRow(QList<MasonryBrick> &bricks, int canvasWidth, int rowTargetHeight, int spacing,
                             int lastRowIndex, qreal rowHeight) {
    int i = lastRowIndex;
    int bricksInRow = bricks[i].column + 1;

    if (!rowHeight) {
        qreal totalWidthWithoutSpacing = bricks[i].x + bricks[i].normalizedSize.width() - (bricksInRow * spacing);
        qreal stretchFactor = (canvasWidth - bricksInRow * spacing) / totalWidthWithoutSpacing;
        rowHeight = (rowTargetHeight - spacing) * stretchFactor + spacing;
    }

    for (int rowIndex = i - bricksInRow + 1; rowIndex <= i; rowIndex++) {
        bricks[rowIndex].normalizedSize = scaleToHeightWithSpacing(bricks[rowIndex].normalizedSize -
                                                                   QSize(spacing, spacing), rowHeight, spacing);
        if (bricks[rowIndex].column) {
            bricks[rowIndex].x = bricks[rowIndex - 1].x + bricks[rowIndex - 1].normalizedSize.width();
        }
    }
    return rowHeight;
}

void MasonryLayout::calcLayout(QList<MasonryBrick> &bricks, int canvasWidth, int rowTargetHeight, int spacing,
                               bool lastRowMatchesPrevious, qreal paddingTop, bool growToFillWidth) {
    int currentRow = 0;
    int currentColumn = 0;
    qreal lastX = 0;
    qreal lastY = paddingTop;

    for (int i = 0; i < bricks.size(); i++) {
        if (bricks[i].originalSize.width() == 0 && bricks[i].originalSize.height() == 0) {
            bricks[i].normalizedSize = QSizeF(canvasWidth, rowTargetHeight);
        }
        else if (bricks[i].originalSize.width() == 0 && bricks[i].originalSize.height() != 0) {
            bricks[i].normalizedSize = QSizeF(canvasWidth, bricks[i].originalSize.height());
        }
        else {
            bricks[i].normalizedSize = scaleToHeightWithSpacing(bricks[i].originalSize, rowTargetHeight, spacing);
        }
        bricks[i].row = currentRow;
        bricks[i].column = currentColumn;
        bricks[i].x = lastX;
        bricks[i].y = lastY;

        bool lineBreak = bricks[i].temporaryLineBreak || bricks[i].lineBreak;
        bricks[i].temporaryLineBreak = false;

        // Row is not filled enough yet, growing
        if (lastX + bricks[i].normalizedSize.width() < canvasWidth && !lineBreak) {
            lastX += bricks[i].normalizedSize.width();

            // Last row should have the same height as the previous one, or just fit in width if last height is too much
            if (i == bricks.size() - 1 && currentRow && lastRowMatchesPrevious) {
                for (int rowIndex = i-1; rowIndex >= 0; rowIndex--) {
                    if (bricks[rowIndex].row != currentRow) {
                        scaleRow(bricks, canvasWidth, rowTargetHeight, spacing,
                                 i, bricks[rowIndex].normalizedSize.height());
                        if (bricks[i].x + bricks[i].normalizedSize.width() > canvasWidth) {
                            for (int j = rowIndex + 1; j <= i; j++) {
                                if (bricks[j].column) {
                                    bricks[j].x = bricks[j - 1].x + bricks[j - 1].normalizedSize.width();
                                }
                                bricks[j].normalizedSize = scaleToHeightWithSpacing(bricks[j].originalSize,
                                                                                    rowTargetHeight, spacing);
                            }
                            scaleRow(bricks, canvasWidth, rowTargetHeight, spacing, i);
                        }
                        break;
                    }
                }
            }
        }
        else if (!currentColumn) { // Single-item row
            if (bricks[i].originalSize.width() != 0 && bricks[i].originalSize.height() != 0) {
                bricks[i].normalizedSize = scaleToWidthWithSpacing(bricks[i].originalSize, canvasWidth, spacing);
            }

            currentRow++;
            currentColumn = -1;
            lastX = 0;

            if (i != bricks.size() - 1) {
                lastY += bricks[i].normalizedSize.height();
            }
        } // Can't grow more, expanding current row and advancing to the next one
        else {
            if (currentColumn != bricks[i-1].column + 1 || currentColumn != bricks[i].column) {
                qDebug() << "------------- ALARM ALARM!!!!" << currentColumn << bricks[i-1].column + 1;
            }
            qreal newRowHeight;
            if (growToFillWidth) {
                newRowHeight = scaleRow(bricks, canvasWidth, rowTargetHeight, spacing, i - 1);
            }
            else {
                newRowHeight = rowTargetHeight;
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
    QRectF boundingRect_ = boundingRect().adjusted(0, _contentY - 46, 0, _contentY);

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

            if (_bricks[i].item->property("text").toString() != _bricks[i].image->fileName) {
                _bricks[i].item->setProperty("text", _quickSearch->indexTextWithQuickSearchApplied(i));
            }
            if (_bricks[i].item->property("fullPath").toString() != _bricks[i].image->fullPath()) {
                _bricks[i].item->setProperty("fullPath", _bricks[i].image->fullPath());
            }
            if (_bricks[i].item->property("index").toInt() != i) {
                _bricks[i].item->setProperty("index", i);
            }
            if (_bricks[i].item->property("nestingInfo").toString() != _bricks[i].image->nestingInfo) {
                _bricks[i].item->setProperty("nestingInfo", _bricks[i].image->nestingInfo);
            }

            if (_bricks[i].item->property("folderView").toBool() != _bricks[i].image->folderView()) {
                _bricks[i].item->setProperty("folderView", _bricks[i].image->folderView());
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

            if (_bricks[i].item->property("isImage").toBool() != _bricks[i].image->isImage) {
                _bricks[i].item->setProperty("isImage", _bricks[i].image->isImage);
            }

            bool isDecodedImage = _bricks[i].image->isImage && _bricks[i].image->fullSize.isValid();
            if (_bricks[i].item->property("isDecodedImage").toBool() != isDecodedImage) {
                _bricks[i].item->setProperty("isDecodedImage", isDecodedImage);
            }

            if (_bricks[i].item->property("iconPath").toString() != _bricks[i].image->iconPath) {
                _bricks[i].item->setProperty("iconPath", _bricks[i].image->iconPath);
            }

            if (_bricks[i].item->property("isFolder").toBool() != _bricks[i].image->isFolder) {
                _bricks[i].item->setProperty("isFolder", _bricks[i].image->isFolder);
            }

            bool isPanorama = _bricks[i].image->exif["Panorama"].toString() == "True";
            if (_bricks[i].item->property("isPanorama").toBool() != isPanorama) {
                _bricks[i].item->setProperty("isPanorama", isPanorama);
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
    updateNeedScroll();
}

void MasonryLayout::onDataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight, const QVector<int> &roles) {
    if (!isEmbedded() && FileListModel::itemFromIndex(topLeft.parent()) != dynamic_cast<ThumbnailsRequestInterface *>(_model)->rootItem()) {
        return;
    }
    int index = topLeft.row();
    if (index < _bricks.size()) {
        if (roles.contains(FileListModel::ImageIdRole)) {
            if (_bricks[index].item) {
               // qDebug() << "upd imageid" << _bricks[index].image->fullPath();
                _bricks[index].item->setProperty("imageId", QString("image://thumbnails/") + _bricks[index].image->imageId);
            }
        }
        if (roles.contains(FileListModel::ImageFullSizeRole)) {
            pushToCurrentRow(index);
        }
        if (roles.contains(FileListModel::FolderViewRole)) {
            if (_bricks[index].item) {
                _bricks[index].item->setProperty("folderView", _bricks[index].image->folderView());

            }
            QSize folderViewSize = _listView ? QSize(0, 0) : GridView_Folder.toSize();
            if (_bricks[index].originalSize != folderViewSize) {
                _bricks[index].originalSize = folderViewSize;
                rewrap();
                updateProperties();
            }
        }
        if (roles.contains(FileListModel::ExifRole)) {
            bool isPanorama = _bricks[index].image->exif["Panorama"].toString() == "True";
            if (_bricks[index].item && _bricks[index].item->property("isPanorama").toBool() != isPanorama) {
                _bricks[index].item->setProperty("isPanorama", isPanorama);
            }
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

    calcLayout(_currentLoadingRow, width() - _paddingLeft - _paddingRight, _targetHeight, _spacing, !_listView, 0, !isEmbedded());
    if (_currentLoadingRow.last().row > 0 || flushMode) {
//        qDebug() << "//// pushing" << _currentLoadingRow.first().globalIndex << flushMode << _currentLoadingRow.size();
//        qDebug() << "REWRAP";
        QList<int> requestsIndexes;
        for (int i = 0; i < _currentLoadingRow.size(); i++) {
            if (_currentLoadingRow[i].row != _currentLoadingRow.last().row || flushMode) {
                int updIndex = _currentLoadingRow[i].globalIndex;
                if (_bricks[updIndex].image && _bricks[updIndex].image->fullSize.isValid()) {
                    _bricks[updIndex].originalSize = _bricks[updIndex].image->fullSize;
                    // When pushing single item that fills the whole row we need to add a line break
                    if (!flushMode && !_bricks[updIndex].column && i == _currentLoadingRow.size() - 2) {
                        _bricks[updIndex].temporaryLineBreak = true;
                    }
                    requestsIndexes.append(updIndex);
                }
            }
            else {
//                int forceNewLineFrom = _currentLoadingRow[i].globalIndex;
//                _bricks[forceNewLineFrom].forceNewLine = true;
                for (int delIndex = 0; delIndex < i; delIndex++) {
                    _currentLoadingRow.removeFirst();
                }
                break;
            }
        }
        if (flushMode) {
            _currentLoadingRow.clear();
        }
        rewrap();
        // TODO: Possible duplicate call, rewrap already updates properties
        updateProperties();

        QList<ImageReadRequest> requests;
        for (int i = 0; i < requestsIndexes.size(); i++) {
            int index = requestsIndexes[i];
            QSize thumbnailSize = _bricks[index].thumbnailSize(spacing());
            thumbnailSize = dp(thumbnailSize);
            requests.append(ImageReadRequest(_bricks[index].image->fullPath(), thumbnailSize));
        }
        dynamic_cast<ThumbnailsRequestInterface *>(_model)->addRequestThumbnails(requests);
    }
}

void MasonryLayout::onThumbnailReadFinished(ImageFile *root) {
    if (_bricks.count() && root == dynamic_cast<ThumbnailsRequestInterface *>(_model)->rootItem() &&
        _currentLoadingRow.size()) {
        pushToCurrentRow(_bricks.count());
    }
}

void MasonryLayout::onModelReset() {
    _currentLoadingRow.clear();
    _visibleStart = -1;
    _visibleEnd = -1;
    _topItem = 0;
    _bricks.clear();
    setCurrentIndex(_topItem);
    for (auto it = _usedBrickItems.begin(); it != _usedBrickItems.end(); ++it) {
        (*it)->setVisible(false);
    }
    _freeBrickItems.unite(_usedBrickItems);
    _usedBrickItems.clear();
    for (auto it = _freeBrickItems.begin(); it != _freeBrickItems.end(); ++it) {
        (*it)->setProperty("folderView", false);
    }

    bool needToRender = false;
    for (int i = 0; i < _model->rowCount(); i++) {
        ImageFile *imageFile = FileListModel::itemFromIndex(_model->index(i, 0));
        QSize imgSize = imageFile->fullSize;
        bool lineBreak = false;
        if (imgSize.isEmpty()) {
            if (imageFile->isFolder && _listView) {
                lineBreak = true;
                imgSize = QSize(0, imageFile->folderView() ? 0 : listRowHeight());
            }
            else {
                imgSize = GridView_Folder.toSize();
            }
        }
        if (imageFile->imageId.isEmpty() || imageFile->isCachedThumbnail) {
            needToRender = true;
        }
        _bricks.append(MasonryBrick(imageFile, imgSize, lineBreak));
    }
    emit modelChanged();
    rewrap();
    if (_viewport) {
        _viewport->setY(-_contentY);
    }

    if (width() > 0 && (needToRender || dynamic_cast<ThumbnailsRequestInterface *>(_model)->isRenderRequested())) {
        startRender();
    }
    else if (needToRender) {
        dynamic_cast<ThumbnailsRequestInterface *>(_model)->requestRender();
    }

    _imageCount = 0;
    for (int i = 0; i < _bricks.size(); i++) {
        if (_bricks[i].image->isImage) {
            _imageCount++;
        }
    }
    emit imageCountChanged();

    emit countChanged();

    emit currentIndexChanged();
}

void MasonryLayout::zoom(bool in) {
    const int smallestHeight = 30;
    const int largestHeight = 500;

    QList<MasonryBrick> bricks;
    QSize minSize = QSize(_targetHeight * GridView_Folder.width() / GridView_Folder.height(), _targetHeight);
    for (int i = 0; i <= ((width() - _paddingLeft - _paddingRight) / minSize.width()) * 2; i++) {
        bricks.append(MasonryBrick(GridView_Folder.width(), GridView_Folder.height()));
    }
    int columns = -1;
    int newTargetHeight = -1;
    int increment = in ? 1 : -1;

    int targetHeightRangeStart = -1;
    int targetHeightRangeEnd = -1;

    for (int targetHeight = _targetHeight - _paddingBottom; targetHeight >= smallestHeight && targetHeight <= largestHeight; targetHeight += increment) {
        calcLayout(bricks, width() - _paddingLeft - _paddingRight, targetHeight, _spacing, !_listView, _paddingTop);
        for (int i = 0; i < bricks.size(); i++) {
            if (bricks[i].row && i) {
                if (columns == -1) {
                    columns = bricks[i - 1].column;
                }
                else if (bricks[i - 1].column != columns) {
                    columns = bricks[i - 1].column;
                    if (targetHeightRangeStart == -1) {
                        targetHeightRangeStart = targetHeight;
                    }
                    else {
                        targetHeightRangeEnd = targetHeight;
                        newTargetHeight = (targetHeightRangeStart + targetHeightRangeEnd) / 2;
                    }
                }
                break;
            }
        }
        if (newTargetHeight != -1) {
            break;
        }
    }
    if (newTargetHeight != -1 || (_targetHeight != largestHeight && in) || (_targetHeight != smallestHeight && !in)) {
        setTargetHeight((newTargetHeight != -1 ? newTargetHeight : (in ? largestHeight : smallestHeight)) + _paddingBottom);
        reReadAndDecodeThumbnails();
    }
}

void MasonryLayout::updateNeedScroll() {
    if (!_bricks.size()) {
        return;
    }
    // Scroll is not needed for embedded layout
    if (isEmbedded()) {
        return;
    }

    bool newNeedScroll = _contentHeight > height();
    if (newNeedScroll != _needScroll && height() > 0) {
        QList<MasonryBrick> bricks = _bricks;
        // TODO: Scrollbar height is hardcoded here
        calcLayout(bricks, width() - _paddingLeft - _paddingRight + (newNeedScroll ? 0 : 16), _targetHeight, _spacing,
                   !_listView, _paddingTop);

        int newContentHeight = (bricks.last().y + bricks.last().normalizedSize.height() + _paddingBottom);
        newNeedScroll = newContentHeight > height();
        if (newNeedScroll != _needScroll && newContentHeight > 0) {
            _needScroll = newNeedScroll;
            emit needScrollChanged();
        }
    }
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

QSize MasonryLayout::dp(QSizeF value) {
    return QSize(dp(value.width()), dp(value.height()));
}

qreal MasonryLayout::dp(qreal value) {
    return qRound(value * dpValue());
}

qreal MasonryLayout::dpValue() {
    QWindow *window_ = window();
    if (window_) {
        _dp = window_->devicePixelRatio();
    }
    return _dp;
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

qreal MasonryLayout::contentY() const {
    return _contentY;
}

void MasonryLayout::setContentY(qreal newContentY) {
    newContentY = qMax(0.0, qMin<qreal, qreal>(_contentHeight - height(), newContentY));
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

void MasonryLayout::setContentYInternal(qreal newContentY) {
    static int depth = 0;
    depth++;
    if (qFuzzyCompare(_contentY, newContentY) || depth > 1) {
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

void MasonryLayout::startRender() {
    if (!_targetHeight) {
        return;
    }
    dynamic_cast<ThumbnailsRequestInterface *>(_model)->renderRequestComplete();
    QList<MasonryBrick> bricks;
    QSize minSize = QSize(_targetHeight * GridView_Folder.width() / GridView_Folder.height(), _targetHeight);
    for (int i = 0; i <= ((width() - _paddingLeft - _paddingRight) / minSize.width()) * 2; i++) {
        bricks.append(MasonryBrick(GridView_Folder.width(), GridView_Folder.height()));
    }
    calcLayout(bricks, width() - _paddingLeft - _paddingRight, _targetHeight, _spacing, !_listView, 0, !isEmbedded());
    if (bricks.size()) {
        QSizeF projectedSize = bricks.first().normalizedSize;
        dynamic_cast<ThumbnailsRequestInterface *>(_model)->requestThumbnails(dp(projectedSize), false, _listView ? 16: 4);
    }
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
    : originalSize(QSize(width, height)), temporaryLineBreak(false), lineBreak(false),
    x(0), y(0), row(0), column(0), item(nullptr), image(nullptr), globalIndex(-1) {
}

MasonryLayout::MasonryBrick::MasonryBrick(ImageFile *image_, QSizeF originalSize_, bool lineBreak_)
    : originalSize(originalSize_), temporaryLineBreak(false), lineBreak(lineBreak_),
    x(0), y(0), row(0), column(0), item(nullptr), image(image_), globalIndex(-1) {
}

QRectF MasonryLayout::MasonryBrick::geometry() const {
    return QRectF(QPointF(x, y), normalizedSize);
}

QSize MasonryLayout::MasonryBrick::thumbnailSize(int spacing) const {
    return roundRect(geometry()).toRect().size() - QSize(spacing, spacing);
}

QAbstractItemModel *MasonryLayout::model() const {
    return _model;
}

void MasonryLayout::setModel(QAbstractItemModel *newModel) {
    if (_model) {
        disconnect(_model, &QAbstractItemModel::dataChanged,
                   this, &MasonryLayout::onDataChanged);
        disconnect(_model, &QAbstractItemModel::modelReset,
                   this, &MasonryLayout::onModelReset);
        disconnect(_model, SIGNAL(thumbnailReadFinished(ImageFile*)),
                   this, SLOT(onThumbnailReadFinished(ImageFile*)));
    }
    _model = newModel;
    connect(_model, &QAbstractItemModel::dataChanged,
            this, &MasonryLayout::onDataChanged);
    connect(_model, &QAbstractItemModel::modelReset,
            this, &MasonryLayout::onModelReset);
    connect(_model, SIGNAL(thumbnailReadFinished(ImageFile*)),
               this, SLOT(onThumbnailReadFinished(ImageFile*)));

    onModelReset();
}

int MasonryLayout::currentIndex() const {
    return _currentIndex;
}

void MasonryLayout::setCurrentIndex(int newCurrentIndex) {
    newCurrentIndex = qMin(qMax(0, newCurrentIndex), _model->rowCount() - 1);
    if (_currentIndex == newCurrentIndex) {
        return;
    }
    _currentIndex = newCurrentIndex;
    emit currentIndexChanged();

    _currentImageIndex = 0;
    for (int i = 0; i < _currentIndex; i++) {
        if (_bricks[i].image->isImage) {
            _currentImageIndex++;
        }
    }
    emit currentImageIndexChanged();

    if (!_quickSearch->mask().isEmpty()) {
        _quickSearch->updateVisuals();
    }
}

int MasonryLayout::spacing() const {
    return _spacing;
}

void MasonryLayout::setSpacing(int newSpacing) {
    if (_spacing == newSpacing)
        return;
    _spacing = newSpacing;
    emit spacingChanged();
}

QQuickItem *MasonryLayout::currentItem() const {
    if (_currentIndex >= 0 && _currentIndex < _bricks.size()) {
        return _bricks[_currentIndex].item;
    }
    return nullptr;
}

int MasonryLayout::count() const {
    if (_model) {
        return _model->rowCount();
    }
    return 0;
}

MasonryLayoutQuickSearch *MasonryLayout::quickSearch() const {
    return _quickSearch;
}

bool MasonryLayout::needScroll() const {
    return _needScroll;
}

bool MasonryLayout::listView() const {
    return _listView;
}

void MasonryLayout::setListView(bool isListView) {
    if (_listView == isListView) {
        return;
    }
    _listView = isListView;
    QSettings set;
    set.setValue("listView", isListView);

    for (int i = 0; i < _bricks.size(); i++) {
        if (_bricks[i].image->isFolder) {
            if (!_listView) {
                _bricks[i].originalSize = GridView_Folder;
                _bricks[i].lineBreak = false;
            }
            else {
                _bricks[i].originalSize = QSize(0, _bricks[i].image->folderView() ? 0 : listRowHeight());
                _bricks[i].lineBreak = true;
            }
        }
    }
    rewrap();
    updateProperties();

    emit listViewChanged();
}

int MasonryLayout::imageCount() const {
    return _imageCount;
}

int MasonryLayout::currentImageIndex() const {
    return _currentImageIndex;
}

void MasonryLayout::setCurrentImageIndex(int newCurrentImageIndex) {
    if (_currentImageIndex == newCurrentImageIndex) {
        return;
    }
    for (int i = 0, imageIndex = 0; i < _bricks.size(); i++) {
        if (_bricks[i].image->isImage) {
            if (imageIndex == newCurrentImageIndex) {
                setCurrentIndex(i);
                break;
            }
            imageIndex++;
        }
    }
}

bool MasonryLayout::showTransparentGrid() const {
    return _showTransparentGrid;
}

void MasonryLayout::setShowTransparentGrid(bool newShowTransparentGrid) {
    if (_showTransparentGrid == newShowTransparentGrid) {
        return;
    }

    _showTransparentGrid = newShowTransparentGrid;

    QSettings set;
    set.setValue("showTransparentGrid", _showTransparentGrid);

    emit showTransparentGridChanged();
}

qreal MasonryLayout::paddingLeft() const {
    return _paddingLeft;
}

void MasonryLayout::setPaddingLeft(qreal newPaddingLeft) {
    if (qFuzzyCompare(_paddingLeft, newPaddingLeft))
        return;
    _paddingLeft = newPaddingLeft;
    if (_viewport) {
        _viewport->setX(_paddingLeft);
    }
    rewrap();
    updateProperties();
    emit paddingLeftChanged();
}

qreal MasonryLayout::paddingRight() const {
    return _paddingRight;
}

void MasonryLayout::setPaddingRight(qreal newPaddingRight) {
    if (qFuzzyCompare(_paddingRight, newPaddingRight))
        return;
    _paddingRight = newPaddingRight;
    rewrap();
    updateProperties();
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
    rewrap();
    updateProperties();
    emit paddingTopChanged();
}

qreal MasonryLayout::paddingBottom() const {
    return _paddingBottom;
}

void MasonryLayout::setPaddingBottom(qreal newPaddingBottom) {
    if (qFuzzyCompare(_paddingBottom, newPaddingBottom))
        return;
    _paddingBottom = newPaddingBottom;
    rewrap();
    updateProperties();
    emit paddingBottomChanged();
}

qreal MasonryLayout::width() const {
    return qIsInf(QQuickItem::width()) ? 0 : QQuickItem::width();
}

int MasonryLayout::listRowHeight() const {
    return _listRowHeight;
}

QVariantList MasonryLayout::currentImageExif() const {
    if (_currentIndex >= 0 && _currentIndex < _bricks.size()) {
        return _bricks[_currentIndex].image->exifList();
    }
    return QVariantList();
}
