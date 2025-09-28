#version 330 core
in float vScalar;
out vec4 fragColor;
uniform sampler2D uTransfer;
uniform vec2 uRange;
void main()
{
    vec2 d = gl_PointCoord * 2.0 - 1.0;
    float r2 = dot(d, d);
    if (r2 > 1.0) {
        discard;
    }
    float t = clamp((vScalar - uRange.x) / (uRange.y - uRange.x), 0.0, 1.0);
    vec3 color = texture(uTransfer, vec2(t, 0.5)).rgb;
    fragColor = vec4(color * (1.1 - 0.3 * r2), (1.0 - r2) * 0.85);
}
