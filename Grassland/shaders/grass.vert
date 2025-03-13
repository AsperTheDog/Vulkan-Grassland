#version 450

layout(push_constant) uniform PushConstants {
    mat4 VPMatrix;
} pc;

// Per-instance attributes
layout(location = 0) in vec3 inPosition;  // Instance base position
layout(location = 1) in float inRotation; // Rotation (in radians)

const vec3 vertexPositions[3] = vec3[](
    vec3( 0.0, -2.0,  0.0),  // Base
    vec3(-0.1, 0.0,  0.0),  // Left tip
    vec3( 0.1, 0.0,  0.0)   // Right tip
);

const vec3 vertexNormals[3] = vec3[](
    vec3( 0.0,  0.0,  1.0),  // Base normal
    vec3(-0.2,  0.8,  1.0),  // Left tip normal
    vec3( 0.2,  0.8,  1.0)   // Right tip normal
);

const vec3 vertexColors[3] = vec3[](
    vec3(0.0, 0.6, 0.0),  // Base color
    vec3(0.0, 0.1, 0.0),  // Left tip color
    vec3(0.0, 0.1, 0.0)   // Right tip color
);

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec3 fragPosition;
layout(location = 2) out vec3 fragColor;

void main() {
    // Rotation around world up (0, -1, 0)
    mat3 rotation = mat3(
        vec3(cos(inRotation), 0.0, sin(inRotation)),
        vec3(0.0, 1.0, 0.0),
        vec3(-sin(inRotation), 0.0, cos(inRotation))
    );

    // Transform the vertex position and normal
    vec3 position = inPosition + rotation * vertexPositions[gl_VertexIndex];
    vec3 normal = rotation * vertexNormals[gl_VertexIndex];

    // Output the normal and position in world space
    fragNormal = normal;
    fragPosition = position;
    fragColor = vertexColors[gl_VertexIndex];

    gl_Position = pc.VPMatrix * vec4(position, 1.0);
}
