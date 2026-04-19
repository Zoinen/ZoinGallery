#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D source;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 viewportSize;
    float sharpenAmount;
    bool showCheckerboard;
    int checkerboardSize;
    float borderRadius;
} ubuf;

void main() {
    //fragColor = texture(source, qt_TexCoord0);
    vec4 outColor;

    vec2 texCoord = qt_TexCoord0;

    // Use texture size for offset calculation
    vec2 texSize = vec2(textureSize(source, 0));
    vec2 texOffset = 1.0 / texSize;

    // Unrolled blur calculation
    vec4 blurColor = vec4(0.0);

    blurColor += texture(source, texCoord + vec2(-1.0, -1.0) * texOffset) * 1.0;
    blurColor += texture(source, texCoord + vec2( 0.0, -1.0) * texOffset) * 2.0;
    blurColor += texture(source, texCoord + vec2( 1.0, -1.0) * texOffset) * 1.0;

    blurColor += texture(source, texCoord + vec2(-1.0,  0.0) * texOffset) * 2.0;
    blurColor += texture(source, texCoord + vec2( 0.0,  0.0) * texOffset) * 4.0;
    blurColor += texture(source, texCoord + vec2( 1.0,  0.0) * texOffset) * 2.0;

    blurColor += texture(source, texCoord + vec2(-1.0,  1.0) * texOffset) * 1.0;
    blurColor += texture(source, texCoord + vec2( 0.0,  1.0) * texOffset) * 2.0;
    blurColor += texture(source, texCoord + vec2( 1.0,  1.0) * texOffset) * 1.0;

    blurColor /= 16.0;

    vec4 originalColor = texture(source, texCoord);
    vec4 mask = originalColor - blurColor;

    vec4 sharpenedPixel = originalColor + mask * ubuf.sharpenAmount;

    if (ubuf.showCheckerboard && sharpenedPixel.a < 1.0) {
        vec4 texColor = sharpenedPixel;

        // Calculate the position on the viewport
        vec2 pos = qt_TexCoord0 * ubuf.viewportSize;

        // Optimized checkerboard calculation
        vec2 checkerCoord = floor(pos / float(ubuf.checkerboardSize));
        float checker = mod(checkerCoord.x + checkerCoord.y, 2.0);
        vec4 checkerColor = checker == 0.0 ? vec4(1.0) : vec4(0.8, 0.8, 0.8, 1.0);

        outColor = texColor + (1.0 - texColor.a) * checkerColor;
    } else {
        outColor = sharpenedPixel;
    }

    // Rectangle dimensions match the viewport size
    vec2 rectSize = ubuf.viewportSize;
    vec2 halfSize = rectSize * 0.5;

    // Optimized rounded rectangle mask calculation
    vec2 p = qt_TexCoord0 * rectSize;
    vec2 d = abs(p - halfSize) - (halfSize - ubuf.borderRadius);
    float distance = length(max(d, vec2(0.0))) + min(max(d.x, d.y), 0.0) - ubuf.borderRadius;

    // Anti-aliasing
    float borderWidth = 0.8;
    float alpha = 1.0 - smoothstep(-borderWidth, borderWidth, distance);

    // Edge adjustment
    if (p.x < 1.0 || p.y < 1.0 || p.x > rectSize.x - 1.0 || p.y > rectSize.y - 1.0) {
        alpha = min(1.0, alpha * 1.1);
    }

    // Final color output
    fragColor = outColor * alpha * ubuf.qt_Opacity;
}
