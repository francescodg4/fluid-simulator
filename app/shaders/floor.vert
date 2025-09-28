#version 330 core
layout(location = 0) in vec2 aCorner;
uniform mat4 uViewProj;
uniform vec3 uCenter;
uniform float uExtent;
out vec3 vWorld;
void main()
{
    vWorld = vec3(uCenter.x + aCorner.x * uExtent, 0.0, uCenter.z + aCorner.y * uExtent);
    gl_Position = uViewProj * vec4(vWorld, 1.0);
}
