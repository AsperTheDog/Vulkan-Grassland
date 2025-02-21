#version 450

layout(location = 0) out vec4 fragColor;

layout(location = 0) in vec2 uv;

void main()
{
    fragColor = vec4(uv, 1.0, 1.0);
}