// ***************************************************************** -*- C++ -*-
/*
 * Copyright (C) 2004-2021 Exiv2 authors
 * This program is part of the Exiv2 distribution.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, 5th Floor, Boston, MA 02110-1301 USA.
 */
// *****************************************************************************
// included header files
#include "Preview.h"
#include <exiv2/config.h>
#include <exiv2/error.hpp>

#include <climits>
#include <string>

#include <exiv2/preview.hpp>
#include <exiv2/futils.hpp>
#include "enforce.hpp"
#include "safe_op.hpp"

#include <exiv2/image.hpp>
#include <exiv2/cr2image.hpp>
#include <exiv2/jpgimage.hpp>
#include <exiv2/tiffimage.hpp>
//#include <exiv2/tiffimage_int.hpp>

// *****************************************************************************
namespace {

    using namespace Exiv2;
    using Exiv2::byte;

    /*!
      @brief Compare two preview images by number of pixels, if width and height
             of both lhs and rhs are available or else by size.
             Return true if lhs is smaller than rhs.
     */
    bool cmpPreviewProperties(
        const PreviewProperties& lhs,
        const PreviewProperties& rhs
    )
    {
        uint32_t l = lhs.width_ * lhs.height_;
        uint32_t r = rhs.width_ * rhs.height_;

        return l < r;
    }

    /*!
      @brief Decode a Hex string.
     */
    DataBuf decodeHex(const byte *src, long srcSize);

    /*!
      @brief Decode a Base64 string.
     */
    DataBuf decodeBase64(const std::string &src);

    /*!
      @brief Decode an Illustrator thumbnail that follows after %AI7_Thumbnail.
     */
    DataBuf decodeAi7Thumbnail(const DataBuf &src);

    /*!
      @brief Create a PNM image from raw RGB data.
     */
    DataBuf makePnm(uint32_t width, uint32_t height, const DataBuf &rgb);

    /*!
      Base class for image loaders. Provides virtual methods for reading properties
      and DataBuf.
     */
    class Loader {
    public:
        //! Virtual destructor.
        virtual ~Loader() {}

        //! Loader auto pointer
        typedef std::auto_ptr<Loader> AutoPtr;

        //! Create a Loader subclass for requested id
        static AutoPtr create(PreviewId id, const Image &image);

        //! Check if a preview image with given params exists in the image
        virtual bool valid() const { return valid_; }

        //! Get properties of a preview image with given params
        virtual PreviewProperties getProperties() const;

        //! Get a buffer that contains the preview image
        virtual DataBuf getData() const = 0;

        //! Read preview image dimensions when they are not available directly
        virtual bool readDimensions() { return true; }

        //! A number of image loaders configured in the loaderList_ table
        static PreviewId getNumLoaders();

    protected:
        //! Constructor. Sets all image properies to unknown.
        Loader(PreviewId id, const Image &image);

        //! Functions that creates a loader from given parameters
        typedef AutoPtr (*CreateFunc)(PreviewId id, const Image &image, int parIdx);

        //! Structure to list possible loaders
        struct LoaderList {
            const char *imageMimeType_; //!< Image type for which the loader is valid, 0 matches all images
            CreateFunc create_;         //!< Function that creates particular loader instance
            int parIdx_;                //!< Parameter that is passed into CreateFunc
        };

        //! Table that lists possible loaders.  PreviewId is an index to this table.
        static const LoaderList loaderList_[];

        //! Identifies preview image type
        PreviewId id_;

        //! Source image reference
        const Image &image_;

        //! Preview image width
        uint32_t width_;

        //! Preview image length
        uint32_t height_;

        //! Preview image size in bytes
        uint32_t size_;

        //! True if the source image contains a preview image of given type
        bool valid_;
    };

    //! Loader for native previews
    class LoaderNative : public Loader {
    public:
        //! Constructor
        LoaderNative(PreviewId id, const Image &image, int parIdx);

        //! Get properties of a preview image with given params
        virtual PreviewProperties getProperties() const;

        //! Get a buffer that contains the preview image
        virtual DataBuf getData() const;

        //! Read preview image dimensions
        virtual bool readDimensions();

    protected:
        //! Native preview information
        NativePreview nativePreview_;
    };

    //! Function to create new LoaderNative
    Loader::AutoPtr createLoaderNative(PreviewId id, const Image &image, int parIdx);

    //! Loader for Jpeg previews that are not read into ExifData directly
    class LoaderExifJpeg : public Loader {
    public:

        //! Constructor
        LoaderExifJpeg(PreviewId id, const Image &image, int parIdx);

        //! Get properties of a preview image with given params
        virtual PreviewProperties getProperties() const;

        //! Get a buffer that contains the preview image
        virtual DataBuf getData() const;

        //! Read preview image dimensions
        virtual bool readDimensions();

    protected:
        //! Structure that lists offset/size tag pairs
        struct Param {
            const char* offsetKey_;         //!< Offset tag
            const char* sizeKey_;           //!< Size tag
            const char* baseOffsetKey_;     //!< Tag that holds base offset or 0
        };

        //! Table that holds all possible offset/size pairs. parIdx is an index to this table
        static const Param param_[];

        //! Offset value
        uint32_t offset_;
    };

    //! Function to create new LoaderExifJpeg
    Loader::AutoPtr createLoaderExifJpeg(PreviewId id, const Image &image, int parIdx);

    //! Loader for Jpeg previews that are read into ExifData
    class LoaderExifDataJpeg : public Loader {
    public:
        //! Constructor
        LoaderExifDataJpeg(PreviewId id, const Image &image, int parIdx);

        //! Get properties of a preview image with given params
        virtual PreviewProperties getProperties() const;

        //! Get a buffer that contains the preview image
        virtual DataBuf getData() const;

        //! Read preview image dimensions
        virtual bool readDimensions();

    protected:

        //! Structure that lists data/size tag pairs
        struct Param {
            const char* dataKey_; //!< Data tag
            const char* sizeKey_; //!< Size tag
        };

        //! Table that holds all possible data/size pairs. parIdx is an index to this table
        static const Param param_[];

        //! Key that points to the Value that contains the JPEG preview in data area
        ExifKey dataKey_;
    };

    //! Function to create new LoaderExifDataJpeg
    Loader::AutoPtr createLoaderExifDataJpeg(PreviewId id, const Image &image, int parIdx);

    //! Loader for Tiff previews - it can get image data from ExifData or image_.io() as needed
    class LoaderTiff : public Loader {
    public:
        //! Constructor
        LoaderTiff(PreviewId id, const Image &image, int parIdx);

        //! Get properties of a preview image with given params
        virtual PreviewProperties getProperties() const;

        //! Get a buffer that contains the preview image
        virtual DataBuf getData() const;

