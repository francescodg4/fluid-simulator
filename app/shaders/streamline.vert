#version 330 core
// Screen-space ribbons: each polyline point is emitted twice (side = -1 / +1) and extruded
// perpendicular to the projected tangent, giving constant pixel width lines on core profiles.
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aPrev;
layout(location = 2) in vec3 aNext;
layout(location = 3) in vec3 aData; // side, scalar, arc length
uniform mat4 uViewProj;
uniform vec2 uViewport;
uniform float uWidth;
out float vScalar;
out float vArc;
out float vSide;

vec2 toScreen(vec4 clip)
{
    return clip.xy / max(clip.w, 1e-4) * 0.5 * uViewport;
}

void main()
{
    vec4 clip = uViewProj * vec4(aPosition, 1.0);
    vec2 prev = toScreen(uViewProj * vec4(aPrev, 1.0));
    vec2 next = toScreen(uViewProj * vec4(aNext, 1.0));
    vec2 tangent = next - prev;
    tangent = length(tangent) > 1e-4 ? normalize(tangent) : vec2(1.0, 0.0);
    vec2 normal = vec2(-tangent.y, tangent.x);
    clip.xy += normal * aData.x * uWidth / uViewport * clip.w;
    gl_Position = clip;
    vSide = aData.x;
    vScalar = aData.y;
    vArc = aData.z;
}
