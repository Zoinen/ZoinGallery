#include "LibpngMetadataReader.h"
#include "ImageFile.h"

#include <png.h>
#include <cstdio>

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QScopedPointer>

bool LibpngMetadataReader::readMetadata(ImageInfo &result) {
    if (!isFormatSupported(result.path)) {
        return false;
    }

    auto fileDeleter = [](FILE* f) { if (f) fclose(f); };
    std::unique_ptr<FILE, decltype(fileDeleter)> file(fopen(result.path.toUtf8().constData(), "rb"), fileDeleter);
    if (!file) {
        qDebug() << "Failed to open file:" << result.path;
        return false;
    }

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) {
        qDebug() << "Failed to create PNG read struct";
        return false;
    }

    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_read_struct(&png, nullptr, nullptr);
        qDebug() << "Failed to create PNG info struct";
        return false;
    }

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, nullptr);
        qDebug() << "Error reading PNG file";
        return false;
    }

    png_init_io(png, file.get());
    png_read_info(png, info);

    result.imageSize = QSize(png_get_image_width(png, info), png_get_image_height(png, info));

    // Read text chunks (tEXt, zTXt, and iTXt)
    png_textp text_ptr;
    int num_text;
    QVariantList pngData;
    if (png_get_text(png, info, &text_ptr, &num_text) > 0) {
        for (int i = 0; i < num_text; i++) {
            QVariantMap map;
            map[QString(text_ptr[i].key)] = QString(text_ptr[i].text);
            pngData.append(map);
        }
    }
    result.exif["png_data"] = pngData;
    result.exif["Size"] = QFileInfo(result.path).size();

    png_destroy_read_struct(&png, &info, nullptr);
    return true;
}

bool LibpngMetadataReader::readPreviewAndMime(ImageData &result) {
    // For PNG, we can use the original file as the preview
    QFile file(result.request.info.path);
    if (!file.open(QIODevice::ReadOnly)) {
        qCritical() << "Failed to open file:" << result.request.info.path;
        return false;
    }

    QByteArray fileData = file.readAll();
    result.previewData.reset(new char[fileData.size()]);
    std::copy(fileData.begin(), fileData.end(), result.previewData.get());
    result.previewDataSize = fileData.size();
    result.previewMimeType = "image/png";
    result.previewUsed = "Original PNG file";

    return true;
}

bool LibpngMetadataReader::isFormatSupported(const QString &path) {
    return path.toLower().endsWith(".png");
}