    protected:
        //! Name of the group that contains the preview image
        const char *group_;

        //! Tag that contains image data. Possible values are "StripOffsets" or "TileOffsets"
        std::string offsetTag_;

        //! Tag that contains data sizes. Possible values are "StripByteCounts" or "TileByteCounts"
        std::string sizeTag_;

        //! Structure that lists preview groups
        struct Param {
            const char* group_; //!< Group name
            const char* checkTag_; //!< Tag to check or NULL
            const char* checkValue_; //!< The preview image is valid only if the checkTag_ has this value
        };

        //! Table that holds all possible groups. parIdx is an index to this table.
        static const Param param_[];

    };

    //! Function to create new LoaderTiff
    Loader::AutoPtr createLoaderTiff(PreviewId id, const Image &image, int parIdx);

    //! Loader for JPEG previews stored in the XMP metadata
    class LoaderXmpJpeg : public Loader {
    public:
        //! Constructor
        LoaderXmpJpeg(PreviewId id, const Image &image, int parIdx);

        //! Get properties of a preview image with given params
        virtual PreviewProperties getProperties() const;

        //! Get a buffer that contains the preview image
        virtual DataBuf getData() const;

        //! Read preview image dimensions
        virtual bool readDimensions();

    protected:
        //! Preview image data
        DataBuf preview_;
    };

    //! Function to create new LoaderXmpJpeg
    Loader::AutoPtr createLoaderXmpJpeg(PreviewId id, const Image &image, int parIdx);

// *****************************************************************************
// class member definitions

    const Loader::LoaderList Loader::loaderList_[] = {
        { 0,                       createLoaderNative,       0 },
        { 0,                       createLoaderNative,       1 },
        { 0,                       createLoaderNative,       2 },
        { 0,                       createLoaderNative,       3 },
        { 0,                       createLoaderExifDataJpeg, 0 },
        { 0,                       createLoaderExifDataJpeg, 1 },
        { 0,                       createLoaderExifDataJpeg, 2 },
        { 0,                       createLoaderExifDataJpeg, 3 },
        { 0,                       createLoaderExifDataJpeg, 4 },
        { 0,                       createLoaderExifDataJpeg, 5 },
        { 0,                       createLoaderExifDataJpeg, 6 },
        { 0,                       createLoaderExifDataJpeg, 7 },
        { 0,                       createLoaderExifDataJpeg, 8 },
        { "image/x-panasonic-rw2", createLoaderExifDataJpeg, 9 },
        { 0,                       createLoaderExifDataJpeg,10 },
        { 0,                       createLoaderExifDataJpeg,11 },
        { 0,                       createLoaderTiff,         0 },
        { 0,                       createLoaderTiff,         1 },
        { 0,                       createLoaderTiff,         2 },
        { 0,                       createLoaderTiff,         3 },
        { 0,                       createLoaderTiff,         4 },
        { 0,                       createLoaderTiff,         5 },
        { 0,                       createLoaderTiff,         6 },
        { "image/x-canon-cr2",     createLoaderTiff,         7 },
        { 0,                       createLoaderExifJpeg,     0 },
        { 0,                       createLoaderExifJpeg,     1 },
        { 0,                       createLoaderExifJpeg,     2 },
        { 0,                       createLoaderExifJpeg,     3 },
        { 0,                       createLoaderExifJpeg,     4 },
        { 0,                       createLoaderExifJpeg,     5 },
        { 0,                       createLoaderExifJpeg,     6 },
        { "image/x-canon-cr2",     createLoaderExifJpeg,     7 },
        { 0,                       createLoaderExifJpeg,     8 },
        { 0,                       createLoaderXmpJpeg,      0 }
    };

    const LoaderExifJpeg::Param LoaderExifJpeg::param_[] = {
        { "Exif.Image.JPEGInterchangeFormat",     "Exif.Image.JPEGInterchangeFormatLength",     0 }, // 0
        { "Exif.SubImage1.JPEGInterchangeFormat", "Exif.SubImage1.JPEGInterchangeFormatLength", 0 }, // 1
        { "Exif.SubImage2.JPEGInterchangeFormat", "Exif.SubImage2.JPEGInterchangeFormatLength", 0 }, // 2
        { "Exif.SubImage3.JPEGInterchangeFormat", "Exif.SubImage3.JPEGInterchangeFormatLength", 0 }, // 3
        { "Exif.SubImage4.JPEGInterchangeFormat", "Exif.SubImage4.JPEGInterchangeFormatLength", 0 }, // 4
        { "Exif.SubThumb1.JPEGInterchangeFormat", "Exif.SubThumb1.JPEGInterchangeFormatLength", 0 }, // 5
        { "Exif.Image2.JPEGInterchangeFormat",    "Exif.Image2.JPEGInterchangeFormatLength",    0 }, // 6
        { "Exif.Image.StripOffsets",              "Exif.Image.StripByteCounts",                 0 }, // 7
        { "Exif.OlympusCs.PreviewImageStart",     "Exif.OlympusCs.PreviewImageLength",          "Exif.MakerNote.Offset"}  // 8
    };

    const LoaderExifDataJpeg::Param LoaderExifDataJpeg::param_[] = {
        { "Exif.Thumbnail.JPEGInterchangeFormat",      "Exif.Thumbnail.JPEGInterchangeFormatLength"      }, //  0
        { "Exif.NikonPreview.JPEGInterchangeFormat",   "Exif.NikonPreview.JPEGInterchangeFormatLength"   }, //  1
        { "Exif.Pentax.PreviewOffset",                 "Exif.Pentax.PreviewLength"                       }, //  2
        { "Exif.PentaxDng.PreviewOffset",              "Exif.PentaxDng.PreviewLength"                    }, //  3
        { "Exif.Minolta.ThumbnailOffset",              "Exif.Minolta.ThumbnailLength"                    }, //  4
        { "Exif.SonyMinolta.ThumbnailOffset",          "Exif.SonyMinolta.ThumbnailLength"                }, //  5
        { "Exif.Olympus.ThumbnailImage",               0                                                 }, //  6
        { "Exif.Olympus2.ThumbnailImage",              0                                                 }, //  7
        { "Exif.Minolta.Thumbnail",                    0                                                 }, //  8
        { "Exif.PanasonicRaw.PreviewImage",            0                                                 }, //  9
        { "Exif.SamsungPreview.JPEGInterchangeFormat", "Exif.SamsungPreview.JPEGInterchangeFormatLength" }, // 10
        { "Exif.Casio2.PreviewImage",                  0                                                 }  // 11
    };

