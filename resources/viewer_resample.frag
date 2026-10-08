#version 450

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 1) flat in int pixelGridAligned;
layout(location = 0) out vec4 fragColor;
layout(binding = 1) uniform sampler2D source;
#ifdef ZOIN_VIDEO_SHADER
layout(binding = 2) uniform sampler2D videoPlane2;
layout(binding = 3) uniform sampler2D videoPlane3;
#if !defined(ZOIN_REFERENCE_SAMPLING) && !defined(ZOIN_LINEAR_VIDEO_PASS)
layout(binding = 4) uniform sampler2D linearVideoSource;
#endif
#endif

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 viewportSize;
    vec2 checkerboardOffset;
    bool showCheckerboard;
    int checkerboardSize;
    float borderRadius;
    bool intermediate;
    bool pixelAlignedIdentity;
    bool nearestNeighbor;
    vec2 sourceExtent;
    bool pixelAligned;
    bool hardwareSampling;
    vec2 itemSize;
    vec4 framebufferRect;
    float framebufferYDirection;
#ifdef ZOIN_VIDEO_SHADER
    mat4 videoColorMatrix;
    vec2 videoFrameSize;
    vec4 videoViewport;
    ivec4 videoPixelInfo;
    ivec4 videoTextureFormats;
    int videoEnabled;
#endif
} ubuf;

#ifdef ZOIN_VIDEO_SHADER
const int VideoFormatArgb8888 = 1;
const int VideoFormatArgb8888Premultiplied = 2;
const int VideoFormatXrgb8888 = 3;
const int VideoFormatBgra8888 = 4;
const int VideoFormatBgra8888Premultiplied = 5;
const int VideoFormatBgrx8888 = 6;
const int VideoFormatAbgr8888 = 7;
const int VideoFormatXbgr8888 = 8;
const int VideoFormatRgba8888 = 9;
const int VideoFormatRgbx8888 = 10;
const int VideoFormatAyuv = 11;
const int VideoFormatAyuvPremultiplied = 12;
const int VideoFormatYuv420P = 13;
const int VideoFormatYuv422P = 14;
const int VideoFormatYv12 = 15;
const int VideoFormatUyvy = 16;
const int VideoFormatYuyv = 17;
const int VideoFormatNv12 = 18;
const int VideoFormatNv21 = 19;
const int VideoFormatImc1 = 20;
const int VideoFormatImc2 = 21;
const int VideoFormatImc3 = 22;
const int VideoFormatImc4 = 23;
const int VideoFormatY8 = 24;
const int VideoFormatY16 = 25;
const int VideoFormatP010 = 26;
const int VideoFormatP016 = 27;
const int VideoFormatJpeg = 29;
const int VideoFormatSamplerRect = 30;
const int VideoFormatYuv420P10 = 31;

const int VideoTextureR8 = 1;
const int VideoTextureR16 = 2;
const int VideoTextureRg8 = 3;
const int VideoTextureRg16 = 4;
const int VideoTextureRgba8 = 5;
const int VideoTextureBgra8 = 6;
#endif

// Decode each source texel's transfer function before convolution.
// These transfer functions operate on the existing RGB data only; ICC profile
// selection, conversion and tagging remain the decoder's responsibility.
vec3 linearize(vec3 color)
{
    return mix(color / 12.92,
               pow((color + 0.055) / 1.055, vec3(2.4)),
               step(vec3(0.04045), color));
}

vec3 encode(vec3 color)
{
    return mix(color * 12.92,
               1.055 * pow(color, vec3(1.0 / 2.4)) - 0.055,
               step(vec3(0.0031308), color));
}

#ifdef ZOIN_VIDEO_SHADER
float videoR8(sampler2D plane, vec2 uv)
{
    return textureLod(plane, uv, 0.0)[ubuf.videoPixelInfo.z];
}

