#version 330 core
layout(location = 0) in vec3 aPosition;
uniform mat4 uViewProj;
out vec3 vWorld;
void main()
{
    vWorld = aPosition;
    gl_Position = uViewProj * vec4(aPosition, 1.0);
}