    const LoaderTiff::Param LoaderTiff::param_[] = {
        { "Image",     "Exif.Image.NewSubfileType",     "1" },  // 0
        { "SubImage1", "Exif.SubImage1.NewSubfileType", "1" },  // 1
        { "SubImage2", "Exif.SubImage2.NewSubfileType", "1" },  // 2
        { "SubImage3", "Exif.SubImage3.NewSubfileType", "1" },  // 3
        { "SubImage4", "Exif.SubImage4.NewSubfileType", "1" },  // 4
        { "SubThumb1", "Exif.SubThumb1.NewSubfileType", "1" },  // 5
        { "Thumbnail", 0,                               0   },  // 6
        { "Image2",    0,                               0   }   // 7
    };

    Loader::AutoPtr Loader::create(PreviewId id, const Image &image)
    {
        if (id < 0 || id >= Loader::getNumLoaders())
            return AutoPtr();

        if (loaderList_[id].imageMimeType_ &&
            std::string(loaderList_[id].imageMimeType_) != image.mimeType())
            return AutoPtr();

        AutoPtr loader = loaderList_[id].create_(id, image, loaderList_[id].parIdx_);

        if (loader.get() && !loader->valid()) loader.reset();
        return loader;
    }

    Loader::Loader(PreviewId id, const Image &image)
        : id_(id), image_(image),
          width_(0), height_(0),
          size_(0),
          valid_(false)
    {
    }

    PreviewProperties Loader::getProperties() const
    {
        PreviewProperties prop;
        prop.id_ = id_;
        prop.size_ = size_;
        prop.width_ = width_;
        prop.height_ = height_;
        return prop;
    }

    PreviewId Loader::getNumLoaders()
    {
        return (PreviewId)EXV_COUNTOF(loaderList_);
    }

    LoaderNative::LoaderNative(PreviewId id, const Image &image, int parIdx)
        : Loader(id, image)
    {
        if (!(0 <= parIdx && static_cast<size_t>(parIdx) < image.nativePreviews().size())) return;
        nativePreview_ = image.nativePreviews()[parIdx];
        width_ = nativePreview_.width_;
        height_ = nativePreview_.height_;
        valid_ = true;
        if (nativePreview_.filter_ == "") {
            size_ = nativePreview_.size_;
        } else {
            size_ = getData().size_;
        }
    }

    Loader::AutoPtr createLoaderNative(PreviewId id, const Image &image, int parIdx)
    {
        return Loader::AutoPtr(new LoaderNative(id, image, parIdx));
    }

    PreviewProperties LoaderNative::getProperties() const
    {
        PreviewProperties prop = Loader::getProperties();
        prop.mimeType_ = nativePreview_.mimeType_;
        if (nativePreview_.mimeType_ == "image/jpeg") {
            prop.extension_ = ".jpg";
        } else if (nativePreview_.mimeType_ == "image/tiff") {
            prop.extension_ = ".tif";
        } else if (nativePreview_.mimeType_ == "image/x-wmf") {
            prop.extension_ = ".wmf";
        } else if (nativePreview_.mimeType_ == "image/x-portable-anymap") {
            prop.extension_ = ".pnm";
        } else {
#ifndef SUPPRESS_WARNINGS
            EXV_WARNING << "Unknown native preview format: " << nativePreview_.mimeType_ << "\n";
#endif
            prop.extension_ = ".dat";
        }
#ifdef EXV_UNICODE_PATH
        prop.wextension_ = s2ws(prop.extension_);
#endif
        return prop;
    }

    DataBuf LoaderNative::getData() const
    {
        if (!valid()) return DataBuf();

        BasicIo &io = image_.io();
        if (io.open() != 0) {
            throw Error(kerDataSourceOpenFailed, io.path(), strError());
        }
        IoCloser closer(io);
        const byte* data = io.mmap();
        if ((long)io.size() < nativePreview_.position_ + static_cast<long>(nativePreview_.size_)) {
#ifndef SUPPRESS_WARNINGS
            EXV_WARNING << "Invalid native preview position or size.\n";
#endif
            return DataBuf();
        }
        if (nativePreview_.filter_ == "") {
            return DataBuf(data + nativePreview_.position_, static_cast<long>(nativePreview_.size_));
        } else if (nativePreview_.filter_ == "hex-ai7thumbnail-pnm") {
            const DataBuf ai7thumbnail = decodeHex(data + nativePreview_.position_, static_cast<long>(nativePreview_.size_));
            const DataBuf rgb = decodeAi7Thumbnail(ai7thumbnail);
            return makePnm(width_, height_, rgb);
        } else if (nativePreview_.filter_ == "hex-irb") {
            const DataBuf psData = decodeHex(data + nativePreview_.position_, static_cast<long>(nativePreview_.size_));
            const byte *record;
            uint32_t sizeHdr;
            uint32_t sizeData;
            if (Photoshop::locatePreviewIrb(psData.pData_, psData.size_, &record, &sizeHdr, &sizeData) != 0) {
#ifndef SUPPRESS_WARNINGS
                EXV_WARNING << "Missing preview IRB in Photoshop EPS preview.\n";
#endif
                return DataBuf();
            }
            return DataBuf(record + sizeHdr + 28, sizeData - 28);
        } else {
            throw Error(kerErrorMessage, "Invalid native preview filter: " + nativePreview_.filter_);
        }
    }

    bool LoaderNative::readDimensions()
    {
        if (!valid()) return false;
        if (width_ != 0 || height_ != 0) return true;

        const DataBuf data = getData();
        if (data.size_ == 0) return false;
        try {
            Image::AutoPtr image = ImageFactory::open(data.pData_, data.size_);
            if (image.get() == 0) return false;
            image->readMetadata();

            width_ = image->pixelWidth();
            height_ = image->pixelHeight();
        } catch (const AnyError& /* error */) {
#ifndef SUPPRESS_WARNINGS
            EXV_WARNING << "Invalid native preview image.\n";
#endif
            return false;
        }
        return true;
    }

    LoaderExifJpeg::LoaderExifJpeg(PreviewId id, const Image &image, int parIdx)
        : Loader(id, image)
    {
        offset_ = 0;
        const ExifData &exifData = image_.exifData();
        ExifData::const_iterator pos = exifData.findKey(ExifKey(param_[parIdx].offsetKey_));
        if (pos != exifData.end() && pos->count() > 0) {
            offset_ = pos->toLong();
        }

        size_ = 0;
        pos = exifData.findKey(ExifKey(param_[parIdx].sizeKey_));
        if (pos != exifData.end() && pos->count() > 0) {
            size_ = pos->toLong();
        }

        if (offset_ == 0 || size_ == 0) return;

        if (param_[parIdx].baseOffsetKey_) {
            pos = exifData.findKey(ExifKey(param_[parIdx].baseOffsetKey_));
            if (pos != exifData.end() && pos->count() > 0) {
                offset_ += pos->toLong();
            }
        }

        if (Safe::add(offset_, size_) > static_cast<uint32_t>(image_.io().size()))
            return;

        valid_ = true;
    }

