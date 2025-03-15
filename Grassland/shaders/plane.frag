#version 450

layout(push_constant) uniform PushConstant
{
    layout(offset = 112) vec3 color;
} pushConstant;

layout(location = 0) out vec4 fragColor;

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec3 inNormal;

void main()
{
    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));

    float intensity = max(dot(inNormal, lightDir), 0.0);
    vec3 finalColor = pushConstant.color * intensity;
    // Add some ambient light
    finalColor += pushConstant.color * 0.1;

    fragColor = vec4(finalColor, 1.0);
}