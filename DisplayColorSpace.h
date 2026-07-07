#ifndef DISPLAYCOLORSPACE_H
#define DISPLAYCOLORSPACE_H

#include <QColorSpace>

class QImage;
class QScreen;

class DisplayColorSpace {
public:
    static QColorSpace current();
    static QString currentDescription();
    static QColorSpace cacheColorSpace();
    static QColorSpace colorSpaceForScreen(QScreen *screen);
    static QImage convertImage(QImage image, const QColorSpace &targetColorSpace = current());
    static QImage convertImageToColorSpace(QImage image, const QColorSpace &targetColorSpace);
    static bool conversionEnabled();
    static void setConversionEnabled(bool enabled);
    static void setCurrent(const QColorSpace &colorSpace);

private:
    static QColorSpace fallbackColorSpace();
    static QString colorSpaceDescription(const QColorSpace &colorSpace);
};

#endif // DISPLAYCOLORSPACE_H