    Loader::AutoPtr createLoaderExifJpeg(PreviewId id, const Image &image, int parIdx)
    {
        return Loader::AutoPtr(new LoaderExifJpeg(id, image, parIdx));
    }

    PreviewProperties LoaderExifJpeg::getProperties() const
    {
        PreviewProperties prop = Loader::getProperties();
        prop.mimeType_ = "image/jpeg";
        prop.extension_ = ".jpg";
#ifdef EXV_UNICODE_PATH
        prop.wextension_ = EXV_WIDEN(".jpg");
#endif
        return prop;
    }

    DataBuf LoaderExifJpeg::getData() const
    {
        if (!valid()) return DataBuf();
        BasicIo &io = image_.io();

        if (io.open() != 0) {
            throw Error(kerDataSourceOpenFailed, io.path(), strError());
        }
        IoCloser closer(io);

        const Exiv2::byte* base = io.mmap();

        return DataBuf(base + offset_, size_);
    }

    bool LoaderExifJpeg::readDimensions()
    {
        if (!valid()) return false;
        if (width_ || height_) return true;

        BasicIo &io = image_.io();

        if (io.open() != 0) {
            throw Error(kerDataSourceOpenFailed, io.path(), strError());
        }
        IoCloser closer(io);
        const Exiv2::byte* base = io.mmap();

        try {
            Image::AutoPtr image = ImageFactory::open(base + offset_, size_);
            if (image.get() == 0) return false;
            image->readMetadata();

            width_ = image->pixelWidth();
            height_ = image->pixelHeight();
        }
        catch (const AnyError& /* error */ ) {
#ifndef SUPPRESS_WARNINGS
            EXV_WARNING << "Invalid JPEG preview image.\n";
#endif
            return false;
        }

        return true;
    }

    LoaderExifDataJpeg::LoaderExifDataJpeg(PreviewId id, const Image &image, int parIdx)
        : Loader(id, image),
          dataKey_(param_[parIdx].dataKey_)
    {
        ExifData::const_iterator pos = image_.exifData().findKey(dataKey_);
        if (pos != image_.exifData().end()) {
            size_ = pos->sizeDataArea(); // indirect data
            if (size_ == 0 && pos->typeId() == undefined)
                size_ = pos->size(); // direct data
        }

        if (size_ == 0) return;

        valid_ = true;
    }

    Loader::AutoPtr createLoaderExifDataJpeg(PreviewId id, const Image &image, int parIdx)
    {
        return Loader::AutoPtr(new LoaderExifDataJpeg(id, image, parIdx));
    }

    PreviewProperties LoaderExifDataJpeg::getProperties() const
    {
        PreviewProperties prop = Loader::getProperties();
        prop.mimeType_ = "image/jpeg";
        prop.extension_ = ".jpg";
#ifdef EXV_UNICODE_PATH
        prop.wextension_ = EXV_WIDEN(".jpg");
#endif
        return prop;
    }

    DataBuf LoaderExifDataJpeg::getData() const
    {
        if (!valid()) return DataBuf();

        ExifData::const_iterator pos = image_.exifData().findKey(dataKey_);
        if (pos != image_.exifData().end()) {
            DataBuf buf = pos->dataArea(); // indirect data

            if (buf.size_ == 0) { // direct data
                buf = DataBuf(pos->size());
                pos->copy(buf.pData_, invalidByteOrder);
            }

            buf.pData_[0] = 0xff; // fix Minolta thumbnails with invalid jpeg header
            return buf;
        }

        return DataBuf();
    }

    bool LoaderExifDataJpeg::readDimensions()
    {
        if (!valid()) return false;

        DataBuf buf = getData();
        if (buf.size_ == 0) return false;

        try {
            Image::AutoPtr image = ImageFactory::open(buf.pData_, buf.size_);
            if (image.get() == 0) return false;
            image->readMetadata();

            width_ = image->pixelWidth();
            height_ = image->pixelHeight();
        }
        catch (const AnyError& /* error */ ) {
            return false;
        }

        return true;
    }

    LoaderTiff::LoaderTiff(PreviewId id, const Image &image, int parIdx)
        : Loader(id, image),
          group_(param_[parIdx].group_)
    {
        const ExifData &exifData = image_.exifData();

        int offsetCount = 0;
        ExifData::const_iterator pos;

        // check if the group_ contains a preview image
        if (param_[parIdx].checkTag_) {
            pos = exifData.findKey(ExifKey(param_[parIdx].checkTag_));
            if (pos == exifData.end()) return;
            if (param_[parIdx].checkValue_ && pos->toString() != param_[parIdx].checkValue_) return;
        }

        pos = exifData.findKey(ExifKey(std::string("Exif.") + group_ + ".StripOffsets"));
        if (pos != exifData.end()) {
            offsetTag_ = "StripOffsets";
            sizeTag_ = "StripByteCounts";
            offsetCount = pos->value().count();
        }
        else {
            pos = exifData.findKey(ExifKey(std::string("Exif.") + group_ + ".TileOffsets"));
            if (pos == exifData.end()) return;
            offsetTag_ = "TileOffsets";
            sizeTag_ = "TileByteCounts";
            offsetCount = pos->value().count();
        }

        pos = exifData.findKey(ExifKey(std::string("Exif.") + group_ + '.' + sizeTag_));
        if (pos == exifData.end()) return;
        if (offsetCount != pos->value().count()) return;
        for (int i = 0; i < offsetCount; i++) {
            size_ += pos->toLong(i);
        }

        if (size_ == 0) return;

        pos = exifData.findKey(ExifKey(std::string("Exif.") + group_ + ".ImageWidth"));
        if (pos != exifData.end() && pos->count() > 0) {
            width_ = pos->toLong();
        }

        pos = exifData.findKey(ExifKey(std::string("Exif.") + group_ + ".ImageLength"));
        if (pos != exifData.end() && pos->count() > 0) {
            height_ = pos->toLong();
        }

        if (width_ == 0 || height_ == 0) return;

        valid_ = true;
    }

    Loader::AutoPtr createLoaderTiff(PreviewId id, const Image &image, int parIdx)
    {
        return Loader::AutoPtr(new LoaderTiff(id, image, parIdx));
    }

    PreviewProperties LoaderTiff::getProperties() const
    {
        PreviewProperties prop = Loader::getProperties();
        prop.mimeType_ = "image/tiff";
        prop.extension_ = ".tif";
#ifdef EXV_UNICODE_PATH
        prop.wextension_ = EXV_WIDEN(".tif");
#endif
        return prop;
    }

