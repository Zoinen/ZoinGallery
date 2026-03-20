#include "DdsDecoder.h"

#include <QFile>
#include <QDebug>
#include <QSize>

// Include the DDS Image library
#include "dds.hpp"

REGISTER_DECODER_DEFINITION(DdsDecoder)

static const QStringList DdsExtensions = {"dds"};

// BC1/DXT1 and BC3/DXT5 decompression helper functions
namespace {
    // BC1/DXT1 block structure (8 bytes)
    struct BC1Block {
        uint16_t color0;
        uint16_t color1;
        uint32_t colorBits;
    };

    struct DXT5AlphaBlock {
        uint8_t alpha0;
        uint8_t alpha1;
        uint8_t alphaBits[6];
    };

    struct BC3Block {
        DXT5AlphaBlock alphaBlock;
        uint16_t color0;
        uint16_t color1;
        uint32_t colorBits;
    };

    // Extract RGB565 color
    void extractRGB565(uint16_t color, uint8_t& r, uint8_t& g, uint8_t& b) {
        r = ((color >> 11) & 0x1F) << 3;
        g = ((color >> 5) & 0x3F) << 2;
        b = (color & 0x1F) << 3;
        
        // Fill in the lower bits for better color precision
        r |= r >> 5;
        g |= g >> 6;
        b |= b >> 5;
    }

    // Decompress a BC1/DXT1 block to RGBA pixels
    void decompressBC1Block(const BC1Block& block, uint8_t* destPixels, int destWidth) {
        // Extract palette colors
        uint8_t colors[4][4]; // 4 colors, RGBA format
        
        uint8_t r0, g0, b0, r1, g1, b1;
        extractRGB565(block.color0, r0, g0, b0);
        extractRGB565(block.color1, r1, g1, b1);
        
        // Set up the color palette
        colors[0][0] = r0;
        colors[0][1] = g0;
        colors[0][2] = b0;
        colors[0][3] = 255; // Always opaque for color0
        
        colors[1][0] = r1;
        colors[1][1] = g1;
        colors[1][2] = b1;
        colors[1][3] = 255; // Always opaque for color1
        
        // Calculate the other two colors
        if (block.color0 > block.color1) {
            // Four-color block (fully opaque)
            colors[2][0] = (2 * r0 + r1) / 3;
            colors[2][1] = (2 * g0 + g1) / 3;
            colors[2][2] = (2 * b0 + b1) / 3;
            colors[2][3] = 255;
            
            colors[3][0] = (r0 + 2 * r1) / 3;
            colors[3][1] = (g0 + 2 * g1) / 3;
            colors[3][2] = (b0 + 2 * b1) / 3;
            colors[3][3] = 255;
        } else {
            // Three-color block with transparency
            colors[2][0] = (r0 + r1) / 2;
            colors[2][1] = (g0 + g1) / 2;
            colors[2][2] = (b0 + b1) / 2;
            colors[2][3] = 255;
            
            // Fourth color is transparent black
            colors[3][0] = 0;
            colors[3][1] = 0;
            colors[3][2] = 0;
            colors[3][3] = 0;
        }
        
        // Decode the block
        for (int y = 0; y < 4; y++) {
            for (int x = 0; x < 4; x++) {
                // Get color index (2 bits per pixel)
                int colorIndex = (block.colorBits >> (2 * (y * 4 + x))) & 0x3;
                
                // Calculate destination position
                int pos = (y * destWidth + x) * 4;
                
                // Write RGBA pixel
                destPixels[pos + 0] = colors[colorIndex][0];
                destPixels[pos + 1] = colors[colorIndex][1];
                destPixels[pos + 2] = colors[colorIndex][2];
                destPixels[pos + 3] = colors[colorIndex][3];
            }
        }
    }

    // Get alpha value from DXT5 alpha block
    uint8_t getAlpha(const DXT5AlphaBlock& block, int index) {
        // Get the 3-bit index (0-7) from the alpha block
        uint64_t bits = 0;
        for (int i = 0; i < 6; i++) {
            bits |= static_cast<uint64_t>(block.alphaBits[i]) << (i * 8);
        }
        uint8_t alphaIndex = (bits >> (3 * index)) & 0x7;

        // Decode alpha value based on the index
        if (block.alpha0 > block.alpha1) {
            // 8-alpha block
            switch (alphaIndex) {
                case 0: return block.alpha0;
                case 1: return block.alpha1;
                default: // Interpolated values
                    return ((8 - alphaIndex) * block.alpha0 + (alphaIndex - 1) * block.alpha1) / 7;
            }
        } else {
            // 6-alpha block
            switch (alphaIndex) {
                case 0: return block.alpha0;
                case 1: return block.alpha1;
                case 6: return 0;
                case 7: return 255;
                default: // Interpolated values
                    return ((6 - alphaIndex) * block.alpha0 + (alphaIndex - 1) * block.alpha1) / 5;
            }
        }
    }

