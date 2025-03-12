#version 450
layout(vertices = 4) out;

layout(push_constant) uniform CameraData {
    layout(offset = 16) vec3 cameraPos;
    float minTessLevel;
    float maxTessLevel;
    float tessFactor;
    float tessSlope;
};

layout(location = 0) in vec2 texCoord[];
layout(location = 0) out vec2 tcOut[4];

// A helper function to compute tessellation factor based on a world-space position
float computeTessFactor(vec3 pos) {
    float dist = length(cameraPos - pos);
    return mix(maxTessLevel, minTessLevel, pow(clamp(dist * tessFactor, 0.0, 1.0), tessSlope));
}

void main() {
    // Get the world-space positions for the control points of this patch
    vec3 p0 = gl_in[0].gl_Position.xyz;
    vec3 p1 = gl_in[1].gl_Position.xyz;
    vec3 p2 = gl_in[2].gl_Position.xyz;
    vec3 p3 = gl_in[3].gl_Position.xyz;

    // Compute midpoints along each edge
    vec3 m0 = (p2 + p0) * 0.5;
    vec3 m1 = (p0 + p1) * 0.5;
    vec3 m2 = (p1 + p3) * 0.5;
    vec3 m3 = (p3 + p2) * 0.5;

    // Compute tessellation factors for each edge based on the midpoints.
    float tess0 = computeTessFactor(m0);
    float tess1 = computeTessFactor(m1);
    float tess2 = computeTessFactor(m2);
    float tess3 = computeTessFactor(m3);

    // Set the outer tessellation factors for the patch.
    if (gl_InvocationID == 0) {
        gl_TessLevelOuter[0] = tess0;
        gl_TessLevelOuter[1] = tess1;
        gl_TessLevelOuter[2] = tess2;
        gl_TessLevelOuter[3] = tess3;

        // For the inner tessellation factors, average of opposite edges.
        gl_TessLevelInner[0] = (tess0 + tess3) * 0.5;
        gl_TessLevelInner[1] = (tess2 + tess1) * 0.5;
    }

    gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;
    tcOut[gl_InvocationID] = texCoord[gl_InvocationID];
}