    enum IfdId {
        ifdIdNotSet,
        ifd0Id,
        ifd1Id,
        ifd2Id,
        ifd3Id,
        exifId,
        gpsId,
        iopId,
        mpfId,
        subImage1Id,
        subImage2Id,
        subImage3Id,
        subImage4Id,
        subImage5Id,
        subImage6Id,
        subImage7Id,
        subImage8Id,
        subImage9Id,
        subThumb1Id,
        panaRawId,
        mnId,
        canonId,
        canonAf2Id,
        canonAf3Id,
        canonAfCId,
        canonAfMiAdjId,
        canonAmId,
        canonAsId,
        canonCbId,
        canonCiId,
        canonCsId,
        canonFilId,
        canonFlId,
        canonHdrId,
        canonLeId,
        canonMeId,
        canonMoID,
        canonMvId,
        canonRawBId,
        canonSiId,
        canonCfId,
        canonContrastId,
        canonFcd1Id,
        canonFcd2Id,
        canonFcd3Id,
        canonLiOpId,
        canonMyColorID,
        canonPiId,
        canonPaId,
        canonTiId,
        canonFiId,
        canonPrId,
        canonPreID,
        canonVigCorId,
        canonVigCor2Id,
        canonWbId,
        casioId,
        casio2Id,
        fujiId,
        minoltaId,
        minoltaCs5DId,
        minoltaCs7DId,
        minoltaCsOldId,
        minoltaCsNewId,
        nikon1Id,
        nikon2Id,
        nikon3Id,
        nikonPvId,
        nikonVrId,
        nikonPcId,
        nikonWtId,
        nikonIiId,
        nikonAfId,
        nikonAf21Id,
        nikonAf22Id,
        nikonAFTId,
        nikonFiId,
        nikonMeId,
        nikonFl1Id,
        nikonFl2Id,
        nikonFl3Id,
        nikonSi1Id,
        nikonSi2Id,
        nikonSi3Id,
        nikonSi4Id,
        nikonSi5Id,
        nikonSi6Id,
        nikonLd1Id,
        nikonLd2Id,
        nikonLd3Id,
        nikonLd4Id,
        nikonCb1Id,
        nikonCb2Id,
        nikonCb2aId,
        nikonCb2bId,
        nikonCb3Id,
        nikonCb4Id,
        olympusId,
        olympus2Id,
        olympusCsId,
        olympusEqId,
        olympusRdId,
        olympusRd2Id,
        olympusIpId,
        olympusFiId,
        olympusFe1Id,
        olympusFe2Id,
        olympusFe3Id,
        olympusFe4Id,
        olympusFe5Id,
        olympusFe6Id,
        olympusFe7Id,
        olympusFe8Id,
        olympusFe9Id,
        olympusRiId,
        panasonicId,
        pentaxId,
        pentaxDngId,
        samsung2Id,
        samsungPvId,
        samsungPwId,
        sigmaId,
        sony1Id,
        sony2Id,
        sonyMltId,
        sony1CsId,
        sony1Cs2Id,
        sony2CsId,
        sony2Cs2Id,
        sony2FpId,
        sony2010eId,
        sony1MltCs7DId,
        sony1MltCsOldId,
        sony1MltCsNewId,
        sony1MltCsA100Id,
        tagInfoMvId,
        lastId,
        ignoreId = lastId
    };

    struct TiffImgTagStruct {
        //! Search key for TIFF image tag structure.
        struct Key {
            //! Constructor
            Key(uint16_t t, IfdId g) : t_(t), g_(g) {}
            uint16_t t_;                    //!< %Tag
            IfdId    g_;                    //!< %Group
        };

        //! Comparison operator to compare a TiffImgTagStruct with a TiffImgTagStruct::Key
        bool operator==(const Key& key) const
        {
            return key.g_ == group_ && key.t_ == tag_;
        }

        // DATA
        uint16_t       tag_;            //!< Image tag
        IfdId          group_;          //!< Group that contains the image tag
    }; // struct TiffImgTagStruct

