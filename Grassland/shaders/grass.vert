#version 450

layout(push_constant) uniform PushConstants {
    mat4 VPMatrix;
    float widthMult;
    float tilt;
    float bend;
    vec2 windDir;
    float windStrength;
} pc;

layout(binding = 0) uniform sampler2D windNoise;

// Per-instance attributes
layout(location = 0) in vec3 inInstPosition;    // Instance base position
layout(location = 1) in float inInstRotation;   // Rotation (in radians)
layout(location = 2) in vec2 uv;                // UV coordinates for noise
layout(location = 3) in float inInstanceHeight; // Height of the grass blade

// Per-vertex attributes
layout(location = 4) in vec2 vertexPosition;  // Vertex positions
layout(location = 5) in vec2 vertexNormal;    // Vertex normals


layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec3 fragPosition;
layout(location = 2) out float fragWeight;

mat3 getRotationMatrix(vec3 axis, float angle)
{
    axis = normalize(axis);
    float s = sin(angle);
    float c = cos(angle);
    float oc = 1.0 - c;

    return mat3(
        vec3(axis.x * axis.x * oc + c,          axis.y * axis.x * oc - axis.z * s, axis.z * axis.x * oc + axis.y * s),
        vec3(axis.x * axis.y * oc + axis.z * s, axis.y * axis.y * oc + c,          axis.z * axis.y * oc - axis.x * s),
        vec3(axis.x * axis.z * oc - axis.y * s, axis.y * axis.z * oc + axis.x * s, axis.z * axis.z * oc + c)
    );
}

void main() {
    vec2 finalVertPos = vertexPosition;
    finalVertPos.x *= pc.widthMult;
    fragWeight = -finalVertPos.y;

    float localTilt = 0.0;
    if (fragWeight > 0.0) 
        localTilt = mix(0.0, pc.tilt, pow(fragWeight, pc.bend));

    vec3 fragPos = vec3(finalVertPos, 0.0);
    finalVertPos.y *= inInstanceHeight;

    float windBendIntensity = mix(0.0, texture(windNoise, uv).r * pc.windStrength, fragWeight);
    vec3 windAxis = normalize(cross(vec3(0.0, 1.0, 0.0), vec3(-pc.windDir.x, 0.0, -pc.windDir.y)));
    
    mat3 rotation = getRotationMatrix(windAxis, windBendIntensity) 
                  * getRotationMatrix(vec3(0.0, 1.0, 0.0), inInstRotation) 
                  * getRotationMatrix(vec3(1.0, 0.0, 0.0), localTilt);

    vec3 windBendPos = rotation * vec3(finalVertPos, 0.0);

    fragPosition = inInstPosition + windBendPos;
    fragNormal = rotation * vec3(vertexNormal, 0.0);

    gl_Position = pc.VPMatrix * vec4(fragPosition, 1.0);
}
