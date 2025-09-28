#version 330 core
in vec2 vUv;
out vec4 fragColor;
uniform vec2 uViewport;
void main()
{
    vec3 top = vec3(0.105, 0.135, 0.205);
    vec3 bottom = vec3(0.035, 0.045, 0.075);
    vec3 color = mix(bottom, top, smoothstep(0.0, 1.0, vUv.y));
    // Soft glow behind the subject and a vignette towards the corners.
    vec2 p = (vUv - vec2(0.5, 0.55)) * vec2(uViewport.x / uViewport.y, 1.0);
    color += vec3(0.05, 0.08, 0.14) * exp(-dot(p, p) * 3.0);
    color *= 1.0 - 0.35 * smoothstep(0.4, 1.2, length(vUv - 0.5) * 1.4);
    fragColor = vec4(color, 1.0);
}
