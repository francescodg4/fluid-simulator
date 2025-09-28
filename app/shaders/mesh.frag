#version 330 core
in vec3 vWorld;
in vec3 vNormal;
out vec4 fragColor;

uniform vec3 uCameraPos;
uniform vec3 uViewDir;
uniform float uOrtho;
uniform vec3 uBaseColor; // linear
uniform float uMetallic;
uniform float uRoughness;
uniform float uEmissive;
uniform int uShading; // 0 studio materials, 1 clay, 2 scalar field
uniform float uHighlight;

uniform sampler3D uField;
uniform sampler2D uTransfer;
uniform int uHasField;
uniform int uChannel;
uniform vec2 uRange;
uniform vec3 uGridOrigin;
uniform vec3 uGridExtent;

vec3 toLinear(vec3 c) { return pow(c, vec3(2.2)); }

// Procedural studio environment: blue gradient dome, two soft boxes and a dark floor.
vec3 environment(vec3 d)
{
    vec3 sky = mix(vec3(0.02, 0.03, 0.06), vec3(0.16, 0.22, 0.36), smoothstep(-0.05, 0.9, d.y));
    float softbox = smoothstep(0.10, 0.0, abs(d.y - 0.55)) * (0.55 + 0.45 * d.x);
    float strip = smoothstep(0.06, 0.0, abs(0.75 * d.z + d.y - 0.95));
    vec3 color = sky + vec3(0.95, 0.97, 1.0) * (1.8 * softbox + 0.8 * strip);
    return mix(vec3(0.012, 0.016, 0.028), color, smoothstep(-0.2, 0.02, d.y));
}

void main()
{
    vec3 N = normalize(vNormal);
    vec3 V = uOrtho > 0.5 ? -uViewDir : normalize(uCameraPos - vWorld);
    if (dot(N, V) < 0.0) {
        N = -N; // exported meshes are not consistently oriented: shade both sides
    }

    vec3 base = uBaseColor;
    float metal = uMetallic;
    float rough = uRoughness;
    float emissive = uEmissive;
    if (uShading == 1) {
        base = vec3(0.30, 0.36, 0.46);
        metal = 0.0;
        rough = 0.6;
        emissive = 0.0;
    } else if (uShading == 2 && uHasField == 1) {
        vec3 tc = (vWorld - uGridOrigin) / uGridExtent;
        float v = texture(uField, tc)[uChannel];
        float t = clamp((v - uRange.x) / (uRange.y - uRange.x), 0.0, 1.0);
        base = toLinear(texture(uTransfer, vec2(t, 0.5)).rgb);
        metal = 0.0;
        rough = 0.5;
        emissive = 0.12;
    }

    vec3 L1 = normalize(vec3(-0.35, 0.85, 0.4));
    vec3 L2 = normalize(vec3(0.7, 0.35, -0.6));
    float d1 = max(dot(N, L1), 0.0);
    float d2 = max(dot(N, L2), 0.0);
    vec3 hemi = mix(vec3(0.015, 0.02, 0.035), vec3(0.16, 0.2, 0.3), N.y * 0.5 + 0.5);
    vec3 diffuse = base * (hemi + vec3(1.0, 0.97, 0.93) * d1 * 0.9 + vec3(0.45, 0.58, 0.9) * d2 * 0.35) * (1.0 - 0.85 * metal);

    float shininess = mix(400.0, 6.0, rough);
    vec3 H = normalize(L1 + V);
    float highlight = pow(max(dot(N, H), 0.0), shininess) * (shininess + 8.0) / 30.0;
    vec3 F0 = mix(vec3(0.04), base, metal);
    float fresnel = pow(1.0 - max(dot(N, V), 0.0), 5.0);
    vec3 F = F0 + (vec3(1.0) - F0) * fresnel;
    vec3 reflection = environment(reflect(-V, N)) * mix(1.0, 0.15, rough);
    vec3 color = diffuse + F * (reflection + highlight) + base * emissive;

    color += vec3(1.0, 0.55, 0.12) * 0.25 * uHighlight;
    color = color / (1.0 + 0.3 * color); // gentle tone mapping
    fragColor = vec4(pow(color, vec3(1.0 / 2.2)), 1.0);
}
