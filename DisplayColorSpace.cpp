#include "DisplayColorSpace.h"

#include <QGuiApplication>
#include <QImage>
#include <QMutex>
#include <QMutexLocker>
#include <QScreen>
#include <QSettings>
#include <QDebug>

namespace
{
QMutex currentColorSpaceMutex;
QColorSpace currentColorSpace;
}

#if defined(Q_OS_MACOS)
QColorSpace macDisplayColorSpaceForScreen(QScreen *screen);
#endif

QColorSpace DisplayColorSpace::current()
{
    QMutexLocker locker(&currentColorSpaceMutex);
    if (currentColorSpace.isValid()) {
        return currentColorSpace;
    }
    locker.unlock();

    return colorSpaceForScreen(QGuiApplication::primaryScreen());
}

QString DisplayColorSpace::currentDescription()
{
    return colorSpaceDescription(current());
}

QColorSpace DisplayColorSpace::cacheColorSpace()
{
    return QColorSpace(QColorSpace::SRgb);
}

QColorSpace DisplayColorSpace::colorSpaceForScreen(QScreen *screen)
{
#if defined(Q_OS_MACOS)
    const QColorSpace macColorSpace = macDisplayColorSpaceForScreen(screen);
    if (macColorSpace.isValid()) {
        return macColorSpace;
    }
#else
    Q_UNUSED(screen);
#endif

    return fallbackColorSpace();
}

QImage DisplayColorSpace::convertImage(QImage image, const QColorSpace &targetColorSpace)
{
    if (!conversionEnabled()) {
        if (!image.isNull() && !image.colorSpace().isValid()) {
            image.setColorSpace(cacheColorSpace());
        }
        return image;
    }

    return convertImageToColorSpace(image, targetColorSpace);
}

QImage DisplayColorSpace::convertImageToColorSpace(QImage image, const QColorSpace &targetColorSpace)
{
    if (image.isNull()) {
        return image;
    }

    const QColorSpace target = targetColorSpace.isValid() ? targetColorSpace : fallbackColorSpace();
    const QColorSpace assumedSource = cacheColorSpace();
    QColorSpace source = image.colorSpace();
    if (!source.isValid()) {
        image.setColorSpace(assumedSource);
        source = assumedSource;
    }
    if (source == target) {
        return image;
    }

    QImage converted = image.convertedToColorSpace(target, image.format());
    if (converted.isNull()) {
        converted = image.convertedToColorSpace(target);
    }
    if (!converted.isNull()) {
        return converted;
    }

    qWarning() << "Failed to convert image color space; tagging existing pixels as target display color space";
    image.setColorSpace(target);
    return image;
}

bool DisplayColorSpace::conversionEnabled()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("General"));
    return settings.value(QStringLiteral("convertToDisplayColorSpace"), true).toBool();
}

void DisplayColorSpace::setConversionEnabled(bool enabled)
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("General"));
    settings.setValue(QStringLiteral("convertToDisplayColorSpace"), enabled);
}

void DisplayColorSpace::setCurrent(const QColorSpace &colorSpace)
{
    const QColorSpace nextColorSpace = colorSpace.isValid() ? colorSpace : fallbackColorSpace();

    QMutexLocker locker(&currentColorSpaceMutex);
    if (currentColorSpace == nextColorSpace) {
        return;
    }
    currentColorSpace = nextColorSpace;
    qInfo() << "Display color space changed" << currentColorSpace;
}

QColorSpace DisplayColorSpace::fallbackColorSpace()
{
    return QColorSpace(QColorSpace::SRgb);
}

QString DisplayColorSpace::colorSpaceDescription(const QColorSpace &colorSpace)
{
    if (!colorSpace.isValid()) {
        return QStringLiteral("sRGB");
    }

    const QString description = colorSpace.description();
    if (!description.isEmpty()) {
        return description;
    }
    if (colorSpace == QColorSpace(QColorSpace::DisplayP3)) {
        return QStringLiteral("Display P3");
    }
    if (colorSpace == QColorSpace(QColorSpace::SRgb)) {
        return QStringLiteral("sRGB");
    }
    if (colorSpace == QColorSpace(QColorSpace::AdobeRgb)) {
        return QStringLiteral("Adobe RGB");
    }
    if (colorSpace == QColorSpace(QColorSpace::Bt2020)) {
        return QStringLiteral("BT.2020");
    }

    return QStringLiteral("Custom RGB display profile");
}
