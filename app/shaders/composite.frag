#version 330 core
in vec2 vUv;
out vec4 fragColor;
uniform sampler2D uImage;
uniform int uFxaa;
uniform vec2 uTexel;

// FXAA (after T. Lottes' console variant): cheap edge-directed anti-aliasing used when
// multisampling is too expensive (software rasterizers).
vec3 fxaa(vec2 uv)
{
    const vec3 lumaWeights = vec3(0.299, 0.587, 0.114);
    vec3 rgbNW = texture(uImage, uv + vec2(-1.0, -1.0) * uTexel).rgb;
    vec3 rgbNE = texture(uImage, uv + vec2(1.0, -1.0) * uTexel).rgb;
    vec3 rgbSW = texture(uImage, uv + vec2(-1.0, 1.0) * uTexel).rgb;
    vec3 rgbSE = texture(uImage, uv + vec2(1.0, 1.0) * uTexel).rgb;
    vec3 rgbM = texture(uImage, uv).rgb;
    float lumaNW = dot(rgbNW, lumaWeights);
    float lumaNE = dot(rgbNE, lumaWeights);
    float lumaSW = dot(rgbSW, lumaWeights);
    float lumaSE = dot(rgbSE, lumaWeights);
    float lumaM = dot(rgbM, lumaWeights);
    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));
    if (lumaMax - lumaMin < max(0.0312, lumaMax * 0.125)) {
        return rgbM; // not an edge
    }
    vec2 dir = vec2(-((lumaNW + lumaNE) - (lumaSW + lumaSE)), (lumaNW + lumaSW) - (lumaNE + lumaSE));
    float reduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * 0.03125, 1.0 / 128.0);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + reduce);
    dir = clamp(dir * rcpDirMin, vec2(-8.0), vec2(8.0)) * uTexel;
    vec3 rgbA = 0.5 * (texture(uImage, uv + dir * (1.0 / 3.0 - 0.5)).rgb + texture(uImage, uv + dir * (2.0 / 3.0 - 0.5)).rgb);
    vec3 rgbB = rgbA * 0.5 + 0.25 * (texture(uImage, uv - dir * 0.5).rgb + texture(uImage, uv + dir * 0.5).rgb);
    float lumaB = dot(rgbB, lumaWeights);
    return (lumaB < lumaMin || lumaB > lumaMax) ? rgbA : rgbB;
}

void main()
{
    fragColor = vec4(uFxaa == 1 ? fxaa(vUv) : texture(uImage, vUv).rgb, 1.0);
}
