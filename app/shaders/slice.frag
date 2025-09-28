#version 330 core
in vec3 vWorld;
out vec4 fragColor;
uniform sampler3D uField;
uniform sampler2D uTransfer;
uniform int uChannel;
uniform vec2 uRange;
uniform vec3 uGridOrigin;
uniform vec3 uGridExtent;
uniform float uOpacity;
void main()
{
    vec4 s = texture(uField, (vWorld - uGridOrigin) / uGridExtent);
    if (s.a > 0.5) {
        discard; // inside the vehicle
    }
    float t = clamp((s[uChannel] - uRange.x) / (uRange.y - uRange.x), 0.0, 1.0);
    vec3 color = texture(uTransfer, vec2(t, 0.5)).rgb;
    // Iso-contours every 1/12 of the range.
    float bands = t * 12.0;
    float contour = 1.0 - smoothstep(0.0, 1.5 * fwidth(bands), abs(fract(bands + 0.5) - 0.5));
    color = mix(color, color * 0.45, contour * 0.6);
    fragColor = vec4(color, uOpacity);
}
