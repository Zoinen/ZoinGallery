// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#version 440

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;

    float shadowScale;
    vec2 shadowOffset;
    vec2 centerOffset;

    float contrast;
    float brightness;
    float saturation;
    vec4 colorizationColor;
    vec4 blurWeight1;
    vec2 blurWeight2;
    vec4 mask;
    float maskInverted;
    vec4 shadowColor;
    vec4 shadowBlurWeight1;
    vec2 shadowBlurWeight2;
};

layout(binding = 1) uniform sampler2D src;

layout(binding = 2) uniform sampler2D blurSrc1;
layout(binding = 3) uniform sampler2D blurSrc2;
layout(binding = 4) uniform sampler2D blurSrc3;
layout(binding = 5) uniform sampler2D blurSrc4;
layout(binding = 6) uniform sampler2D blurSrc5;

void main() {

    vec4 color = texture(src, texCoord) * blurWeight1[0];

    color += texture(blurSrc1, texCoord) * blurWeight1[1];
    color += texture(blurSrc2, texCoord) * blurWeight1[2];
    color += texture(blurSrc3, texCoord) * blurWeight1[3];

    color += texture(blurSrc4, texCoord) * blurWeight2[0];

    color += texture(blurSrc5, texCoord) * blurWeight2[1];

    // contrast, brightness, saturation and colorization
    color.rgb = (color.rgb - 0.5 * color.a) * (1.0 + contrast) + 0.5 * color.a;
    color.rgb += brightness * color.a;
    float gray = dot(color.rgb, vec3(0.299, 0.587, 0.114));
    color.rgb = gray * colorizationColor.rgb * colorizationColor.a + color.rgb * (1.0 - colorizationColor.a);
    color.rgb = mix(vec3(gray), color.rgb, 1.0 + saturation);

    fragColor = color * qt_Opacity;
}
