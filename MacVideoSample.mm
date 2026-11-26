#include "MacVideoSample.h"
#include "VideoFrameGeometry.h"

#include <QCoreApplication>
#include <QFile>
#include <QScopeGuard>
#include <QtEndian>
#include <array>
#include <cstring>
#include <dlfcn.h>
#include <limits>

extern "C" {
#include <libavutil/ambient_viewing_environment.h>
#include <libavutil/hwcontext_videotoolbox.h>
#include <libavutil/mastering_display_metadata.h>
}

namespace ZoinGallery {
namespace {
template<typename Function>
Function pluginFunction(const char *name)
{
    // Qt loads media plugins locally. Resolve only the already-loaded plugin
    // from Qt's actual search paths, including a deployed application bundle.
    if (auto symbol = dlsym(RTLD_DEFAULT, name))
        return reinterpret_cast<Function>(symbol);
    for (const auto &path : QCoreApplication::libraryPaths()) {
        const auto file = QFile::encodeName(path + QStringLiteral("/multimedia/libffmpegmediaplugin.dylib"));
        if (auto *plugin = dlopen(file.constData(), RTLD_NOLOAD | RTLD_LAZY)) {
            auto symbol = reinterpret_cast<Function>(dlsym(plugin, name));
            dlclose(plugin);
            if (symbol) return symbol;
        }
    }
    return nullptr;
}

void attach(CVPixelBufferRef buffer, CFStringRef key, CFTypeRef value)
{
    if (value) CVBufferSetAttachment(buffer, key, value, kCVAttachmentMode_ShouldPropagate);
}

void attachStaticMetadata(CVPixelBufferRef buffer, const AVFrame &frame)
{
    // Core Video expects the original HEVC SEI wire layout, in network order.
    const auto validUnit = [](AVRational v) { return v.den > 0 && v.num >= 0 && v.num <= v.den; };
    auto data = av_frame_get_side_data(&frame, AV_FRAME_DATA_MASTERING_DISPLAY_METADATA);
    if (data && data->size >= sizeof(AVMasteringDisplayMetadata)
            && !CVBufferHasAttachment(buffer, kCVImageBufferMasteringDisplayColorVolumeKey)) {
        const auto &m = *reinterpret_cast<const AVMasteringDisplayMetadata *>(data->data);
        bool valid = m.has_primaries && m.has_luminance && m.min_luminance.den > 0
            && m.max_luminance.den > 0 && m.min_luminance.num >= 0 && m.max_luminance.num > 0
            && av_cmp_q(m.min_luminance, m.max_luminance) <= 0;
        for (const auto &primary : m.display_primaries)
            valid = valid && validUnit(primary[0]) && validUnit(primary[1]);
        if (valid && validUnit(m.white_point[0]) && validUnit(m.white_point[1])) {
            std::array<uchar, 24> bytes{};
            int offset = 0;
            const auto chromaticity = [&](AVRational v) {
                qToBigEndian<quint16>(av_rescale_rnd(v.num, 50000, v.den, AV_ROUND_NEAR_INF), bytes.data() + offset);
                offset += 2;
            };
            for (int i : {1, 2, 0}) { // SEI stores green, blue, red.
                chromaticity(m.display_primaries[i][0]);
                chromaticity(m.display_primaries[i][1]);
            }
            chromaticity(m.white_point[0]);
            chromaticity(m.white_point[1]);
            const auto luminance = [](AVRational v) { return av_rescale_rnd(v.num, 10000, v.den, AV_ROUND_NEAR_INF); };
            qToBigEndian<quint32>(luminance(m.max_luminance), bytes.data() + 16);
            qToBigEndian<quint32>(luminance(m.min_luminance), bytes.data() + 20);
            QCFType<CFDataRef> payload = CFDataCreate(nullptr, bytes.data(), bytes.size());
            attach(buffer, kCVImageBufferMasteringDisplayColorVolumeKey, payload);
        }
    }
    data = av_frame_get_side_data(&frame, AV_FRAME_DATA_CONTENT_LIGHT_LEVEL);
    if (data && data->size >= sizeof(AVContentLightMetadata)
            && !CVBufferHasAttachment(buffer, kCVImageBufferContentLightLevelInfoKey)) {
        const auto &light = *reinterpret_cast<const AVContentLightMetadata *>(data->data);
        uchar bytes[4];
        qToBigEndian<quint16>(qMin(light.MaxCLL, 65535u), bytes);
        qToBigEndian<quint16>(qMin(light.MaxFALL, 65535u), bytes + 2);
        QCFType<CFDataRef> payload = CFDataCreate(nullptr, bytes, sizeof(bytes));
        attach(buffer, kCVImageBufferContentLightLevelInfoKey, payload);
    }
    data = av_frame_get_side_data(&frame, AV_FRAME_DATA_AMBIENT_VIEWING_ENVIRONMENT);
    if (data && data->size >= sizeof(AVAmbientViewingEnvironment)
            && !CVBufferHasAttachment(buffer, kCVImageBufferAmbientViewingEnvironmentKey)) {
        const auto &env = *reinterpret_cast<const AVAmbientViewingEnvironment *>(data->data);
        if (env.ambient_illuminance.den > 0 && env.ambient_illuminance.num > 0
                && validUnit(env.ambient_light_x) && validUnit(env.ambient_light_y)) {
            const auto scaled = [](AVRational v, int units) { return av_rescale(v.num, units, v.den); };
            const auto illuminance = scaled(env.ambient_illuminance, 10000);
            if (illuminance <= std::numeric_limits<quint32>::max()) {
                uchar bytes[8];
                qToBigEndian<quint32>(illuminance, bytes);
                qToBigEndian<quint16>(scaled(env.ambient_light_x, 50000), bytes + 4);
                qToBigEndian<quint16>(scaled(env.ambient_light_y, 50000), bytes + 6);
                QCFType<CFDataRef> payload = CFDataCreate(nullptr, bytes, sizeof(bytes));
                attach(buffer, kCVImageBufferAmbientViewingEnvironmentKey, payload);
            }
        }
    }
}
} // namespace

QCFType<CVPixelBufferRef> MacVideoSample::copyPixels(QVideoFrame frame, OSType type)
{
    if (!m_pool || m_size != frame.size() || m_type != type) {
        NSDictionary *attributes = @{ (id)kCVPixelBufferWidthKey: @(frame.width()),
            (id)kCVPixelBufferHeightKey: @(frame.height()), (id)kCVPixelBufferPixelFormatTypeKey: @(type),
            (id)kCVPixelBufferIOSurfacePropertiesKey: @{} };
        m_pool = nullptr;
        if (CVPixelBufferPoolCreate(nullptr, nullptr, (CFDictionaryRef)attributes, &m_pool) != kCVReturnSuccess)
            return {};
        m_size = frame.size();
        m_type = type;
    }
    QCFType<CVPixelBufferRef> buffer;
    if (CVPixelBufferPoolCreatePixelBuffer(nullptr, m_pool, &buffer) != kCVReturnSuccess
            || !frame.map(QVideoFrame::ReadOnly))
        return {};
    const auto unmap = qScopeGuard([&] { frame.unmap(); });
    CVBufferRemoveAllAttachments(buffer); // A pool lease must not inherit the previous frame's tags.
    if (CVPixelBufferLockBaseAddress(buffer, 0) != kCVReturnSuccess) return {};
    const auto unlock = qScopeGuard([raw = CVPixelBufferRef(buffer)] { CVPixelBufferUnlockBaseAddress(raw, 0); });
    const bool planar = frame.pixelFormat() == QVideoFrameFormat::Format_YUV420P10;
    const bool sixteen = frame.pixelFormat() == QVideoFrameFormat::Format_P016;
    if (frame.planeCount() != (planar ? 3 : 2)) return {};
    std::array<int, 3> lastRow{};
    for (int p = 0; p < frame.planeCount(); ++p) {
        const int samples = p ? ((frame.width() + 1) / 2) * (planar ? 1 : 2) : frame.width();
        const int rows = p ? (frame.height() + 1) / 2 : frame.height();
        const qint64 bytes = qint64(samples) * sizeof(quint16);
        if (!frame.bits(p) || frame.bytesPerLine(p) < bytes || frame.mappedBytes(p) < bytes)
            return {};
        lastRow[p] = int((frame.mappedBytes(p) - bytes) / frame.bytesPerLine(p));
        // Qt can expose floor(height / 2) mapped chroma rows for an odd
        // 4:2:0 frame. Extend its bottom edge into Core Video's final row.
        const int required = rows - int(p != 0 && frame.height() % 2);
        if (lastRow[p] + 1 < required) return {};
    }
    const auto rowAt = [&](int plane, int y) {
        return reinterpret_cast<const quint16 *>(frame.bits(plane)
            + qMin(y, lastRow[plane]) * frame.bytesPerLine(plane));
    };
    for (int p = 0; p < 2; ++p) {
        const int width = p ? (frame.width() + 1) / 2 * 2 : frame.width();
        const int height = p ? (frame.height() + 1) / 2 : frame.height();
        auto *dst = static_cast<uchar *>(CVPixelBufferGetBaseAddressOfPlane(buffer, p));
        const auto stride = CVPixelBufferGetBytesPerRowOfPlane(buffer, p);
        for (int y = 0; y < height; ++y, dst += stride) {
            const auto *src = rowAt(p, y);
            auto *out = reinterpret_cast<quint16 *>(dst);
            if (planar && p) {
                const auto *v = rowAt(2, y);
                for (int x = 0; x < width / 2; ++x) {
                    out[2*x] = src[x] << 6;
                    out[2*x+1] = v[x] << 6;
                }
            } else if (planar) {
                for (int x = 0; x < width; ++x) out[x] = src[x] << 6;
            } else if (sixteen) {
                for (int x = 0; x < width; ++x) out[x] = src[x] & 0xffc0;
            } else {
                std::memcpy(dst, src, width * sizeof(quint16));
            }
        }
    }
    return buffer;
}

const AVFrame *MacVideoSample::decodedFrame(const QVideoFrame &frame)
{
    if (!m_frameAccessor)
        m_frameAccessor = pluginFunction<decltype(m_frameAccessor)::value_type>("f4DecodedVideoFrameV1");
    return *m_frameAccessor ? (*m_frameAccessor)(&frame) : nullptr;
}

std::optional<MacVideoSample::Upcoming> MacVideoSample::upcoming(const QVideoFrame &current)
{
    if (!m_upcomingAccessor)
        m_upcomingAccessor = pluginFunction<decltype(m_upcomingAccessor)::value_type>("f4UpcomingVideoFrameV1");
    QVideoFrame next;
    qint64 hostTimeUs = 0;
    if (!*m_upcomingAccessor || !(*m_upcomingAccessor)(&current, &next, &hostTimeUs))
        return std::nullopt;
    return Upcoming{std::move(next), CMTimeMake(hostTimeUs, 1000000)};
}

bool MacVideoSample::sameDecoderFrame(const QVideoFrame &a, const QVideoFrame &b)
{
    if (!a.isValid() || !b.isValid() || a.startTime() != b.startTime()) return false;
    const auto *left = decodedFrame(a);
    const auto *right = decodedFrame(b);
    return left && right && left->format == AV_PIX_FMT_VIDEOTOOLBOX && right->format == left->format
        && left->data[3] && left->data[3] == right->data[3];
}

QCFType<CMSampleBufferRef> MacVideoSample::make(const QVideoFrame &frame, CMTime hostTime)
{
    const auto fmt = frame.surfaceFormat();
    if (!frame.isValid() || !VideoColorSpace::isHdr(fmt))
        return {};
    using F = QVideoFrameFormat;
    if (fmt.pixelFormat() != F::Format_P010 && fmt.pixelFormat() != F::Format_YUV420P10
        && fmt.pixelFormat() != F::Format_P016) return {};
    const auto *av = decodedFrame(frame);
    const auto original = av && av->format == AV_PIX_FMT_VIDEOTOOLBOX
        ? reinterpret_cast<CVPixelBufferRef>(av->data[3]) : nullptr;
    const bool full = fmt.colorRange() == F::ColorRange_Full
        || (fmt.colorRange() == F::ColorRange_Unknown && original
            && CVPixelBufferGetPixelFormatType(original) == kCVPixelFormatType_420YpCbCr10BiPlanarFullRange);
    const OSType type = full ? kCVPixelFormatType_420YpCbCr10BiPlanarFullRange
                             : kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange;
    QCFType<CVPixelBufferRef> buffer;
    if (original) {
        const auto surface = CVPixelBufferGetIOSurface(original);
        if (surface && CVPixelBufferGetPixelFormatType(original) == type
                && frame.width() == int(CVPixelBufferGetWidth(original))
                && frame.height() == int(CVPixelBufferGetHeight(original))
                && CVPixelBufferCreateWithIOSurface(nullptr, surface, nullptr, &buffer) == kCVReturnSuccess) {
            // The pixels are exactly the decoder's. Core Video marks which
            // attachments follow them, including the per-frame display metadata
            // produced by VideoToolbox. Keep the decoder's pool lease alive.
            CVBufferPropagateAttachments(original, buffer);
            CVBufferSetAttachment(buffer, CFSTR("F4DecoderSurfaceOwner"), original, kCVAttachmentMode_ShouldNotPropagate);
        }
    }
    if (!buffer) buffer = copyPixels(frame, type);
    if (!buffer) return {};
    // BT.2100 uses BT.2020 primaries. Explicit stream tags take precedence.
    const bool wide = fmt.colorSpace() == F::ColorSpace_BT2020 || fmt.colorSpace() == F::ColorSpace_Undefined;
    const auto primary = av ? av_map_videotoolbox_color_primaries_from_av(av->color_primaries) : nullptr;
    const auto matrix = av ? av_map_videotoolbox_color_matrix_from_av(av->colorspace) : nullptr;
    if (primary || !CVBufferHasAttachment(buffer, kCVImageBufferColorPrimariesKey)) {
        const auto qtPrimaries = wide ? kCVImageBufferColorPrimaries_ITU_R_2020 : kCVImageBufferColorPrimaries_ITU_R_709_2;
        attach(buffer, kCVImageBufferColorPrimariesKey, primary ? primary : qtPrimaries);
    }
    if (matrix || !CVBufferHasAttachment(buffer, kCVImageBufferYCbCrMatrixKey)) {
        auto qtMatrix = wide ? kCVImageBufferYCbCrMatrix_ITU_R_2020 : kCVImageBufferYCbCrMatrix_ITU_R_709_2;
        if (fmt.colorSpace() == F::ColorSpace_BT601) qtMatrix = kCVImageBufferYCbCrMatrix_ITU_R_601_4;
        attach(buffer, kCVImageBufferYCbCrMatrixKey, matrix ? matrix : qtMatrix);
    }
    attach(buffer, kCVImageBufferTransferFunctionKey, fmt.colorTransfer() == F::ColorTransfer_ST2084
        ? kCVImageBufferTransferFunction_SMPTE_ST_2084_PQ : kCVImageBufferTransferFunction_ITU_R_2100_HLG);
    if (av) {
        attach(buffer, kCVImageBufferChromaLocationTopFieldKey, av_map_videotoolbox_chroma_loc_from_av(av->chroma_location));
        attachStaticMetadata(buffer, *av);
    }
    const auto crop = VideoFrameGeometry::visibleViewport(frame);
    if (crop.size() != frame.size()) {
        NSDictionary *aperture = @{ (id)kCVImageBufferCleanApertureWidthKey: @(crop.width()),
            (id)kCVImageBufferCleanApertureHeightKey: @(crop.height()),
            (id)kCVImageBufferCleanApertureHorizontalOffsetKey: @(crop.x() + (crop.width()-frame.width()) / 2.0),
            (id)kCVImageBufferCleanApertureVerticalOffsetKey: @(crop.y() + (crop.height()-frame.height()) / 2.0) };
        attach(buffer, kCVImageBufferCleanApertureKey, (CFDictionaryRef)aperture);
    }
    QCFType<CMVideoFormatDescriptionRef> format;
    QCFType<CMSampleBufferRef> sample;
    const bool scheduled = CMTIME_IS_NUMERIC(hostTime);
    const CMTime duration = frame.endTime() > frame.startTime() && frame.startTime() >= 0
        ? CMTimeMake(frame.endTime() - frame.startTime(), 1000000) : kCMTimeInvalid;
    CMSampleTimingInfo timing = {duration, scheduled ? hostTime : CMClockGetTime(CMClockGetHostTimeClock()),
                                 kCMTimeInvalid};
    if (CMVideoFormatDescriptionCreateForImageBuffer(nullptr, buffer, &format) != noErr
            || CMSampleBufferCreateReadyWithImageBuffer(nullptr, buffer, format, &timing, &sample) != noErr)
        return {};
    if (!scheduled) {
        auto attachments = CMSampleBufferGetSampleAttachmentsArray(sample, true);
        CFDictionarySetValue((CFMutableDictionaryRef)CFArrayGetValueAtIndex(attachments, 0),
                             kCMSampleAttachmentKey_DisplayImmediately, kCFBooleanTrue);
    }
    return sample;
}

} // namespace ZoinGallery
