#version 330 core
in float vScalar;
in float vArc;
in float vSide;
out vec4 fragColor;
uniform sampler2D uTransfer;
uniform vec2 uRange;
uniform float uTime;
uniform float uAnimate;
uniform float uOpacity;
void main()
{
    float t = clamp((vScalar - uRange.x) / (uRange.y - uRange.x), 0.0, 1.0);
    vec3 color = texture(uTransfer, vec2(t, 0.5)).rgb;
    float edge = 1.0 - smoothstep(0.55, 1.0, abs(vSide));
    // Travelling pulses along the line: speed is proportional to the local flow speed.
    float phase = fract(vArc * 0.45 - uTime * (0.35 + 0.9 * vScalar));
    float pulse = mix(1.0, 0.35 + 1.1 * pow(phase, 5.0), uAnimate);
    fragColor = vec4(color * (0.75 + 0.45 * pulse), uOpacity * edge * clamp(pulse, 0.25, 1.0));
}
