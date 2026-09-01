#include "GalleryEntryVisual.h"

GalleryEntryVisual::GalleryEntryVisual(QObject *parent)
    : QObject(parent) {}

namespace {

template <typename Value>
bool assignChanged(Value &target, const Value &next) {
    if (target == next) {
        return false;
    }
    target = next;
    return true;
}

QString styleColor(const QVariantMap &style, const char *state,
                   const char *role) {
    return style.value(QString::fromLatin1(state)).toMap()
        .value(QString::fromLatin1(role)).toString();
}

} // namespace

struct GalleryEntryVisual::SnapshotValues {
    bool valid = false;
    QString entryId;
    int sourceIndex = -1;
    QString localPath;
    QString text;
    bool isFolder = false;
    bool isImage = false;
    bool isSelected = false;
    QString iconPath;
    QString iconKey;
    QString displayBaseName;
    QString displayExtension;
    QString sizeText;
    bool isHidden = false;
    QString highlightMarker;
    QString normalForeground;
    QString normalBackground;
    QString selectedForeground;
    QString selectedBackground;
    QString cursorForeground;
    QString cursorBackground;
    QString selectedCursorForeground;
    QString selectedCursorBackground;
    QString imageIdUrl;
};

GalleryEntryVisual::SnapshotValues GalleryEntryVisual::parseSnapshot(
    const QVariantMap &snapshot) {
    const QVariantMap style = snapshot.value(
        QStringLiteral("highlightStyle")).toMap();
    const QVariantMap display = snapshot.value(
        QStringLiteral("displayFields")).toMap();
    return {
        .valid = snapshot.value(QStringLiteral("valid")).toBool(),
        .entryId = snapshot.value(QStringLiteral("entryId")).toString(),
        .sourceIndex = snapshot.value(
            QStringLiteral("sourceIndex"), -1).toInt(),
        .localPath = snapshot.value(
            QStringLiteral("localPath")).toString(),
        .text = snapshot.value(QStringLiteral("text")).toString(),
        .isFolder = snapshot.value(
            QStringLiteral("isFolder")).toBool(),
        .isImage = snapshot.value(QStringLiteral("isImage")).toBool(),
        .isSelected = snapshot.value(
            QStringLiteral("isSelected")).toBool(),
        .iconPath = snapshot.value(
            QStringLiteral("iconPath")).toString(),
        .iconKey = snapshot.value(QStringLiteral("iconKey")).toString(),
        .displayBaseName = display.value(
            QStringLiteral("displayBaseName")).toString(),
        .displayExtension = display.value(
            QStringLiteral("displayExtension")).toString(),
        .sizeText = display.value(
            QStringLiteral("sizeText")).toString(),
        .isHidden = display.value(QStringLiteral("isHidden")).toBool(),
        .highlightMarker = style.value(
            QStringLiteral("marker")).toString(),
        .normalForeground = styleColor(
            style, "normal", "foreground"),
        .normalBackground = styleColor(
            style, "normal", "background"),
        .selectedForeground = styleColor(
            style, "selected", "foreground"),
        .selectedBackground = styleColor(
            style, "selected", "background"),
        .cursorForeground = styleColor(
            style, "cursor", "foreground"),
        .cursorBackground = styleColor(
            style, "cursor", "background"),
        .selectedCursorForeground = styleColor(
            style, "selectedCursor", "foreground"),
        .selectedCursorBackground = styleColor(
            style, "selectedCursor", "background"),
        .imageIdUrl = snapshot.value(
            QStringLiteral("imageIdUrl")).toString(),
    };
}

bool GalleryEntryVisual::applyIdentity(const SnapshotValues &values) {
    bool changed = false;
    changed |= assignChanged(_valid, values.valid);
    changed |= assignChanged(_entryId, values.entryId);
    changed |= assignChanged(_sourceIndex, values.sourceIndex);
    changed |= assignChanged(_localPath, values.localPath);
    changed |= assignChanged(_text, values.text);
    changed |= assignChanged(_displayBaseName, values.displayBaseName);
    changed |= assignChanged(_displayExtension, values.displayExtension);
    return changed;
}

