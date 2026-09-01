#ifndef GALLERYDELEGATEITEM_H
#define GALLERYDELEGATEITEM_H

#include "GalleryEntryVisual.h"

#include <QQuickItem>
#include <QVariantMap>

class QParallelAnimationGroup;
class QPropertyAnimation;

class GalleryDelegateItem : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(int presentationMode READ presentationMode
               WRITE setPresentationMode NOTIFY presentationModeChanged)
    Q_PROPERTY(QString mode READ mode NOTIFY presentationModeChanged)
    Q_PROPERTY(bool masonryMode READ masonryMode
               NOTIFY presentationModeChanged)
    Q_PROPERTY(bool columnsMode READ columnsMode
               NOTIFY presentationModeChanged)
    Q_PROPERTY(bool detailsMode READ detailsMode
               NOTIFY presentationModeChanged)
    Q_PROPERTY(bool gridMode READ gridMode
               NOTIFY presentationModeChanged)
    Q_PROPERTY(bool iconsMode READ iconsMode
               NOTIFY presentationModeChanged)
    Q_PROPERTY(bool largePreviewMode READ largePreviewMode
               NOTIFY presentationModeChanged)
    Q_PROPERTY(int viewIndex READ viewIndex WRITE setViewIndex
               NOTIFY viewSourceIndexesChanged)
    Q_PROPERTY(int sourceIndex READ sourceIndex WRITE setSourceIndex
               NOTIFY viewSourceIndexesChanged)
    Q_PROPERTY(int row READ row WRITE setRow NOTIFY rowColumnChanged)
    Q_PROPERTY(int column READ column WRITE setColumn NOTIFY rowColumnChanged)
    Q_PROPERTY(QRectF previewRect READ previewRect WRITE setPreviewRect
               NOTIFY previewRectChanged)
    Q_PROPERTY(QString iconLabelText READ iconLabelText
               WRITE setIconLabelText
               NOTIFY iconLabelTextChanged)
    Q_PROPERTY(QVariantMap visualRow READ visualRow WRITE assignVisualRow
               NOTIFY visualRowChanged)
    Q_PROPERTY(QObject *visualModel READ visualModel CONSTANT)
    Q_PROPERTY(QString entryId READ entryId NOTIFY visualIdentityChanged)
    Q_PROPERTY(QString displayName READ displayName
               NOTIFY visualIdentityChanged)
    Q_PROPERTY(QString displayBaseName READ displayBaseName
               NOTIFY visualIdentityChanged)
    Q_PROPERTY(QString displayExtension READ displayExtension
               NOTIFY visualIdentityChanged)
    Q_PROPERTY(QString iconPath READ iconPath NOTIFY visualMediaChanged)
    Q_PROPERTY(QString iconKey READ iconKey NOTIFY visualMediaChanged)
    Q_PROPERTY(bool isFolder READ isFolder NOTIFY visualMediaChanged)
    Q_PROPERTY(bool isImage READ isImage NOTIFY visualMediaChanged)
    Q_PROPERTY(QString imageIdUrl READ imageIdUrl
               NOTIFY visualMediaChanged)
    Q_PROPERTY(QString displaySize READ displaySize
               NOTIFY visualStateChanged)
    Q_PROPERTY(bool hiddenEntry READ hiddenEntry NOTIFY visualStateChanged)
    Q_PROPERTY(QString highlightMarker READ highlightMarker
               NOTIFY visualStyleChanged)
    Q_PROPERTY(bool visualFacadeReady READ visualFacadeReady
               WRITE setVisualFacadeReady
               NOTIFY visualFacadeReadyChanged)
    Q_PROPERTY(bool geometryAnimationRunning READ geometryAnimationRunning
               NOTIFY geometryAnimationRunningChanged)

public:
    explicit GalleryDelegateItem(QQuickItem *parent = nullptr);

    void setPresentationMode(int mode);
    void setViewIndex(int viewIndex);
    void setSourceIndex(int sourceIndex);
    void setRow(int row);
    void setColumn(int column);
    bool prepareViewSourceIndexes(int viewIndex, int sourceIndex);
    void notifyViewSourceIndexesChanged();
    void setViewSourceIndexes(int viewIndex, int sourceIndex);
    void setRowColumn(int row, int column);
    void setGeometry(QRectF rect, bool animate,
                     bool snapToLogicalPixels = true);
    void setPreviewRect(const QRectF &previewRect);
    void setIconLabelText(const QString &text);
    quint8 setVisualRow(const QVariantMap &visualRow);
    void assignVisualRow(const QVariantMap &visualRow);
    void setVisualFacadeReady(bool ready);
    QRectF geometry() const;
    QRectF previewRect() const;
    QString iconLabelText() const;
    QVariantMap visualRow() const;
    QObject *visualModel() const;
    QString entryId() const;
    QString displayName() const;
    QString displayBaseName() const;
    QString displayExtension() const;
    QString iconPath() const;
    QString iconKey() const;
    bool isFolder() const;
    bool isImage() const;
    QString imageIdUrl() const;
    QString displaySize() const;
    bool hiddenEntry() const;
    QString highlightMarker() const;
    bool visualFacadeReady() const;
    bool geometryAnimationRunning() const;
    void stopGeometryAnimation();

    int presentationMode() const;
    QString mode() const;
    bool masonryMode() const;
    bool columnsMode() const;
    bool detailsMode() const;
    bool gridMode() const;
    bool iconsMode() const;
    bool largePreviewMode() const;
    int viewIndex() const;
    int sourceIndex() const;
    int row() const;
    int column() const;

signals:
    void presentationModeChanged();
    void viewSourceIndexesChanged();
    void rowColumnChanged();
    void previewRectChanged();
    void iconLabelTextChanged();
    void visualRowChanged();
    void visualIdentityChanged();
    void visualMediaChanged();
    void visualStateChanged();
    void visualStyleChanged();
    void visualFacadeReadyChanged();
    void geometryAnimationRunningChanged();

protected:
#if QT_VERSION < QT_VERSION_CHECK(6, 3, 0)
    void geometryChanged(const QRectF &newGeometry,
                         const QRectF &oldGeometry) override;
#else
    void geometryChange(const QRectF &newGeometry,
                        const QRectF &oldGeometry) override;
#endif

private:
    void ensureGeometryAnimations();
    void animateToRect(const QRectF &rect);

    bool _isChangingGeometry = false;
    int _presentationMode = 0;
    int _viewIndex = -1;
    int _sourceIndex = -1;
    int _row = -1;
    int _column = -1;
    QRectF _previewRect;
    QString _iconLabelText;
    QVariantMap _visualRow;
    GalleryEntryVisual *_visualModel = nullptr;
    bool _visualFacadeReady = false;

    QParallelAnimationGroup *_geometryAnimationGroup = nullptr;
    QPropertyAnimation *_xAnimation = nullptr;
    QPropertyAnimation *_yAnimation = nullptr;
    QPropertyAnimation *_widthAnimation = nullptr;
    QPropertyAnimation *_heightAnimation = nullptr;
};

using BrickItem = GalleryDelegateItem;

#endif
