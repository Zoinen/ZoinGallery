#include "ImageFile.h"

QString fileSizeToHumanReadable(qint64 size) {
    const qint64 KB = 1024;
    const qint64 MB = 1024 * KB;
    const qint64 GB = 1024 * MB;
    const qint64 TB = 1024 * GB;

    QString result;

    if (size < KB) {
        result = QString::number(size) + " Bytes";
    } else if (size < MB) {
        result = QString::number(size / static_cast<double>(KB), 'f', 2) + " KB";
    } else if (size < GB) {
        result = QString::number(size / static_cast<double>(MB), 'f', 2) + " MB";
    } else if (size < TB) {
        result = QString::number(size / static_cast<double>(GB), 'f', 2) + " GB";
    } else {
        result = QString::number(size / static_cast<double>(TB), 'f', 2) + " TB";
    }

    return result;
}

QString calculateDivision(const QString expression) {
    QStringList operands = expression.split("/");
    if (operands.size() == 2) {
        int dividend = operands[0].toInt();
        int divisor = qMax(1, operands[1].toInt());
        return QString("%1").arg(float(dividend) / divisor);
    }
    return expression;
}


double fractionToDouble(const QString& fraction) {
    QStringList parts = fraction.split('/');
    if (parts.size() != 2) return 0.0;

    bool okNumerator, okDenominator;
    double numerator = parts.at(0).toDouble(&okNumerator);
    double denominator = parts.at(1).toDouble(&okDenominator);

    if (!okNumerator || !okDenominator || denominator == 0) return 0.0;

    return numerator / denominator;
}

QString convertEXIFToDMS(const QString& exifInput) {
    QStringList parts = exifInput.split(' ');
    if (parts.size() < 4) return "Invalid Input";

    double degrees = fractionToDouble(parts.at(0));
    double minutes = fractionToDouble(parts.at(1));
    double seconds = fractionToDouble(parts.at(2));

    double dd = degrees + minutes / 60 + seconds / 3600;
    int deg = dd;
    int min = 60 * (dd - deg);
    double sec = 3600 * (dd - deg) - 60 * min;

    // Extract and validate the direction
    QString direction = parts.last();
    if (direction != "N" && direction != "S" && direction != "E" && direction != "W") {
        return "Invalid Direction";
    }

    // Format the output correctly
    QString dmsString = QString("%1°%2'%3\"%4").arg(deg)
                            .arg(min, 2, 10, QChar('0'))
                            .arg(sec, 2, 'f', 2, QChar('0'))
                            .arg(direction);

    return dmsString;
}

QVariantList ImageFile::exifList() const {
    QVariantList out;

    QVariantMap name;
    name["text"] = fileName;
    out.append(name);

    if (info.exif.contains("DateTime")) {
        QVariantMap title;
        title["title"] = true;
        title["text"] = "Date and Time";
        title["icon"] = "qrc:/resources/ExifDateTime.svg";
        out.append(title);

        QVariantMap date;
        date["text"] = info.exif["DateTime"].toDateTime().toString("yyyy, MMMM dd");
        out.append(date);

        QVariantMap time;
        time["text"] = info.exif["DateTime"].toDateTime().toString("hh:mm:ss");
        out.append(time);
    }

    if (info.exif.contains("Size")) {
        QVariantMap title;
        title["title"] = true;
        title["text"] = "Image";
        title["icon"] = "qrc:/resources/ExifImage.svg";
        out.append(title);

        QVariantMap resolution;
        float mp = fullSize.width() * fullSize.height() / 1000000;
        if (mp > 1) {
            mp = qRound(mp);
        }
        resolution["text"] = QString("%1x%2 (%3 MP) %4").arg(info.imageSize.width()).arg(info.imageSize.height()).arg(mp).arg(info.orientation);
        out.append(resolution);

        QVariantMap size;
        size["text"] = fileSizeToHumanReadable(info.exif["Size"].toLongLong());
        out.append(size);
    }

    if (info.exif.contains("ShutterSpeed") || info.exif.contains("FNumber") || info.exif.contains("ISO")) {
        QVariantMap title;
        title["title"] = true;
        title["text"] = "Shooting";
        title["icon"] = "qrc:/resources/ExifShooting.svg";
        out.append(title);
    }
    if (info.exif.contains("ShutterSpeed")) {
        QString shutterString = info.exif["ShutterSpeed"].toString();
        QStringList shutterValues = shutterString.split("/");
        if (shutterValues.size() == 2) {
            int dividend = shutterValues[0].toInt();
            int divisor = qMax(1, shutterValues[1].toInt());

            float shutterSpeed = float(dividend) / divisor;
            if (shutterSpeed < 0.3) {
                shutterString = QString("1/%1").arg(qRound(1 / shutterSpeed));
            }
            else {
                shutterString = QString("%1 s").arg(shutterSpeed);
            }
        }

        QVariantMap shutterSpeed;
        shutterSpeed["text"] = shutterString;
        out.append(shutterSpeed);
    }
    if (info.exif.contains("FNumber")) {
        QVariantMap fNumber;
        fNumber["text"] = QString("F") + calculateDivision(info.exif["FNumber"].toString());
        out.append(fNumber);
    }
    if (info.exif.contains("ISO")) {
        QVariantMap iso;
        iso["text"] = info.exif["ISO"].toString() + " ISO";
        out.append(iso);
    }

    if (info.exif.contains("Camera") || info.exif.contains("FocalLength") || info.exif.contains("Lens")) {
        QVariantMap title;
        title["title"] = true;
        title["text"] = "Camera";
        title["icon"] = "qrc:/resources/ExifCamera.svg";
        out.append(title);
    }
    if (info.exif.contains("Camera")) {
        QVariantMap camera;
        camera["text"] = info.exif["Camera"].toString();
        out.append(camera);
    }
    if (info.exif.contains("FocalLength")) {
        QVariantMap focalLength;
        focalLength["text"] = info.exif["FocalLength"].toString() + " mm";
        out.append(focalLength);
    }
    if (info.exif.contains("Lens")) {
        QVariantMap lens;
        lens["text"] = info.exif["Lens"].toString();
        out.append(lens);
    }

    if (info.exif.contains("Location")) {
        QVariantMap title;
        title["title"] = true;
        title["text"] = "Location";
        title["icon"] = "qrc:/resources/ExifLocation.svg";
        out.append(title);

        QStringList loc = info.exif["Location"].toString().split(",");
        QVariantMap location;
        location["text"] = QString("%1 %2").arg(convertEXIFToDMS(loc[0]), convertEXIFToDMS(loc[1]));
        location["url"] = "https://www.google.com/maps/place/" + location["text"].toString().replace(" ", "+");
        out.append(location);
    }
    return out;
}
