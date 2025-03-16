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

//mat3 rotationMatrixXZ(vec2 rotationVec, float angle) {
//    vec3 axis = vec3(rotationVec.x, 0.0, rotationVec.y);
//    float s = sin(angle);
//    float c = cos(angle);
//    float oc = 1.0 - c;
//
//    return mat3(
//        oc * axis.x * axis.x + c,          oc * axis.x * axis.y,            oc * axis.x * axis.z + axis.y * s,
//        oc * axis.x * axis.y,              c,                               oc * axis.y * axis.z,
//        oc * axis.x * axis.z - axis.y * s, oc * axis.y * axis.z,            oc * axis.z * axis.z + c
//    );
//}

mat3 getTotationMatrixY(float angle) {
    float s = sin(angle);
    float c = cos(angle);

    return mat3(
        c,   0.0,  s,
        0.0, 1.0,  0.0,
       -s,   0.0,  c
    );
}

mat3 rotationMatrixX(float angle) {
    float s = sin(angle);
    float c = cos(angle);

    return mat3(
        1.0, 0.0,  0.0,
        0.0,  c,    s,
        0.0, -s,    c
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
    
    mat3 rotation = getTotationMatrixY(inInstRotation);
    mat3 tiltRotation = rotationMatrixX(localTilt);

    vec3 basePos = rotation * tiltRotation * vec3(finalVertPos, 0.0);

    float windNoiseValue = texture(windNoise, uv).r;
    vec3 windOffset = vec3(-pc.windDir.x, 0.0, -pc.windDir.y) * windNoiseValue * pc.windStrength * fragWeight;
    
    fragPosition = inInstPosition + basePos + windOffset;
    fragNormal = rotation * vec3(vertexNormal, 0.0);

    gl_Position = pc.VPMatrix * vec4(fragPosition, 1.0);
}
