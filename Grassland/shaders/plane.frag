#version 450

layout(push_constant) uniform PushConstant
{
    layout(offset = 128) vec3 color;
    vec3 lightDir;
} pushConstant;

layout(location = 0) out vec4 fragColor;

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec3 inNormal;

void main()
{
    float intensity = max(dot(inNormal, pushConstant.lightDir), 0.0);
    vec3 finalColor = pushConstant.color * intensity;
    // Add some ambient light
    finalColor += pushConstant.color * 0.1;

    fragColor = vec4(finalColor, 1.0);
}