bool GalleryEntryVisual::applyMedia(const SnapshotValues &values) {
    bool changed = false;
    changed |= assignChanged(_isFolder, values.isFolder);
    changed |= assignChanged(_isImage, values.isImage);
    changed |= assignChanged(_iconPath, values.iconPath);
    changed |= assignChanged(_iconKey, values.iconKey);
    changed |= assignChanged(_imageIdUrl, values.imageIdUrl);
    return changed;
}

bool GalleryEntryVisual::applyState(const SnapshotValues &values) {
    bool changed = false;
    changed |= assignChanged(_isSelected, values.isSelected);
    changed |= assignChanged(_sizeText, values.sizeText);
    changed |= assignChanged(_isHidden, values.isHidden);
    return changed;
}

bool GalleryEntryVisual::applyStyle(const SnapshotValues &values) {
    bool changed = false;
    changed |= assignChanged(_highlightMarker, values.highlightMarker);
    changed |= assignChanged(_normalForeground, values.normalForeground);
    changed |= assignChanged(_normalBackground, values.normalBackground);
    changed |= assignChanged(
        _selectedForeground, values.selectedForeground);
    changed |= assignChanged(
        _selectedBackground, values.selectedBackground);
    changed |= assignChanged(_cursorForeground, values.cursorForeground);
    changed |= assignChanged(_cursorBackground, values.cursorBackground);
    changed |= assignChanged(
        _selectedCursorForeground, values.selectedCursorForeground);
    changed |= assignChanged(
        _selectedCursorBackground, values.selectedCursorBackground);
    return changed;
}

quint8 GalleryEntryVisual::emitChanges(
    bool identity, bool media, bool state, bool style) {
    quint8 changes = 0;
    if (identity) {
        changes |= IdentityChange;
        emit identityChanged();
    }
    if (media) {
        changes |= MediaChange;
        emit mediaChanged();
    }
    if (state) {
        changes |= StateChange;
        emit stateChanged();
    }
    if (style) {
        changes |= StyleChange;
        emit styleChanged();
    }
    return changes;
}

quint8 GalleryEntryVisual::applySnapshot(
    const QVariantMap &snapshot) {
    const SnapshotValues values = parseSnapshot(snapshot);
    return emitChanges(
        applyIdentity(values), applyMedia(values),
        applyState(values), applyStyle(values));
}


bool GalleryEntryVisual::valid() const { return _valid; }
QString GalleryEntryVisual::entryId() const { return _entryId; }
int GalleryEntryVisual::sourceIndex() const { return _sourceIndex; }
QString GalleryEntryVisual::localPath() const { return _localPath; }
QString GalleryEntryVisual::text() const { return _text; }
bool GalleryEntryVisual::isFolder() const { return _isFolder; }
bool GalleryEntryVisual::isImage() const { return _isImage; }
bool GalleryEntryVisual::isSelected() const { return _isSelected; }
QString GalleryEntryVisual::iconPath() const { return _iconPath; }
QString GalleryEntryVisual::iconKey() const { return _iconKey; }
QString GalleryEntryVisual::displayBaseName() const { return _displayBaseName; }
QString GalleryEntryVisual::displayExtension() const { return _displayExtension; }
QString GalleryEntryVisual::sizeText() const { return _sizeText; }
bool GalleryEntryVisual::isHidden() const { return _isHidden; }
QString GalleryEntryVisual::highlightMarker() const { return _highlightMarker; }
QString GalleryEntryVisual::normalForeground() const { return _normalForeground; }
QString GalleryEntryVisual::normalBackground() const { return _normalBackground; }
QString GalleryEntryVisual::selectedForeground() const { return _selectedForeground; }
QString GalleryEntryVisual::selectedBackground() const { return _selectedBackground; }
QString GalleryEntryVisual::cursorForeground() const { return _cursorForeground; }
QString GalleryEntryVisual::cursorBackground() const { return _cursorBackground; }
QString GalleryEntryVisual::selectedCursorForeground() const {
    return _selectedCursorForeground;
}
QString GalleryEntryVisual::selectedCursorBackground() const {
    return _selectedCursorBackground;
}
QString GalleryEntryVisual::imageIdUrl() const { return _imageIdUrl; }
