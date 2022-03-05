#include "MasonryLayout.h"

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlProperty>

static void registerMyQmlTypes() {
    qmlRegisterType<MasonryLayout>("ZoinGallery", 1, 0, "MasonryLayout");
    qmlRegisterType<BrickItem>("ZoinGallery", 1, 0, "BrickItem");
}

Q_COREAPP_STARTUP_FUNCTION(registerMyQmlTypes)


MasonryLayout::MasonryLayout(QQuickItem *parent)
    : QQuickItem(parent) {
    _targetHeight = 100;
}

void MasonryLayout::componentComplete() {
    QQuickItem::componentComplete();

    _pics = {
        {7952, 5304},
        {7952, 5304},
        {5304, 7952},
        {7952, 5304},
        {7952, 5304},
        {7952, 5304},
        {5304, 7952},
        {5304, 7952},
        {7952, 5304},
        {7952, 5304},
        {7952, 530},
        {7952, 5304},
        {7952, 5304},
        {5304, 7952},
        {7952, 5304},
        {7952, 5304},
        {7952, 5304},
        {5304, 7952},
        {5304, 7952},
        {7952, 5304},
        {7952, 5304},
        {7952, 5304},
        {7952, 5304},
        {5304, 7952},
        {7952, 5304},
        {7952, 5304},
        {7952, 5304},
        {5304, 7952},
        {5304, 7952},
        {7952, 5304},
        {7952, 5304},
        {7952, 5304},
        {7952, 5304},
        {5304, 7952},
        {7952, 5304},
        {7952, 5304},
        {7952, 5304},
        {5304, 7952},
        {5304, 7952},
        {7952, 5304},
        {7952, 5304},
        {7952, 5304},
        {7952, 5304},
        {5304, 7952},
        {7952, 5304},
        {7952, 5304},
        {7952, 5304},
        {5304, 7952},
        {5304, 7952},
        {7952, 5304},
        {7952, 5304},
        {7952, 530},
        {7952, 5304},
        {7952, 5304},
        {5304, 7952},
        {7952, 5304},
        {7952, 5304},
        {7952, 5304},
        {5304, 7952},
        {5304, 7952},
        {7952, 5304},
        {7952, 5304},
        {7952, 5304},
        {7952, 5304},
        {5304, 7952},
        {7952, 5304},
        {7952, 5304},
        {7952, 5304},
        {5304, 7952},
        {5304, 7952},
        {7952, 5304},
        {7952, 5304},
        {7952, 5304},
        {7952, 5304},
        {5304, 7952},
        {7952, 5304},
        {7952, 5304},
        {7952, 5304},
        {5304, 7952},
        {5304, 7952},
        {7952, 5304},
        {7952, 5304},
        {7952, 5304},
        {7952, 5304},
        {5304, 7952},
        {7952, 5304},
        {7952, 5304},
        {7952, 5304},
        {5304, 7952},
        {5304, 7952},
        {7952, 5304},
        {7952, 5304},
        {7952, 530},
        {7952, 5304},
        {7952, 5304},
        {5304, 7952},
        {7952, 5304},
        {7952, 5304},
        {7952, 5304},
        {5304, 7952},
        {5304, 7952},
        {7952, 5304},
        {7952, 5304},
        {7952, 5304},
        {7952, 5304},
        {5304, 7952},
        {7952, 5304},
        {7952, 5304},
        {7952, 5304},
        {5304, 7952},
        {5304, 7952},
        {7952, 5304},
        {7952, 5304},
        {7952, 5304},
        {7952, 5304},
        {5304, 7952},
        {7952, 5304},
        {7952, 5304},
        {7952, 5304},
        {5304, 7952},
        {5304, 7952},
        {7952, 5304},
        {7952, 5304},
        {7952, 5304},
        {7952, 5304},
        {5304, 7952},
        {7952, 5304},
        {7952, 5304},
        {7952, 5304},
        {5304, 7952},
        {5304, 7952},
        {7952, 5304},
        {7952, 5304},
        {7952, 530},
        {7952, 5304},
        {7952, 5304},
        {5304, 7952},
        {7952, 5304},
        {7952, 5304},
        {7952, 5304},
        {5304, 7952},
        {5304, 7952},
        {7952, 5304},
        {7952, 5304},
        {7952, 5304},
        {7952, 5304},
        {5304, 7952},
        {7952, 5304},
        {7952, 5304},
        {7952, 5304},
        {5304, 7952},
        {5304, 7952},
        {7952, 5304},
        {7952, 5304},
        {7952, 5304},
        {7952, 5304},
        {5304, 7952},
        {7952, 5304},
        {7952, 5304},
        {7952, 5304},
        {5304, 7952},
        {5304, 7952},
        {7952, 5304},
        {7952, 5304},
    };

    connect(this, &MasonryLayout::widthChanged,
            this, &MasonryLayout::rewrap);
//    qDebug() << normalizedSizes;
}