    bool isTiffImageTag(uint16_t tag, IfdId group)
    {
        //! List of TIFF image tags
        static const TiffImgTagStruct tiffImageTags[] = {
            { 0x00fe, ifd0Id }, // Exif.Image.NewSubfileType
            { 0x00ff, ifd0Id }, // Exif.Image.SubfileType
            { 0x0100, ifd0Id }, // Exif.Image.ImageWidth
            { 0x0101, ifd0Id }, // Exif.Image.ImageLength
            { 0x0102, ifd0Id }, // Exif.Image.BitsPerSample
            { 0x0103, ifd0Id }, // Exif.Image.Compression
            { 0x0106, ifd0Id }, // Exif.Image.PhotometricInterpretation
            { 0x010a, ifd0Id }, // Exif.Image.FillOrder
            { 0x0111, ifd0Id }, // Exif.Image.StripOffsets
            { 0x0115, ifd0Id }, // Exif.Image.SamplesPerPixel
            { 0x0116, ifd0Id }, // Exif.Image.RowsPerStrip
            { 0x0117, ifd0Id }, // Exif.Image.StripByteCounts
            { 0x011a, ifd0Id }, // Exif.Image.XResolution
            { 0x011b, ifd0Id }, // Exif.Image.YResolution
            { 0x011c, ifd0Id }, // Exif.Image.PlanarConfiguration
            { 0x0122, ifd0Id }, // Exif.Image.GrayResponseUnit
            { 0x0123, ifd0Id }, // Exif.Image.GrayResponseCurve
            { 0x0124, ifd0Id }, // Exif.Image.T4Options
            { 0x0125, ifd0Id }, // Exif.Image.T6Options
            { 0x0128, ifd0Id }, // Exif.Image.ResolutionUnit
            { 0x0129, ifd0Id }, // Exif.Image.PageNumber
            { 0x012d, ifd0Id }, // Exif.Image.TransferFunction
            { 0x013d, ifd0Id }, // Exif.Image.Predictor
            { 0x013e, ifd0Id }, // Exif.Image.WhitePoint
            { 0x013f, ifd0Id }, // Exif.Image.PrimaryChromaticities
            { 0x0140, ifd0Id }, // Exif.Image.ColorMap
            { 0x0141, ifd0Id }, // Exif.Image.HalftoneHints
            { 0x0142, ifd0Id }, // Exif.Image.TileWidth
            { 0x0143, ifd0Id }, // Exif.Image.TileLength
            { 0x0144, ifd0Id }, // Exif.Image.TileOffsets
            { 0x0145, ifd0Id }, // Exif.Image.TileByteCounts
            { 0x014c, ifd0Id }, // Exif.Image.InkSet
            { 0x014d, ifd0Id }, // Exif.Image.InkNames
            { 0x014e, ifd0Id }, // Exif.Image.NumberOfInks
            { 0x0150, ifd0Id }, // Exif.Image.DotRange
            { 0x0151, ifd0Id }, // Exif.Image.TargetPrinter
            { 0x0152, ifd0Id }, // Exif.Image.ExtraSamples
            { 0x0153, ifd0Id }, // Exif.Image.SampleFormat
            { 0x0154, ifd0Id }, // Exif.Image.SMinSampleValue
            { 0x0155, ifd0Id }, // Exif.Image.SMaxSampleValue
            { 0x0156, ifd0Id }, // Exif.Image.TransferRange
            { 0x0157, ifd0Id }, // Exif.Image.ClipPath
            { 0x0158, ifd0Id }, // Exif.Image.XClipPathUnits
            { 0x0159, ifd0Id }, // Exif.Image.YClipPathUnits
            { 0x015a, ifd0Id }, // Exif.Image.Indexed
            { 0x015b, ifd0Id }, // Exif.Image.JPEGTables
            { 0x0200, ifd0Id }, // Exif.Image.JPEGProc
            { 0x0201, ifd0Id }, // Exif.Image.JPEGInterchangeFormat
            { 0x0202, ifd0Id }, // Exif.Image.JPEGInterchangeFormatLength
            { 0x0203, ifd0Id }, // Exif.Image.JPEGRestartInterval
            { 0x0205, ifd0Id }, // Exif.Image.JPEGLosslessPredictors
            { 0x0206, ifd0Id }, // Exif.Image.JPEGPointTransforms
            { 0x0207, ifd0Id }, // Exif.Image.JPEGQTables
            { 0x0208, ifd0Id }, // Exif.Image.JPEGDCTables
            { 0x0209, ifd0Id }, // Exif.Image.JPEGACTables
            { 0x0211, ifd0Id }, // Exif.Image.YCbCrCoefficients
            { 0x0212, ifd0Id }, // Exif.Image.YCbCrSubSampling
            { 0x0213, ifd0Id }, // Exif.Image.YCbCrPositioning
            { 0x0214, ifd0Id }, // Exif.Image.ReferenceBlackWhite
            { 0x828d, ifd0Id }, // Exif.Image.CFARepeatPatternDim
            { 0x828e, ifd0Id }, // Exif.Image.CFAPattern
        //  { 0x8773, ifd0Id }, // Exif.Image.InterColorProfile
            { 0x8824, ifd0Id }, // Exif.Image.SpectralSensitivity
            { 0x8828, ifd0Id }, // Exif.Image.OECF
            { 0x9102, ifd0Id }, // Exif.Image.CompressedBitsPerPixel
            { 0x9217, ifd0Id }, // Exif.Image.SensingMethod
        };

        // If tag, group is one of the image tags listed above -> bingo!
        if (find(tiffImageTags, TiffImgTagStruct::Key(tag, group))) {
#ifdef EXIV2_DEBUG_MESSAGES
            ExifKey key(tag, groupName(group));
            std::cerr << "Image tag: " << key << " (3)\n";
#endif
            return true;
        }
#ifdef EXIV2_DEBUG_MESSAGES
        std::cerr << "Not an image tag: " << tag << " (4)\n";
#endif
        return false;
    }


    DataBuf LoaderTiff::getData() const
    {
        const ExifData &exifData = image_.exifData();

        ExifData preview;

        // copy tags
        for (ExifData::const_iterator pos = exifData.begin(); pos != exifData.end(); ++pos) {
            if (pos->groupName() == group_) {
                /*
                   Write only the necessary TIFF image tags
                   tags that especially could cause problems are:
                   "NewSubfileType" - the result is no longer a thumbnail, it is a standalone image
                   "Orientation" - this tag typically appears only in the "Image" group. Deleting it ensures
                                   consistent result for all previews, including JPEG
                */
                uint16_t tag = pos->tag();
                if (tag != 0x00fe && tag != 0x00ff && isTiffImageTag(tag, ifd0Id)) {
                    preview.add(ExifKey(tag, "Image"), &pos->value());
                }
            }
        }

        Value &dataValue = const_cast<Value&>(preview["Exif.Image." + offsetTag_].value());

        if (dataValue.sizeDataArea() == 0) {
            // image data are not available via exifData, read them from image_.io()
            BasicIo &io = image_.io();

            if (io.open() != 0) {
                throw Error(kerDataSourceOpenFailed, io.path(), strError());
            }
            IoCloser closer(io);

            const Exiv2::byte* base = io.mmap();

            const Value &sizes = preview["Exif.Image." + sizeTag_].value();

            if (sizes.count() == dataValue.count()) {
                if (sizes.count() == 1) {
                    // this saves one copying of the buffer
                    uint32_t offset = dataValue.toLong(0);
                    uint32_t size = sizes.toLong(0);
                    if (Safe::add(offset, size) <= static_cast<uint32_t>(io.size()))
                        dataValue.setDataArea(base + offset, size);
                }
                else {
                    // FIXME: the buffer is probably copied twice, it should be optimized
                    enforce(size_ <= static_cast<uint32_t>(io.size()), kerCorruptedMetadata);
                    DataBuf buf(size_);
                    uint32_t idxBuf = 0;
                    for (int i = 0; i < sizes.count(); i++) {
                        uint32_t offset = dataValue.toLong(i);
                        uint32_t size = sizes.toLong(i);
                        enforce(Safe::add(idxBuf, size) < size_, kerCorruptedMetadata);
                        if (size!=0 && Safe::add(offset, size) <= static_cast<uint32_t>(io.size()))
                            memcpy(&buf.pData_[idxBuf], base + offset, size);
                        idxBuf += size;
                    }
                    dataValue.setDataArea(buf.pData_, buf.size_);
                }
            }
        }

        // Fix compression value in the CR2 IFD2 image
        if (0 == strcmp(group_, "Image2") && image_.mimeType() == "image/x-canon-cr2") {
            preview["Exif.Image.Compression"] = uint16_t(1);
        }

        // write new image
        MemIo mio;
        IptcData emptyIptc;
        XmpData  emptyXmp;
        TiffParser::encode(mio, 0, 0, Exiv2::littleEndian, preview, emptyIptc, emptyXmp);
        return DataBuf(mio.mmap(), (long) mio.size());
    }

