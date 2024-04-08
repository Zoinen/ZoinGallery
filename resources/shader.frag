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

/*void main() {
    vec2 texSize = vec2(textureSize(source, 0));
    vec2 xy = ubuf.viewportSize * qt_TexCoord0;
    // float r = (x < 10000 ? mod(qt_TexCoord0.x, 255.0) : 0);
    // float g = (y < 10000 ? mod(qt_TexCoord0.y, 255.0) : 0);

    vec2 imgPos = ubuf.viewportSize / texSize * xy;
    float r = 0;
    float g = 0;
    if (int(xy.x) == 100 || int(xy.y) == 100) {
        r = 1;
        g = 1;
    }
    fragColor = texelFetch(source, ivec2(xy), 0); //vec4(r, g, 0, 1);
}
*/

const int KERNEL_SIZE = 9; // Assuming a 3x3 kernel for simplicity
const float kernel[KERNEL_SIZE] = float[](1.0, 2.0, 1.0, 2.0, 4.0, 2.0, 1.0, 2.0, 1.0);

void main() {
    vec4 outColor;

    vec2 texSize = vec2(textureSize(source, 0));
    vec2 texOffset = 1.0 / ubuf.viewportSize; // Use viewport size for offset calculation

    vec4 blurColor = vec4(0.0);
    for (int i = 0; i < KERNEL_SIZE; i++) {
        // Calculate offsets based on the viewport size
        int x = i % 3 - 1;
        int y = i / 3 - 1;
        vec2 offset = vec2(x, y) * texOffset;
        blurColor += texture(source, qt_TexCoord0 + offset) * kernel[i];
    }
    blurColor /= 16.0;

    vec4 originalColor = texture(source, qt_TexCoord0);
    vec4 mask = originalColor - blurColor;

    // bool downscaling = ubuf.viewportSize.x < texSize.x || ubuf.viewportSize.y < texSize.y;
    // if (downscaling) {
        vec4 sharpenedPixel = originalColor + mask * ubuf.sharpenAmount;

        if (ubuf.showCheckerboard && sharpenedPixel.a < 1.0) {
            vec4 texColor = sharpenedPixel; // Sample the texture color

            // Calculate the physical position of the fragment on the viewport
            vec2 pos = qt_TexCoord0 * ubuf.viewportSize;

            // Determine whether we're on an "on" or "off" square of the checkerboard
            int checkerX = int(floor(pos.x / ubuf.checkerboardSize));
            int checkerY = int(floor(pos.y / ubuf.checkerboardSize));
            bool isCheckerOn = (checkerX + checkerY) % 2 == 0;

            vec4 checkerColor = isCheckerOn ? vec4(1.0, 1.0, 1.0, 1.0) : vec4(0.8, 0.8, 0.8, 1.0); // White or black

            // Smooth blending based on the alpha channel of the image
            // float blendFactor = texColor.a; // Use the alpha channel as the blend factor
            // vec4 blendedColor = mix(checkerColor, texColor, blendFactor); // Blend checkerboard and texture color

            // fragColor = blendedColor;
            outColor = texColor + (1.0 - texColor.a) * checkerColor;
        }
        else {
            outColor = sharpenedPixel;
        }
    // }
    // else {
        // fragColor = originalColor;
    // }

    // Rectangle dimensions match the viewport size
    vec2 rectSize = ubuf.viewportSize;

    // Calculate distance from the edge of the rounded rectangle
    vec2 p = qt_TexCoord0 * rectSize;
    vec2 d = abs(p - rectSize * 0.5) - (rectSize * 0.5 - ubuf.borderRadius);
    float distance = min(max(d.x, d.y), 0.0) + length(max(d, 0.0)) - ubuf.borderRadius;

    // Anti-aliasing width, as a function of viewport size for consistency
    float borderWidth = 0.8; // You can adjust this based on desired edge sharpness
    float alpha = 1 - smoothstep(-borderWidth, borderWidth, distance);
    if ((p.x < 1 || p.y < 1 || p.x > rectSize.x - 1 || p.y > rectSize.y - 1)/* && alpha > 0.8*/) {
        alpha = min(1, alpha * 1.1);
    }

    // // Set the color
    // vec4 fillColor = texture(source, qt_TexCoord0); // Sample from the texture

    // Determine color based on the distance, applying anti-aliasing to the border
    fragColor = outColor * alpha * ubuf.qt_Opacity; //mix(fillColor, borderColor, alpha);
}
