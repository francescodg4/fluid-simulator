#version 330 core
in vec3 vWorld;
out vec4 fragColor;
uniform vec3 uCameraPos;
uniform vec4 uFootprint; // vehicle xz min / max
uniform vec4 uTunnel;    // tunnel xz min / max
uniform float uFadeDistance;

float gridLine(vec2 p, float spacing)
{
    vec2 q = p / spacing;
    vec2 g = abs(fract(q - 0.5) - 0.5) / max(fwidth(q), vec2(1e-5));
    return 1.0 - min(min(g.x, g.y), 1.0);
}

float roundedBox(vec2 p, vec2 center, vec2 halfSize, float radius)
{
    vec2 q = abs(p - center) - halfSize + radius;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
}

void main()
{
    vec2 p = vWorld.xz;
    float fade = exp(-pow(length(p - uCameraPos.xz) / uFadeDistance, 2.0));

    vec3 color = vec3(0.055, 0.07, 0.11);
    bool inTunnel = p.x > uTunnel.x && p.x < uTunnel.z && p.y > uTunnel.y && p.y < uTunnel.w;
    if (inTunnel) {
        color = vec3(0.07, 0.095, 0.15);
    }
    float minor = gridLine(p, 0.5) * 0.35;
    float major = gridLine(p, 2.5) * 0.8;
    color = mix(color, vec3(0.2, 0.28, 0.42), max(minor, major));

    // World axes as in Blender: X red, Z (depth) blue.
    float ax = 1.0 - min(abs(p.y) / max(fwidth(p.y), 1e-5) / 1.5, 1.0);
    float az = 1.0 - min(abs(p.x) / max(fwidth(p.x), 1e-5) / 1.5, 1.0);
    color = mix(color, vec3(0.75, 0.22, 0.28), ax * 0.8);
    color = mix(color, vec3(0.22, 0.45, 0.85), az * 0.8);

    // Soft contact shadow under the vehicle.
    vec2 c = 0.5 * (uFootprint.xy + uFootprint.zw);
    vec2 h = 0.5 * (uFootprint.zw - uFootprint.xy);
    float d = roundedBox(p, c, h * vec2(0.95, 0.9), 0.4);
    color *= 1.0 - 0.8 * (1.0 - smoothstep(-0.3, 0.7, d));

    fragColor = vec4(color, fade);
}