    LoaderXmpJpeg::LoaderXmpJpeg(PreviewId id, const Image &image, int parIdx)
        : Loader(id, image)
    {
        (void)parIdx;

        const XmpData &xmpData = image_.xmpData();

        std::string prefix = "xmpGImg";
        if (xmpData.findKey(XmpKey("Xmp.xmp.Thumbnails[1]/xapGImg:image")) != xmpData.end()) {
            prefix = "xapGImg";
        }

        XmpData::const_iterator imageDatum = xmpData.findKey(XmpKey("Xmp.xmp.Thumbnails[1]/" + prefix + ":image"));
        if (imageDatum == xmpData.end()) return;
        XmpData::const_iterator formatDatum = xmpData.findKey(XmpKey("Xmp.xmp.Thumbnails[1]/" + prefix + ":format"));
        if (formatDatum == xmpData.end()) return;
        XmpData::const_iterator widthDatum = xmpData.findKey(XmpKey("Xmp.xmp.Thumbnails[1]/" + prefix + ":width"));
        if (widthDatum == xmpData.end()) return;
        XmpData::const_iterator heightDatum = xmpData.findKey(XmpKey("Xmp.xmp.Thumbnails[1]/" + prefix + ":height"));
        if (heightDatum == xmpData.end()) return;

        if (formatDatum->toString() != "JPEG") return;

        width_ = widthDatum->toLong();
        height_ = heightDatum->toLong();
        preview_ = decodeBase64(imageDatum->toString());
        size_ = static_cast<uint32_t>(preview_.size_);
        valid_ = true;
    }

    Loader::AutoPtr createLoaderXmpJpeg(PreviewId id, const Image &image, int parIdx)
    {
        return Loader::AutoPtr(new LoaderXmpJpeg(id, image, parIdx));
    }

    PreviewProperties LoaderXmpJpeg::getProperties() const
    {
        PreviewProperties prop = Loader::getProperties();
        prop.mimeType_ = "image/jpeg";
        prop.extension_ = ".jpg";
#ifdef EXV_UNICODE_PATH
        prop.wextension_ = EXV_WIDEN(".jpg");
#endif
        return prop;
    }

    DataBuf LoaderXmpJpeg::getData() const
    {
        if (!valid()) return DataBuf();
        return DataBuf(preview_.pData_, preview_.size_);
    }

    bool LoaderXmpJpeg::readDimensions()
    {
        return valid();
    }

    DataBuf decodeHex(const byte *src, long srcSize)
    {
        // create decoding table
        byte invalid = 16;
        byte decodeHexTable[256];
        for (long i = 0; i < 256; i++) decodeHexTable[i] = invalid;
        for (byte i = 0; i < 10; i++) decodeHexTable[static_cast<byte>('0') + i] = i;
        for (byte i = 0; i < 6; i++) decodeHexTable[static_cast<byte>('A') + i] = i + 10;
        for (byte i = 0; i < 6; i++) decodeHexTable[static_cast<byte>('a') + i] = i + 10;

        // calculate dest size
        long validSrcSize = 0;
        for (long srcPos = 0; srcPos < srcSize; srcPos++) {
            if (decodeHexTable[src[srcPos]] != invalid) validSrcSize++;
        }
        const long destSize = validSrcSize / 2;

        // allocate dest buffer
        DataBuf dest(destSize);

        // decode
        for (long srcPos = 0, destPos = 0; destPos < destSize; destPos++) {
            byte buffer = 0;
            for (int bufferPos = 1; bufferPos >= 0 && srcPos < srcSize; srcPos++) {
                byte srcValue = decodeHexTable[src[srcPos]];
                if (srcValue == invalid) continue;
                buffer |= srcValue << (bufferPos * 4);
                bufferPos--;
            }
            dest.pData_[destPos] = buffer;
        }
        return dest;
    }

    static const char encodeBase64Table[64 + 1] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    DataBuf decodeBase64(const std::string& src)
    {
        const size_t srcSize = src.size();

        // create decoding table
        unsigned long invalid = 64;
        unsigned long decodeBase64Table[256] = {};
        for (unsigned long i = 0; i < 256; i++)
            decodeBase64Table[i] = invalid;
        for (unsigned long i = 0; i < 64; i++)
            decodeBase64Table[(unsigned char)encodeBase64Table[i]] = i;

        // calculate dest size
        unsigned long validSrcSize = 0;
        for (unsigned long srcPos = 0; srcPos < srcSize; srcPos++) {
            if (decodeBase64Table[(unsigned char)src[srcPos]] != invalid) validSrcSize++;
        }
        if (validSrcSize > ULONG_MAX / 3) return DataBuf(); // avoid integer overflow
        const unsigned long destSize = (validSrcSize * 3) / 4;

        // allocate dest buffer
        if (destSize > LONG_MAX) return DataBuf(); // avoid integer overflow
        DataBuf dest(static_cast<long>(destSize));

        // decode
        for (unsigned long srcPos = 0, destPos = 0; destPos < destSize;) {
            unsigned long buffer = 0;
            for (int bufferPos = 3; bufferPos >= 0 && srcPos < srcSize; srcPos++) {
                unsigned long srcValue = decodeBase64Table[(unsigned char)src[srcPos]];
                if (srcValue == invalid) continue;
                buffer |= srcValue << (bufferPos * 6);
                bufferPos--;
            }
            for (int bufferPos = 2; bufferPos >= 0 && destPos < destSize; bufferPos--, destPos++) {
                dest.pData_[destPos] = static_cast<byte>((buffer >> (bufferPos * 8)) & 0xFF);
            }
        }
        return dest;
    }

    DataBuf decodeAi7Thumbnail(const DataBuf &src)
    {
        const byte *colorTable = src.pData_;
        const long colorTableSize = 256 * 3;
        if (src.size_ < colorTableSize) {
#ifndef SUPPRESS_WARNINGS
            EXV_WARNING << "Invalid size of AI7 thumbnail: " << src.size_ << "\n";
#endif
            return DataBuf();
        }
        const byte *imageData = src.pData_ + colorTableSize;
        const long imageDataSize = src.size_ - colorTableSize;
        const bool rle = (imageDataSize >= 3 && imageData[0] == 'R' && imageData[1] == 'L' && imageData[2] == 'E');
        std::string dest;
        for (long i = rle ? 3 : 0; i < imageDataSize;) {
            byte num = 1;
            byte value = imageData[i++];
            if (rle && value == 0xFD) {
                if (i >= imageDataSize) {
#ifndef SUPPRESS_WARNINGS
                    EXV_WARNING << "Unexpected end of image data at AI7 thumbnail.\n";
#endif
                    return DataBuf();
                }
                value = imageData[i++];
                if (value != 0xFD) {
                    if (i >= imageDataSize) {
#ifndef SUPPRESS_WARNINGS
                        EXV_WARNING << "Unexpected end of image data at AI7 thumbnail.\n";
#endif
                        return DataBuf();
                    }
                    num = value;
                    value = imageData[i++];
                }
            }
            for (; num != 0; num--) {
                dest.append(reinterpret_cast<const char*>(colorTable + (3*value)), 3);
            }
        }
        return DataBuf(reinterpret_cast<const byte*>(dest.data()), static_cast<long>(dest.size()));
    }

