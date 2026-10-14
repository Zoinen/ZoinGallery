#version 440
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 viewportSize;
};
void main() {
    vec2 tile = floor(qt_TexCoord0 * viewportSize / 4.0);
    float gray = mix(0.72, 0.9, mod(tile.x + tile.y, 2.0));
    fragColor = vec4(vec3(gray), 1.0) * qt_Opacity;
}