vec2 videoRg8(sampler2D plane, vec2 uv, int textureFormat)
{
    vec4 value = textureLod(plane, uv, 0.0);
    if (textureFormat == VideoTextureRgba8)
        return value.rg;
    if (textureFormat == VideoTextureBgra8)
        return value.ba;
    return value.rg;
}

float videoR16(sampler2D plane, vec2 uv, int textureFormat)
{
    vec4 value = textureLod(plane, uv, 0.0);
    if (textureFormat == VideoTextureR16)
        return value.r;
    if (textureFormat == VideoTextureRg8)
        return (value.r * 255.0 + value.g * 255.0 * 256.0) / 65535.0;
    if (textureFormat == VideoTextureRgba8)
        return (value.r * 255.0 + value.g * 255.0 * 256.0) / 65535.0;
    return 0.0;
}

vec2 videoRg16(sampler2D plane, vec2 uv, int textureFormat)
{
    vec4 value = textureLod(plane, uv, 0.0);
    if (textureFormat == VideoTextureRg16)
        return value.rg;
    if (textureFormat == VideoTextureRgba8)
        return vec2((value.r * 255.0 + value.g * 255.0 * 256.0) / 65535.0,
                    (value.b * 255.0 + value.a * 255.0 * 256.0) / 65535.0);
    return vec2(0.0);
}

ivec2 displayToVideoPixel(ivec2 pixel, ivec2 displaySize, ivec2 visibleSize)
{
    if (ubuf.videoTextureFormats.w != 0)
        pixel.x = displaySize.x - 1 - pixel.x;

    if (ubuf.videoPixelInfo.w == 90)
        return ivec2(pixel.y, visibleSize.y - 1 - pixel.x);
    if (ubuf.videoPixelInfo.w == 180)
        return ivec2(visibleSize.x - 1 - pixel.x,
                     visibleSize.y - 1 - pixel.y);
    if (ubuf.videoPixelInfo.w == 270)
        return ivec2(visibleSize.x - 1 - pixel.y, pixel.x);
    return pixel;
}

