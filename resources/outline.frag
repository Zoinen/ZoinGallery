#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec4 outlineColor;
    float outlineWidth;
    float outlineOpacity;
    float blurRadius;
    vec4 blurColor;
    float blurOpacity;
    vec2 textureSize;
};
layout(binding = 1) uniform sampler2D source;

float sampleAlpha(vec2 uv) {
    return texture(source, uv).a;
}

// Define 'over' function for pre-multiplied alpha
vec4 over(vec4 fg, vec4 bg) {
    return fg + bg * (1.0 - fg.a);
}

void main() {
    vec2 pixelSize = 1.0 / textureSize;
    float baseAlpha = sampleAlpha(qt_TexCoord0);

    // Blur calculation
    float blur = 0.0;
    float totalWeight = 0.0;
    for (float y = -blurRadius; y <= blurRadius; y += 1.0) {
        for (float x = -blurRadius; x <= blurRadius; x += 1.0) {
            vec2 offset = vec2(x, y) * pixelSize;
            float weight = exp(-(x*x + y*y) / (2.0 * blurRadius * blurRadius));
            blur += weight * sampleAlpha(qt_TexCoord0 + offset);
            totalWeight += weight;
        }
    }
    blur /= totalWeight;

    // Outline calculation
    float outline = 0.0;
    float sigma = outlineWidth / 2.0;
    float sigma2 = sigma * sigma;
    float denom = 2.0 * sigma2;

    for (float y = -outlineWidth; y <= outlineWidth; y += 0.5) {
        for (float x = -outlineWidth; x <= outlineWidth; x += 0.5) {
            if (x == 0.0 && y == 0.0) continue;

            vec2 offset = vec2(x, y) * pixelSize;
            float dist2 = x * x + y * y;
            float gauss = exp(-dist2 / denom) / (3.14159 * denom);

            outline += gauss * sampleAlpha(qt_TexCoord0 + offset);
        }
    }

    outline = smoothstep(0.0, 0.5, outline);

    vec4 sourceColor = texture(source, qt_TexCoord0);
    vec4 outlineColorWithOpacity = vec4(outlineColor.rgb, outlineColor.a * outlineOpacity);
    vec4 blurColorWithOpacity = vec4(blurColor.rgb, blurColor.a * blurOpacity);

    // Pre-multiply colors
    outlineColorWithOpacity.rgb *= outlineColorWithOpacity.a;
    blurColorWithOpacity.rgb *= blurColorWithOpacity.a;
    sourceColor.rgb *= sourceColor.a;

    // Compute contributions
    vec4 blurContribution = blur * blurColorWithOpacity;
    vec4 outlineContribution = outline * outlineColorWithOpacity;

    // Combine colors using 'over' operation
    fragColor = over(sourceColor, over(outlineContribution, blurContribution));

    // Apply overall opacity
    fragColor.rgb *= qt_Opacity;
    fragColor.a *= qt_Opacity;
}
