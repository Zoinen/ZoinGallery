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

QVariantList ImageFile::exifList() const {
    QVariantList out;

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
        location["text"] = info.exif["Location"].toString();
        // location["text"] = QString("%1 %2").arg(convertEXIFToDMS(loc[0]), convertEXIFToDMS(loc[1]));
        // location["url"] = "https://www.google.com/maps/place/" + location["text"].toString().replace(" ", "+");
        QStringList latLon = location["text"].toString().split(", ");
        location["url"] = QString("https://www.openstreetmap.org/?mlat=%1&mlon=%2").arg(latLon[0], latLon[1]);
        out.append(location);
    }

    if (info.exif.contains("png_data")) {
        QVariantList pngData = info.exif["png_data"].toList();
        for (auto data : pngData) {
            QVariantMap map = data.toMap();
            QString key = map.keys().first();
            if (key.startsWith("XML:")) {
                continue;
            }

            QVariantMap title;
            title["title"] = true;
            title["text"] = key;
            out.append(title);

            QVariantMap value;
            value["text"] = map[key];
            value["multiline"] = true;
            out.append(value);
        }
    }
    return out;
}

bool isExtensionMatch(const QString &path, const QStringList &pattern) {
    for (const QString &ext : pattern) {
        if (path.endsWith(QString(".") + ext, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}
