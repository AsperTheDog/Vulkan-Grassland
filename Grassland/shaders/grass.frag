#version 450

layout(push_constant) uniform PushConstants {
    layout(offset = 96) vec3 baseColor;
    vec3 tipColor;
    float colorRamp;
} pc;

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec3 inPosition;
layout(location = 2) in float inWeight;

layout(location = 0) out vec4 outColor;

void main()
{
    vec3 normal = normalize(inNormal);
    vec3 color = mix(pc.baseColor, pc.tipColor, pow(inWeight, pc.colorRamp));
    outColor = vec4(color, 1.0);
}