vec4 videoColor(vec2 uv, ivec2 pixel, ivec2 rawSize)
{
    int format = ubuf.videoPixelInfo.x;
    vec4 sampleValue = textureLod(source, uv, 0.0);

    if (format == VideoFormatArgb8888
            || format == VideoFormatArgb8888Premultiplied
            || format == VideoFormatXrgb8888)
        return ubuf.videoColorMatrix * sampleValue.gbar;
    if (format == VideoFormatAbgr8888 || format == VideoFormatXbgr8888)
        return ubuf.videoColorMatrix * sampleValue.abgr;
    if (format == VideoFormatBgra8888
            || format == VideoFormatBgra8888Premultiplied
            || format == VideoFormatBgrx8888)
        return ubuf.videoColorMatrix * sampleValue.rgba;
    if (format == VideoFormatJpeg || format == VideoFormatSamplerRect)
        return ubuf.videoColorMatrix * sampleValue.bgra;
    if (format == VideoFormatRgba8888 || format == VideoFormatRgbx8888)
        return ubuf.videoColorMatrix * sampleValue.rgba;

    if (format == VideoFormatAyuv || format == VideoFormatAyuvPremultiplied) {
        float alpha = sampleValue.r;
        vec4 color = ubuf.videoColorMatrix * vec4(sampleValue.gba, 1.0);
        return vec4(color.rgb * alpha, alpha);
    }

    if (format == VideoFormatY8 || format == VideoFormatY16) {
        float y = format == VideoFormatY8
            ? videoR8(source, uv)
            : videoR16(source, uv, ubuf.videoTextureFormats.x);
        return ubuf.videoColorMatrix * vec4(y, y, y, 1.0);
    }

    float y = videoR8(source, uv);
    vec2 chromaUv = uv;
    vec2 chroma = vec2(0.5);
    if (format == VideoFormatYuv420P || format == VideoFormatYuv422P
            || format == VideoFormatYv12 || format == VideoFormatImc1
            || format == VideoFormatImc3) {
        float first = videoR8(videoPlane2, chromaUv);
        float second = videoR8(videoPlane3, chromaUv);
        chroma = (format == VideoFormatYv12 || format == VideoFormatImc1)
            ? vec2(second, first) : vec2(first, second);
    } else if (format == VideoFormatNv12 || format == VideoFormatNv21) {
        chroma = videoRg8(videoPlane2, chromaUv, ubuf.videoTextureFormats.y);
        if (format == VideoFormatNv21)
            chroma = chroma.yx;
    } else if (format == VideoFormatP010 || format == VideoFormatP016) {
        y = videoR16(source, uv, ubuf.videoTextureFormats.x);
        chroma = videoRg16(videoPlane2, chromaUv, ubuf.videoTextureFormats.y);
    } else if (format == VideoFormatYuv420P10) {
        y = videoR16(source, uv, ubuf.videoTextureFormats.x) * 64.0;
        chroma = vec2(videoR16(videoPlane2, chromaUv, ubuf.videoTextureFormats.y),
                      videoR16(videoPlane3, chromaUv, ubuf.videoTextureFormats.z)) * 64.0;
    } else if (format == VideoFormatImc2 || format == VideoFormatImc4) {
        float x = uv.x * 0.5;
        float u = videoR8(videoPlane2, vec2(x + 0.5, uv.y));
        float v = videoR8(videoPlane2, vec2(x, uv.y));
        chroma = format == VideoFormatImc2 ? vec2(u, v) : vec2(v, u);
    } else if (format == VideoFormatUyvy || format == VideoFormatYuyv) {
        float xOffset = (pixel.x & 1) == 0 ? 0.5 : -0.5;
        vec4 packed = textureLod(source,
                                 uv + vec2(xOffset / float(rawSize.x), 0.0),
                                 0.0);
        if (format == VideoFormatUyvy) {
            y = (pixel.x & 1) == 0 ? packed.g : packed.a;
            chroma = packed.xz;
        } else {
            y = (pixel.x & 1) == 0 ? packed.r : packed.b;
            chroma = packed.ga;
        }
    }

    return clamp(ubuf.videoColorMatrix * vec4(y, chroma, 1.0), 0.0, 1.0);
}

vec4 videoTexel(ivec2 displayPixel)
{
    ivec2 displaySize = ivec2(ubuf.videoFrameSize + 0.5);
    ivec2 rawSize = ivec2(textureSize(source, 0));
    ivec2 visibleSize = ivec2(ubuf.videoViewport.zw + 0.5);
    if (any(lessThanEqual(visibleSize, ivec2(0))))
        visibleSize = rawSize;
    ivec2 viewportOrigin = ivec2(ubuf.videoViewport.xy + 0.5);
    displayPixel = clamp(displayPixel, ivec2(0), displaySize - 1);
    ivec2 pixel = displayToVideoPixel(displayPixel, displaySize, visibleSize)
                  + viewportOrigin;
    pixel = clamp(pixel, ivec2(0), rawSize - 1);
    return videoColor((vec2(pixel) + 0.5) / vec2(rawSize), pixel, rawSize);
}