BrickItem *MasonryLayout::createComponent() {
    QQmlComponent component(qmlEngine(this));
    component.setData(R"QML(
import QtQuick 2.15
import ZoinGallery 1.0

BrickItem {
    property alias text: textField.text
    Rectangle {
        anchors.fill: parent
        color: "white"
        border.color: "black"
        Text {
            id: textField
            anchors.centerIn: parent
        }
    }
}
)QML", QUrl());
    if (component.status() != QQmlComponent::Ready) {
        qDebug() << "Error in component:" << component.status() << component.errors();
    }

    QVariantMap properties = {
        {"width", 100},
        {"height", 200},
//        {"testProperty", "wow"},
    };
    BrickItem *object = qobject_cast<BrickItem*>(component.createWithInitialProperties(properties));
    object->setParentItem(this);
    object->setParent(this);
    return object;
}

void MasonryLayout::rewrap() {
//    qDebug() << "rewrap" << width() << _pics.size();
    int targetWidth = width();

    int currentRow = 0;
    int currentColumn = 0;
    qreal lastX = 0;
    qreal lastY = 0;


    int rowTargetWidth = targetWidth;
    for (int i = 0; i < _pics.size(); i++) {
        qreal divider = _pics[i].originalSize.height() / qreal(_targetHeight);
        _pics[i].normalizedSize = QSizeF(_pics[i].originalSize.width() / divider, _pics[i].originalSize.height() / divider);
        _pics[i].row = currentRow;
        _pics[i].column = currentColumn;
        _pics[i].x = lastX;
        _pics[i].y = lastY;

        if (lastX + _pics[i].normalizedSize.width() < rowTargetWidth) {
            lastX += _pics[i].normalizedSize.width();
        }
        else if (!currentColumn) {
            qreal divider = _pics[i].originalSize.width() / qreal(targetWidth);
            _pics[i].normalizedSize = QSizeF(_pics[i].originalSize.width() / divider, _pics[i].originalSize.height() / divider);

            currentRow++;
            currentColumn = -1;
            lastX = 0;

            if (i != _pics.size() - 1) {
                lastY += _pics[i].normalizedSize.height();
            }
        }
        else {
            qreal stretchFactor = rowTargetWidth / qreal(lastX);
            for (int reverseI = i - 1; reverseI >= 0; reverseI--) {
                if (_pics[reverseI].row != currentRow) {
                    break;
                }
                _pics[reverseI].normalizedSize *= stretchFactor;
                _pics[reverseI].x *= stretchFactor;
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
    if (_pics.size()) {
        setContentHeight(lastY + _pics.last().normalizedSize.height());
//        if (_pics.size() > 1 && _pics[_pics.size() - 2].row == _pics[_pics.size() - 1].row) {
//        }
//        else {
//            setContentHeight(lastY);
//        }
    }
    else {
        setContentHeight(0);
    }

    int newContentY = qMin<int>(_contentY, qMax<int>(0, contentHeight() - height()));
    if (newContentY != _contentY) {
        setContentY(newContentY);
    }
    else {
        updateProperties();
    }
}

void MasonryLayout::updateProperties() {
    for (int i = 0; i < _pics.size(); i++) {
        if (!_pics[i].item) {
            _pics[i].item = createComponent();
        }
        BrickItem *item = _pics[i].item;
        QRectF newGeometry(QPointF(_pics[i].x, _pics[i].y - _contentY),
                           QSizeF(_pics[i].normalizedSize));
        item->setGeometry(newGeometry);
        QQmlProperty(item, "text").write(QString("%1 [%2,%3]").arg(i).arg(_pics[i].row).arg(_pics[i].column));
//        QQmlProperty(item, "text").write(QString::number(i));
    }
}

void MasonryLayout::setContentHeight(int newContentHeight) {
    if (_contentHeight == newContentHeight) {
        return;
    }
    _contentHeight = newContentHeight;
    emit contentHeightChanged();
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
//    qDebug() << "set contentY" << newContentY << _contentY;
//    newContentY = qMax(0, newContentY);
    if (_contentY == newContentY) {
        return;
    }
    _contentY = newContentY;
    updateProperties();
    emit contentYChanged();
}

int MasonryLayout::contentHeight() const {
    return _contentHeight;
}

BrickItem::BrickItem(QQuickItem *parent)
    : QQuickItem(parent) {
    _isChangingGeometry = false;
}

void BrickItem::setGeometry(QRectF rect) {
    _isChangingGeometry = true;

    setX(rect.x());
    setY(rect.y());
    setWidth(rect.width());

    _isChangingGeometry = false;

    setHeight(rect.height());
}

void BrickItem::geometryChanged(const QRectF &newGeometry, const QRectF &oldGeometry) {
    if (!_isChangingGeometry) {
        QQuickItem::geometryChanged(newGeometry, oldGeometry);
    }
}
