#version 450

layout(push_constant) uniform PushConstants {
    layout(offset = 96) vec3 baseColor;
    vec3 tipColor;
    float colorRamp;
    vec3 cameraPos;
    vec3 lightDir;
} pc;

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec3 inPosition;
layout(location = 2) in float inWeight;

layout(location = 0) out vec4 outColor;

void main()
{
    vec3 normal = normalize(inNormal);

    vec3 color = mix(pc.baseColor, pc.tipColor, pow(inWeight, pc.colorRamp));

    // Specular lighting
    vec3 viewDir = normalize(pc.cameraPos - inPosition);
    vec3 reflectDir = reflect(-pc.lightDir, normal);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32);
    vec3 specular = vec3(0.5) * spec;

    outColor = vec4(color /*+ specular*/, 1.0);
}