vec4 videoBilinear(vec2 displayPixel)
{
    int format = ubuf.videoPixelInfo.x;
    // Packed/interleaved components and straight-alpha conversion cannot be
    // interpolated as raw planes. Four existing decoded samples suffice;
    // ordinary RGB and planar YUV use the hardware sampler directly.
    if (format == VideoFormatUyvy || format == VideoFormatYuyv
            || format == VideoFormatImc2 || format == VideoFormatImc4
            || format == VideoFormatArgb8888 || format == VideoFormatBgra8888
            || format == VideoFormatAbgr8888 || format == VideoFormatRgba8888
            || format == VideoFormatAyuv || format == VideoFormatAyuvPremultiplied) {
        ivec2 base = ivec2(floor(displayPixel));
        vec2 phase = fract(displayPixel);
        return mix(mix(videoTexel(base), videoTexel(base + ivec2(1, 0)), phase.x),
                   mix(videoTexel(base + ivec2(0, 1)), videoTexel(base + ivec2(1, 1)), phase.x),
                   phase.y);
    }
    vec2 displaySize = ubuf.videoFrameSize;
    ivec2 rawSize = textureSize(source, 0);
    vec2 visibleSize = ubuf.videoViewport.zw;
    if (any(lessThanEqual(visibleSize, vec2(0.0))))
        visibleSize = vec2(rawSize);
    vec2 pixel = clamp(displayPixel, vec2(0.0), displaySize - 1.0);
    if (ubuf.videoTextureFormats.w != 0)
        pixel.x = displaySize.x - 1.0 - pixel.x;
    if (ubuf.videoPixelInfo.w == 90)
        pixel = vec2(pixel.y, visibleSize.y - 1.0 - pixel.x);
    else if (ubuf.videoPixelInfo.w == 180)
        pixel = visibleSize - 1.0 - pixel;
    else if (ubuf.videoPixelInfo.w == 270)
        pixel = vec2(visibleSize.x - 1.0 - pixel.y, pixel.x);
    pixel = clamp(pixel + ubuf.videoViewport.xy, vec2(0.0), vec2(rawSize) - 1.0);
    return videoColor((pixel + 0.5) / vec2(rawSize), ivec2(floor(pixel)), rawSize);
}
#endif

vec4 sourceTexel(ivec2 pixel)
{
#ifdef ZOIN_VIDEO_SHADER
    return ubuf.videoEnabled != 0 ? videoTexel(pixel)
                                  : texelFetch(source, pixel, 0);
#else
    return texelFetch(source, pixel, 0);
#endif
}

vec4 linearSample(ivec2 pixel)
{
#if defined(ZOIN_VIDEO_SHADER) && !defined(ZOIN_REFERENCE_SAMPLING) && !defined(ZOIN_LINEAR_VIDEO_PASS)
    // Full float32 premultiplied linear texels retain the exact per-source-
    // pixel conversion. Only convolution/encoding remains in the final pass.
    if (ubuf.videoEnabled == 2)
        return texelFetch(linearVideoSource, pixel, 0);
#endif
    vec4 color = sourceTexel(pixel);
    if (color.a <= 0.0)
        return vec4(0.0);
    return vec4(linearize(clamp(color.rgb / color.a, 0.0, 1.0)) * color.a,
                color.a);
}

float dither(vec2 outputPosition)
{
    ivec2 cell = ivec2(floor(outputPosition)) & ivec2(7);
    int index = 0;
    for (int bit = 0; bit < 3; ++bit) {
        int x = (cell.x >> bit) & 1;
        int y = (cell.y >> bit) & 1;
        index |= (((x ^ y) << 1) | x) << (4 - 2 * bit);
    }
    return float(2 * index - 63) / 32640.0;
}

// BC cubic with predefined downsampling parameters. Scale the argument,
// not the sample spacing: every source texel inside the widened support must
// contribute. The preceding half-size pyramid bounds that support to 8 taps.
float kernel(float distance)
{
    float x = abs(distance);
    const float B = -0.4;
    const float C = 0.8;
    if (x < 1.0)
        return ((2.0 - 1.5 * B - C) * x + (-3.0 + 2.0 * B + C))
                * x * x + (1.0 - B / 3.0);
    if (x < 2.0)
        return (((-B / 6.0 - C) * x + (B + 5.0 * C)) * x
                + (-2.0 * B - 8.0 * C)) * x + (4.0 * B / 3.0 + 4.0 * C);
    return 0.0;
}

// Interpolating Catmull-Rom cubic (B=0, C=0.5). Unlike the reduction kernel,
// this preserves source samples while retaining contrast between them.
vec4 magnificationWeights(float phase)
{
    return ((vec4(-0.5, 1.5, -1.5, 0.5) * phase
             + vec4(1.0, -2.5, 2.0, -0.5)) * phase
             + vec4(-0.5, 0.0, 0.5, 0.0)) * phase
             + vec4(0.0, 1.0, 0.0, 0.0);
}

