#version 450

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(binding = 1) uniform sampler2D source;

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
    vec2 sourceExtent;
} ubuf;

// sRGB texture reads linearize the samples before convolution.
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

vec4 linearSample(ivec2 pixel)
{
    vec4 color = texelFetch(source, pixel, 0);
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

vec4 resample(vec2 uv)
{
    vec2 size = vec2(textureSize(source, 0));
    // Floor-half levels retain the exact 2:1 grid. An odd trailing source
    // texel can contribute to the edge filter without stretching every sample.
    vec2 extent = all(greaterThan(ubuf.sourceExtent, vec2(0.0)))
        ? min(ubuf.sourceExtent, size) : size;
    vec2 sampleUv = uv * extent / size;
    if (ubuf.pixelAlignedIdentity) {
        ivec2 pixel = ivec2(clamp(floor(sampleUv * size), vec2(0.0),
                                  size - 1.0));
        return texelFetch(source, pixel, 0);
    }
    // Derivatives describe physical output pixels, including DPR and any
    // animated ancestor transform. viewportSize alone cannot describe those.
    vec2 derivativeX = dFdx(sampleUv) * size;
    vec2 derivativeY = dFdy(sampleUv) * size;
    vec2 footprint = vec2(length(vec2(derivativeX.x, derivativeY.x)),
                          length(vec2(derivativeX.y, derivativeY.y)));
    if (max(footprint.x, footprint.y) <= 1.0001)
        return texture(source, sampleUv);

    vec2 scale = 1.0 / max(footprint, vec2(1.0));
    vec2 position = uv * extent - 0.5;
    vec2 base = floor(position);
    vec2 phase = position - base;
    vec4 total = vec4(0.0);
    float totalWeight = 0.0;
    for (int y = -3; y <= 4; ++y) {
        float dy = float(y) - phase.y;
        float wy = footprint.y <= 1.0001 ? max(0.0, 1.0 - abs(dy))
                                         : kernel(dy * scale.y);
        for (int x = -3; x <= 4; ++x) {
            float dx = float(x) - phase.x;
            float wx = footprint.x <= 1.0001 ? max(0.0, 1.0 - abs(dx))
                                             : kernel(dx * scale.x);
            float weight = wy * wx;
            vec2 pixel = clamp(base + vec2(x, y), vec2(0.0), size - 1.0);
            // Fetch before transfer decoding, without bilinear interpolation
            // in encoded values or an implicit mip selection.
            total += linearSample(ivec2(pixel)) * weight;
            totalWeight += weight;
        }
    }
    vec4 color = total / max(totalWeight, 0.00001);
    // Negative cubic lobes must not escape the premultiplied RGBA domain.
    color.a = clamp(color.a, 0.0, 1.0);
    color.rgb = clamp(color.rgb, vec3(0.0), vec3(color.a));
    if (color.a <= 0.0)
        return vec4(0.0);
    color.rgb = encode(color.rgb / color.a) * color.a;
    // Every completed pass is stored as encoded RGBA8
    // Alpha remains linear and is never dithered.
    color.rgb = clamp(color.rgb + dither(uv * ubuf.viewportSize),
                      vec3(0.0), vec3(color.a));
    return color;
}

void main()
{
    vec4 color = resample(qt_TexCoord0);
    vec2 position = qt_TexCoord0 * ubuf.viewportSize;
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
}
