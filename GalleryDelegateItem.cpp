#include "GalleryDelegateItem.h"

#include "GalleryPixelGrid.h"

#include <QAbstractAnimation>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>

GalleryDelegateItem::GalleryDelegateItem(QQuickItem *parent)
    : QQuickItem(parent),
      _visualModel(new GalleryEntryVisual(this)) {
    connect(_visualModel, &GalleryEntryVisual::identityChanged,
            this, &GalleryDelegateItem::visualIdentityChanged);
    connect(_visualModel, &GalleryEntryVisual::mediaChanged,
            this, &GalleryDelegateItem::visualMediaChanged);
    connect(_visualModel, &GalleryEntryVisual::stateChanged,
            this, &GalleryDelegateItem::visualStateChanged);
    connect(_visualModel, &GalleryEntryVisual::styleChanged,
            this, &GalleryDelegateItem::visualStyleChanged);
}

void GalleryDelegateItem::ensureGeometryAnimations() {
    if (_geometryAnimationGroup) {
        return;
    }
    constexpr int animationDuration = 500;
    _geometryAnimationGroup = new QParallelAnimationGroup(this);

    const auto createAnimation = [this](const char *propertyName) {
        auto *animation = new QPropertyAnimation(this, propertyName);
        animation->setDuration(animationDuration);
        animation->setEasingCurve(QEasingCurve::InOutQuad);
        _geometryAnimationGroup->addAnimation(animation);
        return animation;
    };
    _xAnimation = createAnimation("x");
    _yAnimation = createAnimation("y");
    _widthAnimation = createAnimation("width");
    _heightAnimation = createAnimation("height");

    connect(_geometryAnimationGroup, &QAbstractAnimation::stateChanged,
            this, [this](QAbstractAnimation::State current,
                         QAbstractAnimation::State previous) {
        const bool isRunning = current == QAbstractAnimation::Running;
        const bool wasRunning = previous == QAbstractAnimation::Running;
        if (isRunning != wasRunning) {
            emit geometryAnimationRunningChanged();
        }
    });
}

void GalleryDelegateItem::setPresentationMode(int mode) {
    if (_presentationMode == mode) {
        return;
    }
    _presentationMode = mode;
    emit presentationModeChanged();
}

int GalleryDelegateItem::presentationMode() const { return _presentationMode; }
QString GalleryDelegateItem::mode() const {
    switch (_presentationMode) {
    case 1:
        return QStringLiteral("columns");
    case 2:
        return QStringLiteral("details");
    case 3:
        return QStringLiteral("grid");
    case 4:
        return QStringLiteral("icons");
    default:
        return QStringLiteral("masonry");
    }
}
bool GalleryDelegateItem::masonryMode() const { return _presentationMode == 0; }
bool GalleryDelegateItem::columnsMode() const { return _presentationMode == 1; }
bool GalleryDelegateItem::detailsMode() const { return _presentationMode == 2; }
bool GalleryDelegateItem::gridMode() const { return _presentationMode == 3; }
bool GalleryDelegateItem::iconsMode() const { return _presentationMode == 4; }
bool GalleryDelegateItem::largePreviewMode() const {
    return _presentationMode == 0 || _presentationMode == 3
        || _presentationMode == 4;
}

void GalleryDelegateItem::setViewIndex(int viewIndex) {
    if (_viewIndex == viewIndex) {
        return;
    }
    _viewIndex = viewIndex;
    emit viewSourceIndexesChanged();
}

void GalleryDelegateItem::setSourceIndex(int sourceIndex) {
    if (_sourceIndex == sourceIndex) {
        return;
    }
    _sourceIndex = sourceIndex;
    emit viewSourceIndexesChanged();
}

bool GalleryDelegateItem::prepareViewSourceIndexes(int viewIndex,
                                                   int sourceIndex) {
    if (_viewIndex == viewIndex && _sourceIndex == sourceIndex) {
        return false;
    }
    _viewIndex = viewIndex;
    _sourceIndex = sourceIndex;
    return true;
}

void GalleryDelegateItem::notifyViewSourceIndexesChanged() {
    emit viewSourceIndexesChanged();
}

void GalleryDelegateItem::setViewSourceIndexes(int viewIndex,
                                               int sourceIndex) {
    if (prepareViewSourceIndexes(viewIndex, sourceIndex)) {
        notifyViewSourceIndexesChanged();
    }
}

int GalleryDelegateItem::viewIndex() const { return _viewIndex; }
int GalleryDelegateItem::sourceIndex() const { return _sourceIndex; }

void GalleryDelegateItem::setRow(int row) {
    if (_row == row) {
        return;
    }
    _row = row;
    emit rowColumnChanged();
}

void GalleryDelegateItem::setColumn(int column) {
    if (_column == column) {
        return;
    }
    _column = column;
    emit rowColumnChanged();
}