vec4 magnify(vec2 position, vec2 size)
{
    vec2 base = floor(position);
    vec2 phase = position - base;
    vec4 wx = magnificationWeights(phase.x);
    vec4 wy = magnificationWeights(phase.y);
    vec4 color = vec4(0.0);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            vec2 pixel = clamp(base + vec2(x - 1, y - 1), vec2(0.0), size - 1.0);
            // Hardware bilinear reads would mix encoded values before their
            // transfer decoding. Keep all 16 samples in linear light instead.
            color += linearSample(ivec2(pixel)) * wx[x] * wy[y];
        }
    }
    return color;
}

vec4 encodeFilteredColor(vec4 color, vec2 outputPosition)
{
    // Negative cubic lobes must not escape the premultiplied RGBA domain.
    color.a = clamp(color.a, 0.0, 1.0);
    color.rgb = clamp(color.rgb, vec3(0.0), vec3(color.a));
    if (color.a <= 0.0)
        return vec4(0.0);
    color.rgb = encode(color.rgb / color.a) * color.a;
    // Every completed pass is stored as encoded RGBA8.
    // Alpha remains linear and is never dithered.
    color.rgb = clamp(color.rgb + dither(outputPosition),
                      vec3(0.0), vec3(color.a));
    return color;
}

vec4 resample(vec2 uv)
{
#ifdef ZOIN_VIDEO_SHADER
    vec2 size = ubuf.videoEnabled != 0
        ? ubuf.videoFrameSize : vec2(textureSize(source, 0));
#else
    vec2 size = vec2(textureSize(source, 0));
#endif
    // Floor-half levels retain the exact 2:1 grid. An odd trailing source
    // texel can contribute to the edge filter without stretching every sample.
    vec2 extent = all(greaterThan(ubuf.sourceExtent, vec2(0.0)))
        ? min(ubuf.sourceExtent, size) : size;
    vec2 sampleUv = uv * extent / size;
    // Only short presentation flights opt into encoded hardware bilinear.
    // Resting, panning and zooming retain the linear-light quality path below.
    if (ubuf.hardwareSampling && !ubuf.nearestNeighbor) {
#ifdef ZOIN_VIDEO_SHADER
        if (ubuf.videoEnabled != 0)
            return videoBilinear(sampleUv * size - 0.5);
#endif
        return textureLod(source, sampleUv, 0.0);
    }
    if (ubuf.nearestNeighbor || (ubuf.pixelAlignedIdentity
            && (!ubuf.pixelAligned || pixelGridAligned != 0))) {
        ivec2 pixel = ivec2(clamp(floor(sampleUv * size), vec2(0.0),
                                  size - 1.0));
        return sourceTexel(pixel);
    }
    // Derivatives describe physical output pixels, including DPR and any
    // animated ancestor transform. viewportSize alone cannot describe those.
    vec2 derivativeX = dFdx(sampleUv) * size;
    vec2 derivativeY = dFdy(sampleUv) * size;
    vec2 footprint = pixelGridAligned != 0
        ? extent / ubuf.viewportSize
        : vec2(length(vec2(derivativeX.x, derivativeY.x)),
               length(vec2(derivativeX.y, derivativeY.y)));
    if (max(footprint.x, footprint.y) <= 1.0001)
        return encodeFilteredColor(magnify(uv * extent - 0.5, size),
                                   uv * ubuf.viewportSize);

    vec2 scale = 1.0 / max(footprint, vec2(1.0));
    vec2 position = uv * extent - 0.5;
    vec2 base = floor(position);
    vec2 phase = position - base;
    vec4 total = vec4(0.0);
    float totalWeight = 0.0;
#if defined(ZOIN_VIDEO_SHADER) && !defined(ZOIN_REFERENCE_SAMPLING)
    // Horizontal coefficients are identical in every row. Compute them once,
    // preserving the original expression and nonzero accumulation order.
    float xWeights[8];
    for (int x = -3; x <= 4; ++x) {
        float dx = float(x) - phase.x;
        xWeights[x + 3] = footprint.x <= 1.0001 ? max(0.0, 1.0 - abs(dx))
                                               : kernel(dx * scale.x);
    }
#endif
    for (int y = -3; y <= 4; ++y) {
        float dy = float(y) - phase.y;
        float wy = footprint.y <= 1.0001 ? max(0.0, 1.0 - abs(dy))
                                         : kernel(dy * scale.y);
#if defined(ZOIN_VIDEO_SHADER) && !defined(ZOIN_REFERENCE_SAMPLING)
        if (wy == 0.0)
            continue;
#endif
        for (int x = -3; x <= 4; ++x) {
#if defined(ZOIN_VIDEO_SHADER) && !defined(ZOIN_REFERENCE_SAMPLING)
            float wx = xWeights[x + 3];
#else
            float dx = float(x) - phase.x;
            float wx = footprint.x <= 1.0001 ? max(0.0, 1.0 - abs(dx))
                                             : kernel(dx * scale.x);
#endif
            float weight = wy * wx;
#if defined(ZOIN_VIDEO_SHADER) && !defined(ZOIN_REFERENCE_SAMPLING)
            if (weight == 0.0)
                continue;
#endif
            vec2 pixel = clamp(base + vec2(x, y), vec2(0.0), size - 1.0);
            // Fetch before transfer decoding, without bilinear interpolation
            // in encoded values or an implicit mip selection.
            total += linearSample(ivec2(pixel)) * weight;
            totalWeight += weight;
        }
    }
    return encodeFilteredColor(total / max(totalWeight, 0.00001),
                               uv * ubuf.viewportSize);
}

