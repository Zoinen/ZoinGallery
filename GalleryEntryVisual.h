#ifndef GALLERYENTRYVISUAL_H
#define GALLERYENTRYVISUAL_H

#include <QObject>
#include <QString>
#include <QVariantMap>

// Stable per-delegate visual facade. Its QObject identity never changes and
// grouped snapshot notifications make every dependent binding observe the
// same complete row without re-running unrelated bindings.
class GalleryEntryVisual final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool valid READ valid NOTIFY identityChanged)
    Q_PROPERTY(QString entryId READ entryId NOTIFY identityChanged)
    Q_PROPERTY(int sourceIndex READ sourceIndex NOTIFY identityChanged)
    Q_PROPERTY(QString localPath READ localPath NOTIFY identityChanged)
    Q_PROPERTY(QString text READ text NOTIFY identityChanged)
    Q_PROPERTY(bool isFolder READ isFolder NOTIFY mediaChanged)
    Q_PROPERTY(bool isImage READ isImage NOTIFY mediaChanged)
    Q_PROPERTY(bool isSelected READ isSelected NOTIFY stateChanged)
    Q_PROPERTY(QString iconPath READ iconPath NOTIFY mediaChanged)
    Q_PROPERTY(QString iconKey READ iconKey NOTIFY mediaChanged)
    Q_PROPERTY(QString displayBaseName READ displayBaseName
               NOTIFY identityChanged)
    Q_PROPERTY(QString displayExtension READ displayExtension
               NOTIFY identityChanged)
    Q_PROPERTY(QString sizeText READ sizeText NOTIFY stateChanged)
    Q_PROPERTY(bool isHidden READ isHidden NOTIFY stateChanged)
    Q_PROPERTY(QString highlightMarker READ highlightMarker
               NOTIFY styleChanged)
    Q_PROPERTY(QString normalForeground READ normalForeground
               NOTIFY styleChanged)
    Q_PROPERTY(QString normalBackground READ normalBackground
               NOTIFY styleChanged)
    Q_PROPERTY(QString selectedForeground READ selectedForeground
               NOTIFY styleChanged)
    Q_PROPERTY(QString selectedBackground READ selectedBackground
               NOTIFY styleChanged)
    Q_PROPERTY(QString cursorForeground READ cursorForeground
               NOTIFY styleChanged)
    Q_PROPERTY(QString cursorBackground READ cursorBackground
               NOTIFY styleChanged)
    Q_PROPERTY(QString selectedCursorForeground READ selectedCursorForeground
               NOTIFY styleChanged)
    Q_PROPERTY(QString selectedCursorBackground READ selectedCursorBackground
               NOTIFY styleChanged)
    Q_PROPERTY(QString imageIdUrl READ imageIdUrl NOTIFY mediaChanged)

public:
    explicit GalleryEntryVisual(QObject *parent = nullptr);

    enum ChangeGroup : quint8 {
        IdentityChange = 1 << 0,
        MediaChange = 1 << 1,
        StateChange = 1 << 2,
        StyleChange = 1 << 3,
    };

    quint8 applySnapshot(const QVariantMap &snapshot);

    bool valid() const;
    QString entryId() const;
    int sourceIndex() const;
    QString localPath() const;
    QString text() const;
    bool isFolder() const;
    bool isImage() const;
    bool isSelected() const;
    QString iconPath() const;
    QString iconKey() const;
    QString displayBaseName() const;
    QString displayExtension() const;
    QString sizeText() const;
    bool isHidden() const;
    QString highlightMarker() const;
    QString normalForeground() const;
    QString normalBackground() const;
    QString selectedForeground() const;
    QString selectedBackground() const;
    QString cursorForeground() const;
    QString cursorBackground() const;
    QString selectedCursorForeground() const;
    QString selectedCursorBackground() const;
    QString imageIdUrl() const;

signals:
    void identityChanged();
    void mediaChanged();
    void stateChanged();
    void styleChanged();

private:
    struct SnapshotValues;
    static SnapshotValues parseSnapshot(const QVariantMap &snapshot);
    bool applyIdentity(const SnapshotValues &values);
    bool applyMedia(const SnapshotValues &values);
    bool applyState(const SnapshotValues &values);
    bool applyStyle(const SnapshotValues &values);
    quint8 emitChanges(bool identity, bool media,
                       bool state, bool style);

    bool _valid = false;
    QString _entryId;
    int _sourceIndex = -1;
    QString _localPath;
    QString _text;
    bool _isFolder = false;
    bool _isImage = false;
    bool _isSelected = false;
    QString _iconPath;
    QString _iconKey;
    QString _displayBaseName;
    QString _displayExtension;
    QString _sizeText;
    bool _isHidden = false;
    QString _highlightMarker;
    QString _normalForeground;
    QString _normalBackground;
    QString _selectedForeground;
    QString _selectedBackground;
    QString _cursorForeground;
    QString _cursorBackground;
    QString _selectedCursorForeground;
    QString _selectedCursorBackground;
    QString _imageIdUrl;
};

// Transitional source compatibility for embedders that included
// MasonryLayout.h before the visual facade became its own component.
using BrickVisualRow = GalleryEntryVisual;

#endif
