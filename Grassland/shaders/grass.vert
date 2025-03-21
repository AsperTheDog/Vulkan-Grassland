#version 450

layout(push_constant) uniform PushConstants {
    mat4 VPMatrix;
    float widthMult;
    float tilt;
    float bend;
    vec2 windDir;
    float windStrength;
    float grassRoundness;
} pc;

layout(binding = 0) uniform sampler2D windNoise;

// Per-instance attributes
layout(location = 0) in vec3 inInstPosition;    // Instance base position
layout(location = 1) in float inInstRotation;   // Rotation (in radians)
layout(location = 2) in vec2 uv;                // UV coordinates for noise
layout(location = 3) in float inInstanceHeight; // Height of the grass blade

// Per-vertex attributes
layout(location = 4) in vec2 vertexPosition;  // Vertex positions


layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec3 fragPosition;
layout(location = 2) out float fragWeight;

mat3 getPositionRotationMatrix(vec3 axis, float angle)
{
    float s = sin(angle);
    float c = cos(angle);
    float oc = 1.0 - c;

    return mat3(
        vec3(axis.x * axis.x * oc + c,          axis.y * axis.x * oc - axis.z * s, axis.z * axis.x * oc + axis.y * s),
        vec3(axis.x * axis.y * oc + axis.z * s, axis.y * axis.y * oc + c,          axis.z * axis.y * oc - axis.x * s),
        vec3(axis.x * axis.z * oc - axis.y * s, axis.y * axis.z * oc + axis.x * s, axis.z * axis.z * oc + c)
    );
}

vec3 bezierQuadratic(vec3 p0, vec3 pm, vec3 p1, float t) {
    float u = 1.0 - t;
    return u * u * p0 + 2.0 * u * t * pm + t * t * p1;
}

vec3 bezierCubic(vec3 p0, vec3 p1, vec3 p2, vec3 p3, float t) {
    float u = 1.0 - t;
    float uu = u * u;
    float uuu = uu * u;
    float tt = t * t;
    float ttt = tt * t;

    return uuu * p0 + 3.0 * uu * t * p1 + 3.0 * u * tt * p2 + ttt * p3;
}

void main() {
    vec2 finalVertPos = vertexPosition;
    float xSign = -sign(finalVertPos.x);
    finalVertPos.x *= pc.widthMult;
    float weight = -finalVertPos.y;

    float localTilt = 0.0;
    if (weight > 0.0) 
        localTilt = mix(0.0, pc.tilt, pow(weight, pc.bend));

    vec3 fragPos = vec3(finalVertPos, 0.0);
    finalVertPos.y *= inInstanceHeight;

    float windBendIntensity = mix(0.0, texture(windNoise, uv).r * pc.windStrength, weight);
    vec3 windAxis = normalize(cross(vec3(0.0, 1.0, 0.0), vec3(-pc.windDir.x, 0.0, -pc.windDir.y)));
    
    mat3 rotation = getPositionRotationMatrix(windAxis, windBendIntensity) 
                  * getPositionRotationMatrix(vec3(0.0, 1.0, 0.0), inInstRotation) 
                  * getPositionRotationMatrix(vec3(-1.0, 0.0, 0.0), localTilt);

    vec3 windBendPos = rotation * vec3(finalVertPos, 0.0);

    vec3 normal = vec3(pc.grassRoundness * xSign, 0.0, 1.0);

    mat3 normalRotation = getPositionRotationMatrix(windAxis, 2 * windBendIntensity) 
                  * getPositionRotationMatrix(vec3(0.0, 1.0, 0.0), inInstRotation) 
                  * getPositionRotationMatrix(vec3(-1.0, 0.0, 0.0), 2 * localTilt);
    
    fragPosition = inInstPosition + windBendPos;
    fragNormal = normalize(normalRotation * normalize(normal));
    fragWeight = weight;

    gl_Position = pc.VPMatrix * vec4(fragPosition, 1.0);
}