void main()
{
#ifdef ZOIN_LINEAR_VIDEO_PASS
    // An offscreen source-data pass at exactly one texel per display pixel:
    // no output transfer function, dithering, presentation fade or filtering.
    ivec2 size = ivec2(ubuf.videoFrameSize + 0.5);
    ivec2 pixel = clamp(ivec2(floor(qt_TexCoord0 * vec2(size))), ivec2(0), size - 1);
    fragColor = linearSample(pixel);
#else
    // Aligned vertices guarantee an exact physical-pixel grid. Recover its
    // centers instead of retaining projection-dependent interpolation noise;
    // direct and cached passes must evaluate the same filter coordinates.
    // Motion and intermediate passes keep their existing continuous sampling.
    vec2 uv = pixelGridAligned != 0
        ? (floor(qt_TexCoord0 * ubuf.viewportSize) + 0.5) / ubuf.viewportSize
        : qt_TexCoord0;
    vec4 color = resample(uv);
    vec2 position = uv * ubuf.viewportSize;
    if (ubuf.showCheckerboard) {
        vec2 cell = floor((position + ubuf.checkerboardOffset)
                          / float(max(ubuf.checkerboardSize, 1)));
        float gray = mod(cell.x + cell.y, 2.0) == 0.0 ? 1.0 : 0.8;
        color += (1.0 - color.a) * vec4(vec3(gray), 1.0);
    }
    if (ubuf.borderRadius > 0.0) {
        vec2 halfSize = ubuf.viewportSize * 0.5;
        vec2 d = abs(position - halfSize) - (halfSize - ubuf.borderRadius);
        float distance = length(max(d, vec2(0.0)))
                         + min(max(d.x, d.y), 0.0) - ubuf.borderRadius;
        color *= 1.0 - smoothstep(-0.8, 0.8, distance);
    }
    // Pyramid texels are image data, independent of presentation fades.
    fragColor = color * (ubuf.intermediate ? 1.0 : ubuf.qt_Opacity);
#endif
}
