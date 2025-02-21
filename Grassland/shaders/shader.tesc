#version 450
layout(vertices = 4) out;

layout(push_constant) uniform CameraData {
    layout(offset = 8) float minTessLevel;
    float maxTessLevel;
    float tessFactor;
    float tessSlope;
    vec3 cameraPos;
};

layout(location = 0) in vec2 texCoord[];  // Received from vertex shader
layout(location = 0) out vec2 tcOut[4];   // Send to tess eval shader

void main() {
    vec3 worldPos = gl_in[gl_InvocationID].gl_Position.xyz;
    float dist = length(cameraPos - worldPos);
    
    float tessLevel = mix(maxTessLevel, minTessLevel, pow(clamp(dist * tessFactor, 0.0, 1.0), tessSlope));

    if (gl_InvocationID == 0) {
        gl_TessLevelOuter[0] = tessLevel;
        gl_TessLevelOuter[1] = tessLevel;
        gl_TessLevelOuter[2] = tessLevel;
        gl_TessLevelOuter[3] = tessLevel;
        gl_TessLevelInner[0] = tessLevel;
        gl_TessLevelInner[1] = tessLevel;
    }

    gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;
    tcOut[gl_InvocationID] = texCoord[gl_InvocationID];
}