    DataBuf makePnm(uint32_t width, uint32_t height, const DataBuf &rgb)
    {
        const long expectedSize = static_cast<long>(width * height * 3);
        if (rgb.size_ != expectedSize) {
#ifndef SUPPRESS_WARNINGS
            EXV_WARNING << "Invalid size of preview data. Expected " << expectedSize << " bytes, got " << rgb.size_ << " bytes.\n";
#endif
            return DataBuf();
        }

        const std::string header = "P6\n" + toString(width) + " " + toString(height) + "\n255\n";
        const byte *headerBytes = reinterpret_cast<const byte*>(header.data());

        DataBuf dest(static_cast<long>(header.size() + rgb.size_));
        std::copy(headerBytes, headerBytes + header.size(), dest.pData_);
        std::copy(rgb.pData_, rgb.pData_ + rgb.size_, dest.pData_ + header.size());
        return dest;
    }

}                                       // namespace

// *****************************************************************************
// class member definitions
//namespace Exiv2 {

//    PreviewImage::PreviewImage(const PreviewProperties& properties, DataBuf data)
//        : properties_(properties)
//    {
//        pData_ = data.pData_;
//        size_ = data.size_;
//        std::pair<byte*, long> ret = data.release();
//        (void)(ret);
//    }

//    PreviewImage::~PreviewImage()
//    {
//        delete[] pData_;
//    }

//    PreviewImage::PreviewImage(const PreviewImage& rhs)
//    {
//        properties_ = rhs.properties_;
//        pData_ = new byte[rhs.size_];
//        memcpy(pData_, rhs.pData_, rhs.size_);
//        size_ = rhs.size_;
//    }

//    PreviewImage& PreviewImage::operator=(const PreviewImage& rhs)
//    {
//        if (this == &rhs) return *this;
//        if (rhs.size_ > size_) {
//            delete[] pData_;
//            pData_ = new byte[rhs.size_];
//        }
//        properties_ = rhs.properties_;
//        memcpy(pData_, rhs.pData_, rhs.size_);
//        size_ = rhs.size_;
//        return *this;
//    }

//    long PreviewImage::writeFile(const std::string& path) const
//    {
//        std::string name = path + extension();
//        // Todo: Creating a DataBuf here unnecessarily copies the memory
//        DataBuf buf(pData_, size_);
//        return Exiv2::writeFile(buf, name);
//    }

//#ifdef EXV_UNICODE_PATH
//    long PreviewImage::writeFile(const std::wstring& wpath) const
//    {
//        std::wstring name = wpath + wextension();
//        // Todo: Creating a DataBuf here unnecessarily copies the memory
//        DataBuf buf(pData_, size_);
//        return Exiv2::writeFile(buf, name);
//    }

//#endif
//    DataBuf PreviewImage::copy() const
//    {
//        return DataBuf(pData_, size_);
//    }

//    const byte* PreviewImage::pData() const
//    {
//        return pData_;
//    }

//    uint32_t PreviewImage::size() const
//    {
//        return size_;
//    }

//    std::string PreviewImage::mimeType() const
//    {
//        return properties_.mimeType_;
//    }

//    std::string PreviewImage::extension() const
//    {
//        return properties_.extension_;
//    }

//#ifdef EXV_UNICODE_PATH
//    std::wstring PreviewImage::wextension() const
//    {
//        return properties_.wextension_;
//    }

//#endif
//    uint32_t PreviewImage::width() const
//    {
//        return properties_.width_;
//    }

//    uint32_t PreviewImage::height() const
//    {
//        return properties_.height_;
//    }

//    PreviewId PreviewImage::id() const
//    {
//        return properties_.id_;
//    }

//    PreviewManager::PreviewManager(const Image& image)
//        : image_(image)
//    {
//    }

//    PreviewPropertiesList PreviewManager::getPreviewProperties() const
//    {
//        PreviewPropertiesList list;
//        // go through the loader table and store all successfully created loaders in the list
//        for (PreviewId id = 0; id < Loader::getNumLoaders(); ++id) {
//            Loader::AutoPtr loader = Loader::create(id, image_);
//            if (loader.get() && loader->readDimensions()) {
//                PreviewProperties props = loader->getProperties();
//                DataBuf buf             = loader->getData(); // #16 getPreviewImage()
//                props.size_             = buf.size_;         //     update the size
//                list.push_back(props) ;
//            }
//        }
//        std::sort(list.begin(), list.end(), cmpPreviewProperties);
//        return list;
//    }

//    PreviewImage PreviewManager::getPreviewImage(const PreviewProperties &properties) const
//    {
//        Loader::AutoPtr loader = Loader::create(properties.id_, image_);
//        DataBuf buf;
//        if (loader.get()) {
//            buf = loader->getData();
//        }

//        return PreviewImage(properties, buf);
//    }
//}                                       // namespace Exiv2

Exiv2::DataBuf Exiv2Preview::preview(const Exiv2::Image& image, int targetWidth, int targetHeight,
                                     Exiv2::PreviewProperties *outPreviewProperties, int ignoreThumbnailAt,
                                     bool *outLargerImageAvailable) {
    PreviewProperties properties;
    DataBuf buf;

    // go through the loader table and store all successfully created loaders in the list
    int previewWithMaxResolution = -1;
    int maxPreviewWidth = 0;
    int maxPreviewHeight = 0;
    int thumbnailNumber = 0;
    for (PreviewId id = 0; id < Loader::getNumLoaders(); ++id) {
        Loader::AutoPtr loader = Loader::create(id, image);
        if (loader.get() && loader->readDimensions()) {
            thumbnailNumber++;
            if (thumbnailNumber == ignoreThumbnailAt) {
                continue;
            }
            PreviewProperties props = loader->getProperties();
            if ((int)props.width_ > maxPreviewWidth && (int)props.height_ > maxPreviewHeight) {
                maxPreviewWidth = props.width_;
                maxPreviewHeight = props.height_;
                previewWithMaxResolution = id;
            }

            if ((int)props.width_ >= targetWidth && (int)props.height_ >= targetHeight) {
                properties = loader->getProperties();
                buf = loader->getData(); // #16 getPreviewImage()
                properties.size_ = buf.size_;         //     update the size
                break;
            }
        }
    }

    if (!buf.pData_ && previewWithMaxResolution != -1) {
        Loader::AutoPtr loader = Loader::create(previewWithMaxResolution, image);
        if (loader.get() && loader->readDimensions()) {
            properties = loader->getProperties();
            buf = loader->getData(); // #16 getPreviewImage()
            properties.size_ = buf.size_;         //     update the size
        }
    }

    *outLargerImageAvailable = thumbnailNumber < 3;
    *outPreviewProperties = properties;
    return buf;
}