    // Decompress a BC3/DXT5 block to RGBA pixels
    void decompressBC3Block(const BC3Block& block, uint8_t* destPixels, int destWidth) {
        // Extract palette colors
        uint8_t colors[4][4]; // 4 colors, RGBA format
        
        uint8_t r0, g0, b0, r1, g1, b1;
        extractRGB565(block.color0, r0, g0, b0);
        extractRGB565(block.color1, r1, g1, b1);
        
        // Set up the color palette
        colors[0][0] = r0;
        colors[0][1] = g0;
        colors[0][2] = b0;
        colors[0][3] = 255;
        
        colors[1][0] = r1;
        colors[1][1] = g1;
        colors[1][2] = b1;
        colors[1][3] = 255;
        
        // Calculate the other two colors
        if (block.color0 > block.color1) {
            // Four-color block
            colors[2][0] = (2 * r0 + r1) / 3;
            colors[2][1] = (2 * g0 + g1) / 3;
            colors[2][2] = (2 * b0 + b1) / 3;
            colors[2][3] = 255;
            
            colors[3][0] = (r0 + 2 * r1) / 3;
            colors[3][1] = (g0 + 2 * g1) / 3;
            colors[3][2] = (b0 + 2 * b1) / 3;
            colors[3][3] = 255;
        } else {
            // Three-color block
            colors[2][0] = (r0 + r1) / 2;
            colors[2][1] = (g0 + g1) / 2;
            colors[2][2] = (b0 + b1) / 2;
            colors[2][3] = 255;
            
            colors[3][0] = 0;
            colors[3][1] = 0;
            colors[3][2] = 0;
            colors[3][3] = 255;
        }
        
        // Decode the block
        for (int y = 0; y < 4; y++) {
            for (int x = 0; x < 4; x++) {
                // Get color index (2 bits per pixel)
                int colorIndex = (block.colorBits >> (2 * (y * 4 + x))) & 0x3;
                
                // Get alpha value
                uint8_t alpha = getAlpha(block.alphaBlock, y * 4 + x);
                
                // Calculate destination position
                int pos = (y * destWidth + x) * 4;
                
                // Write RGBA pixel
                destPixels[pos + 0] = colors[colorIndex][0];
                destPixels[pos + 1] = colors[colorIndex][1];
                destPixels[pos + 2] = colors[colorIndex][2];
                destPixels[pos + 3] = alpha;
            }
        }
    }
}

QStringList DdsDecoder::supportedFormats() {
    return DdsExtensions;
}

bool DdsDecoder::readMetadata(ImageInfo &result) {
    if (!isFormatSupported(result.path)) {
        return false;
    }

    QFile file(result.path);
    if (!file.open(QFile::ReadOnly)) {
        qDebug() << "Failed to open DDS file:" << result.path;
        return false;
    }

    QByteArray data = file.readAll();
    file.close();

    if (data.isEmpty()) {
        qDebug() << "Empty DDS file:" << result.path;
        return false;
    }

    dds::Image image;
    dds::ReadResult readResult = dds::readImage(
        reinterpret_cast<std::uint8_t*>(data.data()),
        data.size(),
        &image
    );

    if (readResult != dds::ReadResult::Success) {
        qDebug() << "Failed to parse DDS file:" << result.path << "Error code:" << static_cast<int>(readResult);
        return false;
    }

    // Set image size
    result.imageSize = QSize(image.width, image.height);
    
    // Set EXIF information
    result.exif["Size"] = data.size();
    result.exif["Format"] = static_cast<int>(image.format);
    result.exif["MipLevels"] = image.numMips;
    result.exif["Dimension"] = static_cast<int>(image.dimension);
    result.exif["HasAlpha"] = image.supportsAlpha;

    return true;
}

QImage DdsDecoder::decode(const QString &mimeType, const QByteArray &data, QSize targetSize) {
    if (mimeType != "image/vnd.ms-dds" && mimeType != "image/dds") {
        return QImage();
    }
    
    return decodeDds(data, targetSize);
}

