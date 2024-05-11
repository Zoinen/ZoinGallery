#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D source;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    // vec2 viewportSize;
    // float sharpenAmount;

    float fov; // Field of view in degrees
    float tilt; // Tilt angle in degrees
    float pan; // Pan angle in degrees
    float aspect;

} ubuf;

const float PI = 3.14159265359;

vec2 sphericalToEquirectangular(vec3 dir) {
    float theta = acos(-dir.y);
    float phi = atan(-dir.x, -dir.z);
    vec2 uv = vec2(phi / (2.0 * PI), theta / PI);
    return uv;
}

void main() {
    vec2 uv = qt_TexCoord0;
    uv = 2.0 * uv - 1.0;

    float fovRadians = radians(ubuf.fov);
    float tiltRadians = radians(ubuf.tilt);
    float panRadians = radians(ubuf.pan);

    float aspectRatio = ubuf.aspect;
    float fovScaleY = tan(fovRadians / 2.0);
    float fovScaleX = fovScaleY * aspectRatio;

    vec3 dir = normalize(vec3(
        uv.x * fovScaleX,
        uv.y * fovScaleY,
        1.0
    ));

    // Apply tilt rotation
    float cosTilt = cos(-tiltRadians);
    float sinTilt = sin(-tiltRadians);
    dir = vec3(
        dir.x,
        dir.y * cosTilt - dir.z * sinTilt,
        dir.y * sinTilt + dir.z * cosTilt
    );

    // Apply pan rotation
    float cosPan = cos(-panRadians);
    float sinPan = sin(-panRadians);
    dir = vec3(
        dir.x * cosPan - dir.z * sinPan,
        dir.y,
        dir.x * sinPan + dir.z * cosPan
    );

    vec2 equirectangularUV = sphericalToEquirectangular(dir);

    // Wrap texture coordinates
    if (equirectangularUV.x < 0.0) {
        equirectangularUV.x += 1.0;
    } else if (equirectangularUV.x > 1.0) {
        equirectangularUV.x -= 1.0;
    }

    // Sample the equirectangular texture
    vec3 color = texture(source, equirectangularUV).rgb;

    fragColor = vec4(color, 1.0) * ubuf.qt_Opacity;
}
