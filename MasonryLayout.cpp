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
    QSettings set;
    _targetHeight = set.value("targetHeight", 150).toInt();
    // qDebug() << "ZZ TARGET HEIGHT" << _targetHeight;
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
    _listView = set.value("listView", true).toBool();
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

QString MasonryLayout::indexImageIdUrl(int index) const {
    if (index >= 0 && index < _bricks.size()) {
        QString imageIdUrl;
        if (!_bricks[index].image->imageIdUrl().isEmpty()) {
            imageIdUrl = _bricks[index].image->imageIdUrl();
        }
        return imageIdUrl;
    }
    return QString();
}

QString MasonryLayout::indexText(int index) const {
    if (index >= 0 && index < _bricks.size()) {
        return _bricks[index].image->fileName();
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
        return _bricks[index].image->info().exif;
    }
    return QVariantMap();
}

int MasonryLayout::nextImageIndex(bool forward, bool moveToEnd) {
    int nextIndex = _currentIndex;
    for (int i = _currentIndex + (forward ? 1 : -1); i >= 0 && i < _bricks.size(); i += (forward ? 1 : -1)) {
        if (_bricks[i].image->isImage()) {
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
    qDebug() << "MasonryLayout::reReadAndDecodeThumbnails" << isEmbedded();
    if (!isEmbedded() && _model) {
        dynamic_cast<ThumbnailsRequestInterface *>(_model)->cancelAllDecodeRunners();
    }

    QList<ImageDecodeRequest> requests;
    for (const MasonryBrick &brick : _bricks) {
        if (brick.image && brick.image->isImage() && brick.image->fullSize().isValid()) {
            QSize thumbnailSize = brick.thumbnailSize(spacing());
            thumbnailSize = dp(thumbnailSize);
            requests.append(ImageDecodeRequest{
                .info = brick.image->info(),
                .targetSize = thumbnailSize,
                .viewerRequest = false,
                .checkCache = brick.image->info().isCached
            });
            qDebug() << "ZZ REQUEST" << brick.image->fileName() << thumbnailSize << brick.thumbnailSize(0) << brick.image->fullSize() << _spacing;
        }
    }
    emit layoutReset();
    if (_model) {
        dynamic_cast<ThumbnailsRequestInterface *>(_model)->decodeImages(requests);
    }
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
        import QtQuick

        Item {
        }
        )QML", QUrl());
        if (component.status() != QQmlComponent::Ready) {
            qDebug() << "Error in component:" << component.status() << component.errors();
        }

        _viewport = qobject_cast<QQuickItem*>(component.create(QQmlEngine::contextForObject(this)));
        _viewport->setParentItem(this);
        _viewport->setParent(this);
        _viewport->setX(_paddingLeft);
        emit viewportChanged();
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
   // qDebug() << "rewrap" << width() << _bricks.size();
   //  if (width() <= 0) {
   //     qDebug() << "no rewrap, zero width";
   //     return;
   // }
    int currentIndexOffset = -1;
    if (_currentIndex != -1 && _currentIndex >= _visibleStart && _currentIndex <= _visibleEnd) {
        currentIndexOffset = _contentY - _bricks[_currentIndex].y;
    }

    calcLayout(_bricks, width() - _paddingLeft - _paddingRight, _targetHeight, _spacing, !_listView, _paddingTop, layoutMode());
   // qDebug() << "--------------------";
   // for (int i = 0; i < _bricks.size(); i++) {
   //     qDebug() << _bricks[i].image->fullPath() << _bricks[i].originalSize << _bricks[i].normalizedSize;
   // }


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
    return QSizeF(qMax(0.0, toWidth), qMax(0.0, (toWidth - spacing) / aspect + spacing));
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

MasonryLayout::CalcLayoutMode MasonryLayout::layoutMode() const {
    return !isEmbedded() ? CalcLayoutMasonry : _listView ? CalcLayoutSingleRow : CalcLayoutGrid;
}

QRectF fitRectInCell(const QRectF &cellRect, const QSizeF &originalSize) {
    QSizeF scaledSize;
    if (originalSize.width() / originalSize.height() > cellRect.width() / cellRect.height()) {
        // Scale based on cell's width
        scaledSize.setWidth(cellRect.width());
        scaledSize.setHeight(cellRect.width() * originalSize.height() / originalSize.width());
    }
    else {
        // Scale based on cell's height
        scaledSize.setWidth(cellRect.height() * originalSize.width() / originalSize.height());
        scaledSize.setHeight(cellRect.height());
    }

    QPointF offset((cellRect.width() - scaledSize.width()) / 2.0, (cellRect.height() - scaledSize.height()) / 2.0);
    return QRectF(cellRect.topLeft() + offset, scaledSize);
}


void MasonryLayout::calcGridLayout(QList<MasonryBrick> &bricks, int canvasWidth, int rowTargetHeight, int spacing,
                                   bool lastRowMatchesPrevious, qreal paddingTop) {
    int dimensions = canvasWidth < 80 ? 1 :
                     canvasWidth < 150 ? 2 :
                     canvasWidth < 300 ? 3 : 4;
    int rows = dimensions;
    int columns = dimensions;
    qreal cellWidth = (canvasWidth - spacing * (columns + 1)) / qreal(columns);
    qreal cellHeight = (rowTargetHeight - spacing * (rows + 1)) / qreal(rows);
    for (int i = 0; i < bricks.size(); i++) {
        if (i >= rows * columns) {
            bricks[i].normalizedSize = QSizeF();
            bricks[i].row = 0;
            bricks[i].column = 0;
            continue;
        }
        int currentRow = i / columns;
        int currentColumn = i % columns;

        bricks[i].row = currentRow;
        bricks[i].column = currentColumn;
        QRectF cellRect(currentColumn * cellWidth + spacing * (currentColumn + 1), currentRow * cellHeight + spacing * (currentRow + 1),
                        cellWidth, cellHeight);
        QRectF imageRect = fitRectInCell(cellRect, bricks[i].originalSize);
        bricks[i].x = imageRect.x();
        bricks[i].y = imageRect.y();
        bricks[i].normalizedSize = imageRect.size();
    }
}

void MasonryLayout::calcLayout(QList<MasonryBrick> &bricks, int canvasWidth, int rowTargetHeight, int spacing,
                               bool lastRowMatchesPrevious, qreal paddingTop, CalcLayoutMode layoutMode) {
    if (layoutMode == CalcLayoutGrid) {
        calcGridLayout(bricks, canvasWidth, rowTargetHeight, spacing, lastRowMatchesPrevious, paddingTop);
        return;
    }

    canvasWidth = qMax(0, canvasWidth);
    int currentRow = 0;
    int currentColumn = 0;
    qreal lastX = 0;
    qreal lastY = paddingTop;
    // qDebug() << "REWRAP -----------------";

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

        // qDebug() << i << bricks[i].row << bricks[i].column;
        bool lineBreak = false;
        if (i) {
            lineBreak = bricks[i - 1].temporaryLineBreakAfter || bricks[i - 1].lineBreakAfter;
            bricks[i - 1].temporaryLineBreakAfter = false;
        }

        // Row is not filled enough yet, growing
        if (lastX + bricks[i].normalizedSize.width() < canvasWidth && (!lineBreak || !bricks[i].column)
            || layoutMode == CalcLayoutSingleRow) {
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
            if (layoutMode == CalcLayoutMasonry) {
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

    // qDebug() << "-------------";
    int currentRow = -1;
    for (int i = 0; i < _bricks.size(); i++) {
        if (currentRow != _bricks[i].row || !_bricks[i].normalizedSize.isValid()) {
            currentRow = _bricks[i].row;

            if (_bricks[i].normalizedSize.isValid() && _bricks[i].geometry().intersects(boundingRect_)) {
                // qDebug() << "ZZ OK VIS" << i << _bricks[i].row << _bricks[i].geometry() << isEmbedded();

                if (newVisibleStart == -1) {
                    newVisibleStart = i;
                }
            }
            else {
                // qDebug() << "ZZ ELSE" << newVisibleStart << newVisibleEnd << i;
                if (newVisibleStart != -1 && newVisibleEnd == -1) {
                    newVisibleEnd = qMax(0, i - 1);
                    break;
                }
            }
        }
    }
    if (newVisibleEnd == -1) {
        // qDebug() << "ZZ WELL USE LAST";
        newVisibleEnd = _bricks.size() - 1;
    }
    // if (isEmbedded()) {
    //     qDebug() << "update props" << isEmbedded() << _bricks.size() << newVisibleStart << newVisibleEnd;
    // }

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
                _bricks[i].item->setProperty("model", QVariant::fromValue(_bricks[i].image));
            }
            _bricks[i].item->setRowColumn(_bricks[i].row, _bricks[i].column);

            if (roundRect(_bricks[i].item->geometry()) != roundRect(_bricks[i].geometry())) {
                // if (isEmbedded()) {
                //     qDebug() << "ZZ UPD GEOM for" << i << _bricks[i].geometry() << newVisibleStart << newVisibleEnd << isEmbedded();
                // }
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
        }
    }

    _visibleStart = newVisibleStart;
    _visibleEnd = newVisibleEnd;

    for (BrickItem *item : itemsToHide) {
        item->setVisible(false);
        item->setProperty("model", QVariant());
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
    int indexTo = bottomRight.row();
    int changedIndexes = indexTo - index;
    if (index >= 0 && index < _bricks.size() && indexTo >= 0 && indexTo <= _bricks.size()) {
        if (roles.contains(FileListModel::ImageFullSizeRole)) {
            if (isEmbedded() || (changedIndexes > 1 && !_currentLoadingRow.size())) {
                for (int i = index; i <= indexTo; i++) {
                    if (_bricks[i].image && _bricks[i].image->fullSize().isValid()) {
                        _bricks[i].originalSize = _bricks[i].image->fullSize();
                    }
                }
                rewrap();

                if (!isEmbedded()) {
                    QList<ImageDecodeRequest> requests;
                    qDebug() << "-------------";
                    for (int i = index; i <= indexTo; i++) {
                        if (_bricks[i].image && _bricks[i].image->fullSize().isValid()) {
                            qDebug() << "decode" << requests.last().info.path << requests.last().targetSize;
                            requests.append(ImageDecodeRequest{
                                .info = _bricks[i].image->info(),
                                .targetSize = dp(_bricks[i].thumbnailSize(spacing())),
                                .viewerRequest = false,
                                .checkCache = _bricks[i].image->info().isCached
                            });
                        }
                    }
                    dynamic_cast<ThumbnailsRequestInterface *>(_model)->decodeImages(requests);
                }
            }
            else {
                // qDebug() << "ZZ LOADING ROW" << _currentLoadingRow.size();
                for (int i = index; i <= indexTo; i++) {
                    // TODO: this all comes too early when size is 3x2, fix somehow
                    pushToCurrentRow(i);
                }
            }
        }
        if (roles.contains(FileListModel::FolderViewRole)) {
            QSize folderViewSize = _listView ? QSize(0, 0) : GridView_Folder.toSize();
            if (_bricks[index].originalSize != folderViewSize) {
                _bricks[index].originalSize = folderViewSize;
                rewrap();
            }
        }
        if (roles.contains(FileListModel::TimeToFlushRole)) {
            onThumbnailReadFinished();
        }
    }
}
#include <QThread>
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
               // qDebug() << "adding index" << i << "from" << index << i << lastIndex;
                _currentLoadingRow.insert(indexToInsert, _bricks[i]);
                _currentLoadingRow[indexToInsert].globalIndex = i;
            }
            else {
                break;
            }
        }
    }
    if (!flushMode) {
        _currentLoadingRow.append(MasonryBrick {
            .originalSize = _bricks[index].image->fullSize(),
        });
                                  // (_bricks[index].image->fullSize().width(), _bricks[index].image->fullSize().height()));
        _currentLoadingRow.last().globalIndex = index;
    }

    // qDebug() << "==";
    // for (int k = 0; k < _currentLoadingRow.count(); k++) {
    //     qDebug() << "In row" << _currentLoadingRow[k].globalIndex;
    // }
    // qDebug() << "==";

    calcLayout(_currentLoadingRow, width() - _paddingLeft - _paddingRight, _targetHeight, _spacing, !_listView, 0, layoutMode());
    if (_currentLoadingRow.last().row > 0 || flushMode) {
        // qDebug() << "//// pushing" << _currentLoadingRow.first().globalIndex << "-" << _currentLoadingRow.last().globalIndex << flushMode << _currentLoadingRow.size();
        // qDebug() << "REWRAP";
        QList<int> requestsIndexes;
        for (int i = 0; i < _currentLoadingRow.size(); i++) {
            // qDebug() << "i" << i << _currentLoadingRow[i].globalIndex << _currentLoadingRow[i].row << _currentLoadingRow.last().row;
            if (_currentLoadingRow[i].row != _currentLoadingRow.last().row || flushMode) {
                int updIndex = _currentLoadingRow[i].globalIndex;
                if (_bricks[updIndex].image && _bricks[updIndex].image->fullSize().isValid()) {
                    // qDebug() << "Full size is valid, updating" << updIndex;
                    _bricks[updIndex].originalSize = _bricks[updIndex].image->fullSize();
                    if (_bricks[updIndex].image->isImage()) {
                        _bricks[updIndex].image->setIsShowAsImage(true);
                    }
                    // When pushing single item that fills the whole row we need to add a line break
                    if (!flushMode && !_bricks[updIndex].column && i == _currentLoadingRow.size() - 2) {
                        // qDebug() << "Last in row, forcing line break" << updIndex << "at" << i;
                        _bricks[updIndex].temporaryLineBreakAfter = true;

                        for (int delIndex = 0; delIndex <= i; delIndex++) {
                            _currentLoadingRow.removeFirst();
                        }
                        requestsIndexes.append(updIndex);
                        break;
                    }
                    requestsIndexes.append(updIndex);
                }
            }
            else {
                if (i) {
                    int updIndex = _currentLoadingRow[i - 1].globalIndex;
                    if (updIndex >= 0) {
                        // qDebug() << "Second line break source" << updIndex << ", removing 0 to" << i - 1;
                        _bricks[updIndex].temporaryLineBreakAfter = true;
                    }
                }

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

        QList<ImageDecodeRequest> requests;
        // qDebug() << "-------------- 2" << _currentLoadingRow.last().row << flushMode;
        for (int i = 0; i < requestsIndexes.size(); i++) {
            int index = requestsIndexes[i];
            requests.append(ImageDecodeRequest{
                .info = _bricks[index].image->info(),
                .targetSize = dp(_bricks[index].thumbnailSize(spacing())),
                .viewerRequest = false,
                .checkCache = _bricks[index].image->info().isCached
            });
            // qDebug() << "decode2 " << requests.last().info.path << requests.last().targetSize;
        }
        dynamic_cast<ThumbnailsRequestInterface *>(_model)->decodeImages(requests);
    }
}

void MasonryLayout::onThumbnailReadFinished() {
    if (_bricks.count() && _currentLoadingRow.size()) {
        pushToCurrentRow(_bricks.count());
    }
}

void MasonryLayout::onModelAboutToBeReset() {
    _currentLoadingRow.clear();
    _visibleStart = -1;
    _visibleEnd = -1;
    _topItem = 0;
    _bricks.clear();
    if (_model) {
        setCurrentIndex(_topItem);
    }
    for (auto it = _usedBrickItems.begin(); it != _usedBrickItems.end(); ++it) {
        (*it)->setVisible(false);
        (*it)->setProperty("model", QVariant());
    }
    _freeBrickItems.unite(_usedBrickItems);
    _usedBrickItems.clear();
}

void MasonryLayout::onModelReset() {
    bool needToRender = false;
    if (_model) {
        for (int i = 0; i < _model->rowCount(); i++) {
            ImageFile *imageFile = FileListModel::itemFromIndex(_model->index(i, 0));
            QSize imgSize = imageFile->fullSize();
            bool lineBreakAfter = false;
            if (imgSize.isEmpty()) {
                if (imageFile->isFolder() && _listView) {
                    lineBreakAfter = true;
                    imgSize = QSize(0, imageFile->folderView() ? 0 : listRowHeight());
                }
                else {
                    imgSize = GridView_Folder.toSize();
                }
            }
            if (imageFile->imageIdUrl().isEmpty() || imageFile->isCachedThumbnail()) {
                needToRender = true;
            }
            _bricks.append(MasonryBrick {
                .originalSize = imgSize,
                .lineBreakAfter = lineBreakAfter,
                .image = imageFile,
            });
        }
    }
    emit modelChanged();
    rewrap();
    if (_viewport) {
        _viewport->setY(-_contentY);
    }

    _imageCount = 0;
    for (int i = 0; i < _bricks.size(); i++) {
        if (_bricks[i].image->isImage()) {
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
        bricks.append(MasonryBrick {
            .originalSize = GridView_Folder.toSize(),
        });
    }
    int columns = -1;
    int newTargetHeight = -1;
    int increment = in ? 1 : -1;

    int targetHeightRangeStart = -1;
    int targetHeightRangeEnd = -1;

    for (int targetHeight = _targetHeight - _paddingBottom; targetHeight >= smallestHeight && targetHeight <= largestHeight; targetHeight += increment) {
        calcLayout(bricks, width() - _paddingLeft - _paddingRight, targetHeight, _spacing, !_listView, _paddingTop, layoutMode());
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
                   !_listView, _paddingTop, layoutMode());

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
    // qDebug() << "ZZ SET TARGET HEIGHT" << newTargetHeight;
    if (_targetHeight == newTargetHeight) {
        return;
    }
    _targetHeight = newTargetHeight;
    if (!isEmbedded()) {
        QSettings set;
        set.setValue("targetHeight", _targetHeight);
    }
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
        disconnect(_model, &QAbstractItemModel::modelAboutToBeReset,
                   this, &MasonryLayout::onModelAboutToBeReset);
        disconnect(_model, &QAbstractItemModel::modelReset,
                   this, &MasonryLayout::onModelReset);
    }
    _model = newModel;
    if (_model) {
        connect(_model, &QAbstractItemModel::dataChanged,
                this, &MasonryLayout::onDataChanged);
        connect(_model, &QAbstractItemModel::modelAboutToBeReset,
                this, &MasonryLayout::onModelAboutToBeReset);
        connect(_model, &QAbstractItemModel::modelReset,
                this, &MasonryLayout::onModelReset);
    }

    onModelAboutToBeReset();
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
        if (_bricks[i].image->isImage()) {
            _currentImageIndex++;
        }
    }
    emit currentImageIndexChanged();

    if (!_quickSearch->mask().isEmpty()) {
        _quickSearch->updateItemsText();
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
        if (_bricks[i].image->isFolder()) {
            if (!_listView) {
                _bricks[i].originalSize = GridView_Folder;
                _bricks[i].lineBreakAfter = false;
            }
            else {
                _bricks[i].originalSize = QSize(0, _bricks[i].image->folderView() ? 0 : listRowHeight());
                _bricks[i].lineBreakAfter = true;
            }
        }
    }
    rewrap();

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
        if (_bricks[i].image->isImage()) {
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
    emit paddingBottomChanged();
}

qreal MasonryLayout::width() const {
    return qMax(0.0, qIsInf(QQuickItem::width()) ? 0 : QQuickItem::width());
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

QQuickItem *MasonryLayout::viewport() const {
    return _viewport;
}
