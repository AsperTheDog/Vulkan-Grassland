#version 450

layout(input_attachment_index = 0, set = 0, binding = 0) uniform subpassInput sceneColorInput;
layout(input_attachment_index = 1, set = 0, binding = 1) uniform subpassInput depthInput;

layout(push_constant) uniform FogPushConstants {
    vec3 fogColor;
    float fogDensity;
    float nearPlane;
    float farPlane;
} pc;

layout(location = 0) in vec2 uv;

layout(location = 0) out vec4 outColor;

float linearizeDepth(float depth)
{
    float z = depth * 2.0 - 1.0;
    return (2.0 * pc.nearPlane * pc.farPlane) / (pc.farPlane + pc.nearPlane - z * (pc.farPlane - pc.nearPlane));
}

vec3 homogenize(vec4 p)
{
	return vec3(p * (1.0 / p.w));
}

void main()
{
    float rawDepth = subpassLoad(depthInput).r;

    if (rawDepth >= 0.9999) 
    {
        outColor = subpassLoad(sceneColorInput);
        return;
    }

    float linearDepth = linearizeDepth(rawDepth);

    float fogFactor = 1.0 - exp(-pc.fogDensity * linearDepth);

    vec3 sceneColor = subpassLoad(sceneColorInput).rgb;
    vec3 finalColor = mix(sceneColor, pc.fogColor, fogFactor);

    outColor = vec4(finalColor, 1.0);
}