QImage DdsDecoder::decodeDds(const QByteArray &data, QSize targetSize) {
    if (data.isEmpty()) {
        qDebug() << "Empty DDS data";
        return QImage();
    }

    dds::Image image;
    dds::ReadResult readResult = dds::readImage(
        reinterpret_cast<std::uint8_t*>(const_cast<char*>(data.constData())),
        data.size(),
        &image
    );

    if (readResult != dds::ReadResult::Success) {
        qDebug() << "Failed to parse DDS data. Error code:" << static_cast<int>(readResult);
        return QImage();
    }

    // Choose the most appropriate mipmap level based on target size
    uint32_t mipLevel = 0;
    if (image.numMips > 1 && !targetSize.isEmpty()) {
        for (uint32_t i = 0; i < image.numMips; i++) {
            uint32_t width = std::max(1u, image.width >> i);
            uint32_t height = std::max(1u, image.height >> i);
            
            if (width <= static_cast<uint32_t>(targetSize.width()) || 
                height <= static_cast<uint32_t>(targetSize.height())) {
                mipLevel = i;
                break;
            }
        }
    }

    // Get dimensions for selected mip level
    uint32_t width = std::max(1u, image.width >> mipLevel);
    uint32_t height = std::max(1u, image.height >> mipLevel);

    // Create QImage with the appropriate format
    QImage::Format imageFormat = image.supportsAlpha ? QImage::Format_RGBA8888 : QImage::Format_RGB888;
    QImage result(width, height, imageFormat);

    // Process based on DDS format
    if (image.format == DXGI_FORMAT_R8G8B8A8_UNORM || 
        image.format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ||
        image.format == DXGI_FORMAT_B8G8R8A8_UNORM ||
        image.format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) {
        
        // Direct copy for standard RGBA formats
        const uint8_t* srcData = image.mipmaps[mipLevel].data();
        
        for (uint32_t y = 0; y < height; y++) {
            for (uint32_t x = 0; x < width; x++) {
                int offset = (y * width + x) * 4;
                QRgb pixel;
                
                // Handle different byte orders
                if (image.format == DXGI_FORMAT_R8G8B8A8_UNORM || 
                    image.format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) {
                    pixel = qRgba(srcData[offset], srcData[offset + 1], 
                                  srcData[offset + 2], srcData[offset + 3]);
                } else {
                    pixel = qRgba(srcData[offset + 2], srcData[offset + 1], 
                                  srcData[offset], srcData[offset + 3]);
                }
                
                result.setPixel(x, y, pixel);
            }
        }
    } else if (image.format == DXGI_FORMAT_BC1_UNORM || 
               image.format == DXGI_FORMAT_BC1_UNORM_SRGB) {
        // BC1/DXT1 compressed format
        const uint8_t* srcData = image.mipmaps[mipLevel].data();
        
        // Allocate buffer for the decompressed block (4x4 pixels, RGBA)
        uint8_t blockBuffer[4 * 4 * 4];
        
        // BC1 blocks are 4x4 pixels each, 8 bytes per block
        uint32_t blocksWidth = (width + 3) / 4;
        uint32_t blocksHeight = (height + 3) / 4;
        
        // Decompress each block
        for (uint32_t blockY = 0; blockY < blocksHeight; blockY++) {
            for (uint32_t blockX = 0; blockX < blocksWidth; blockX++) {
                // Get pointer to current block
                const BC1Block* block = reinterpret_cast<const BC1Block*>(
                    srcData + (blockY * blocksWidth + blockX) * sizeof(BC1Block)
                );
                
                // Decompress the block
                decompressBC1Block(*block, blockBuffer, 4);
                
                // Copy the decompressed pixels to the result image
                for (int y = 0; y < 4; y++) {
                    int destY = blockY * 4 + y;
                    if (destY >= height) continue;
                    
                    for (int x = 0; x < 4; x++) {
                        int destX = blockX * 4 + x;
                        if (destX >= width) continue;
                        
                        int srcPos = (y * 4 + x) * 4;
                        QRgb pixel = qRgba(
                            blockBuffer[srcPos],
                            blockBuffer[srcPos + 1],
                            blockBuffer[srcPos + 2],
                            blockBuffer[srcPos + 3]
                        );
                        
                        result.setPixel(destX, destY, pixel);
                    }
                }
            }
        }
    } else if (image.format == DXGI_FORMAT_BC3_UNORM || 
               image.format == DXGI_FORMAT_BC3_UNORM_SRGB) {
        // BC3/DXT5 compressed format
        const uint8_t* srcData = image.mipmaps[mipLevel].data();
        
        // Allocate buffer for the decompressed block (4x4 pixels, RGBA)
        uint8_t blockBuffer[4 * 4 * 4];
        
        // BC3 blocks are 4x4 pixels each, 16 bytes per block
        uint32_t blocksWidth = (width + 3) / 4;
        uint32_t blocksHeight = (height + 3) / 4;
        
        // Decompress each block
        for (uint32_t blockY = 0; blockY < blocksHeight; blockY++) {
            for (uint32_t blockX = 0; blockX < blocksWidth; blockX++) {
                // Get pointer to current block
                const BC3Block* block = reinterpret_cast<const BC3Block*>(
                    srcData + (blockY * blocksWidth + blockX) * sizeof(BC3Block)
                );
                
                // Decompress the block
                decompressBC3Block(*block, blockBuffer, 4);
                
                // Copy the decompressed pixels to the result image
                for (int y = 0; y < 4; y++) {
                    int destY = blockY * 4 + y;
                    if (destY >= height) continue;
                    
                    for (int x = 0; x < 4; x++) {
                        int destX = blockX * 4 + x;
                        if (destX >= width) continue;
                        
                        int srcPos = (y * 4 + x) * 4;
                        QRgb pixel = qRgba(
                            blockBuffer[srcPos],
                            blockBuffer[srcPos + 1],
                            blockBuffer[srcPos + 2],
                            blockBuffer[srcPos + 3]
                        );
                        
                        result.setPixel(destX, destY, pixel);
                    }
                }
            }
        }
    } else {
        // For other compressed or unhandled formats, fall back to a default pattern
        result.fill(Qt::red);  // Indicate unhandled format
        
        qDebug() << "Unhandled DDS format:" << image.format 
                 << "- Implementing decompression for this format is needed";
    }
    
    return result;
} 