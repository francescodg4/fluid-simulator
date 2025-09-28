#version 330 core
layout(location = 0) in vec4 aParticle; // xyz + normalised speed
uniform mat4 uViewProj;
uniform float uSize;
out float vScalar;
void main()
{
    gl_Position = uViewProj * vec4(aParticle.xyz, 1.0);
    gl_PointSize = uSize * clamp(12.0 / max(gl_Position.w, 0.1), 0.6, 2.5);
    vScalar = aParticle.w;
}
