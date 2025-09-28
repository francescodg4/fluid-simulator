#version 330 core
// Direct volume rendering: front-to-back emission/absorption ray casting through the 3D field
// texture, classified by the transfer function (colour + opacity) with early ray termination.
in vec3 vWorld;
out vec4 fragColor;
uniform vec3 uCameraPos;
uniform vec3 uViewDir;
uniform float uOrtho;
uniform vec3 uBoxMin;
uniform vec3 uBoxMax;
uniform sampler3D uField;
uniform sampler2D uTransfer;
uniform int uChannel;
uniform vec2 uRange;
uniform float uDensity;
uniform float uStep;

float hash(vec2 p)
{
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

void main()
{
    vec3 dir = uOrtho > 0.5 ? uViewDir : normalize(vWorld - uCameraPos);
    vec3 origin = uOrtho > 0.5 ? vWorld - dir * 1000.0 : uCameraPos;
    vec3 inv = 1.0 / dir;
    vec3 t0 = (uBoxMin - origin) * inv;
    vec3 t1 = (uBoxMax - origin) * inv;
    vec3 tmin = min(t0, t1);
    vec3 tmax = max(t0, t1);
    float enter = max(max(max(tmin.x, tmin.y), tmin.z), 0.0);
    float leave = min(min(tmax.x, tmax.y), tmax.z);
    if (leave <= enter) {
        discard;
    }

    vec3 extent = uBoxMax - uBoxMin;
    vec4 acc = vec4(0.0);
    float t = enter + uStep * hash(gl_FragCoord.xy); // jitter against banding
    for (int i = 0; i < 768 && t < leave; ++i, t += uStep) {
        vec4 s = texture(uField, (origin + dir * t - uBoxMin) / extent);
        if (s.a > 0.5) {
            break; // hit the vehicle
        }
        float x = clamp((s[uChannel] - uRange.x) / (uRange.y - uRange.x), 0.0, 1.0);
        vec4 c = texture(uTransfer, vec2(x, 0.5));
        float alpha = 1.0 - exp(-c.a * uDensity * uStep);
        acc.rgb += (1.0 - acc.a) * alpha * c.rgb;
        acc.a += (1.0 - acc.a) * alpha;
        if (acc.a > 0.97) {
            break;
        }
    }
    fragColor = acc; // premultiplied alpha
}