void GalleryDelegateItem::setRowColumn(int row, int column) {
    if (_row == row && _column == column) {
        return;
    }
    _row = row;
    _column = column;
    emit rowColumnChanged();
}

void GalleryDelegateItem::setPreviewRect(const QRectF &previewRect) {
    if (_previewRect == previewRect) {
        return;
    }
    _previewRect = previewRect;
    emit previewRectChanged();
}

void GalleryDelegateItem::setIconLabelText(const QString &text) {
    if (_iconLabelText == text) {
        return;
    }
    _iconLabelText = text;
    emit iconLabelTextChanged();
}

quint8 GalleryDelegateItem::setVisualRow(const QVariantMap &visualRow) {
    if (_visualRow == visualRow) {
        return 0;
    }
    _visualRow = visualRow;
    const quint8 changes = _visualModel->applySnapshot(visualRow);
    emit visualRowChanged();
    return changes;
}

void GalleryDelegateItem::assignVisualRow(
    const QVariantMap &visualRow) {
    setVisualRow(visualRow);
}

void GalleryDelegateItem::setVisualFacadeReady(bool ready) {
    if (_visualFacadeReady == ready) {
        return;
    }
    _visualFacadeReady = ready;
    emit visualFacadeReadyChanged();
}

QString GalleryDelegateItem::iconLabelText() const { return _iconLabelText; }
QVariantMap GalleryDelegateItem::visualRow() const { return _visualRow; }
bool GalleryDelegateItem::visualFacadeReady() const {
    return _visualFacadeReady;
}

bool GalleryDelegateItem::geometryAnimationRunning() const {
    return _geometryAnimationGroup
        && _geometryAnimationGroup->state() == QAbstractAnimation::Running;
}

QObject *GalleryDelegateItem::visualModel() const { return _visualModel; }
QString GalleryDelegateItem::entryId() const {
    return _visualModel->entryId();
}
QString GalleryDelegateItem::displayName() const {
    return _visualModel->text();
}
QString GalleryDelegateItem::displayBaseName() const {
    return _visualModel->displayBaseName();
}
QString GalleryDelegateItem::displayExtension() const {
    return _visualModel->displayExtension();
}
QString GalleryDelegateItem::iconPath() const {
    return _visualModel->iconPath();
}
QString GalleryDelegateItem::iconKey() const {
    return _visualModel->iconKey();
}
bool GalleryDelegateItem::isFolder() const {
    return _visualModel->isFolder();
}
bool GalleryDelegateItem::isImage() const {
    return _visualModel->isImage();
}
QString GalleryDelegateItem::imageIdUrl() const {
    return _visualModel->imageIdUrl();
}
QString GalleryDelegateItem::displaySize() const {
    return _visualModel->sizeText();
}
bool GalleryDelegateItem::hiddenEntry() const {
    return _visualModel->isHidden();
}
QString GalleryDelegateItem::highlightMarker() const {
    return _visualModel->highlightMarker();
}

void GalleryDelegateItem::animateToRect(const QRectF &rect) {
    ensureGeometryAnimations();
    _xAnimation->setEndValue(rect.x());
    _yAnimation->setEndValue(rect.y());
    _widthAnimation->setEndValue(rect.width());
    _heightAnimation->setEndValue(rect.height());
    _geometryAnimationGroup->start();
}

void GalleryDelegateItem::setGeometry(QRectF rect, bool animate,
                                      bool snapToLogicalPixels) {
    if (snapToLogicalPixels) {
        rect = ZoinGallery::PixelGrid::snapLogicalRect(rect);
    }
    if (animate) {
        animateToRect(rect);
        return;
    }

    stopGeometryAnimation();
    const QRectF oldGeometry(x(), y(), width(), height());
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
#if QT_VERSION < QT_VERSION_CHECK(6, 3, 0)
        geometryChanged(oldGeometry, rect);
#else
        geometryChange(oldGeometry, rect);
#endif
    }
}

QRectF GalleryDelegateItem::geometry() const {
    return QRectF(x(), y(), width(), height());
}

QRectF GalleryDelegateItem::previewRect() const { return _previewRect; }

void GalleryDelegateItem::stopGeometryAnimation() {
    if (_geometryAnimationGroup) {
        _geometryAnimationGroup->stop();
    }
}

int GalleryDelegateItem::row() const { return _row; }
int GalleryDelegateItem::column() const { return _column; }

#if QT_VERSION < QT_VERSION_CHECK(6, 3, 0)
void GalleryDelegateItem::geometryChanged(const QRectF &newGeometry,
                                          const QRectF &oldGeometry) {
    if (!_isChangingGeometry) {
        QQuickItem::geometryChanged(newGeometry, oldGeometry);
    }
}
#else
void GalleryDelegateItem::geometryChange(const QRectF &newGeometry,
                                         const QRectF &oldGeometry) {
    if (!_isChangingGeometry) {
        QQuickItem::geometryChange(newGeometry, oldGeometry);
    }
}
#endif
