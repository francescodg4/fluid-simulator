#version 330 core
layout(location = 0) in vec3 aCorner; // unit cube
uniform mat4 uViewProj;
uniform vec3 uBoxMin;
uniform vec3 uBoxMax;
out vec3 vWorld;
void main()
{
    vWorld = mix(uBoxMin, uBoxMax, aCorner);
    gl_Position = uViewProj * vec4(vWorld, 1.0);
}
