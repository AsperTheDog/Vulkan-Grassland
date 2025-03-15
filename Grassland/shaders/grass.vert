#version 450

layout(push_constant) uniform PushConstants {
    mat4 VPMatrix;
} pc;

// Per-instance attributes
layout(location = 0) in vec3 inInstPosition;  // Instance base position
layout(location = 1) in float inInstRotation; // Rotation (in radians)

layout(location = 2) in vec2 vertexPosition;  // Vertex positions
layout(location = 3) in vec2 vertexNormal;    // Vertex normals
layout(location = 4) in vec3 vertexColor;     // Vertex colors

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec3 fragPosition;
layout(location = 2) out vec3 fragColor;

void main() {
    const float height = 2.0;

    // Rotation around world up (0, -1, 0)
    mat3 rotation = mat3(
        vec3(cos(inInstRotation), 0.0, sin(inInstRotation)),
        vec3(0.0, 1.0, 0.0),
        vec3(-sin(inInstRotation), 0.0, cos(inInstRotation))
    );

    vec2 finalVertPos = vertexPosition;
    finalVertPos.y *= height;

    fragNormal = rotation * vec3(vertexNormal, 1.0);
    fragPosition = inInstPosition + rotation * vec3(finalVertPos, 0.0);
    fragColor = vertexColor;

    gl_Position = pc.VPMatrix * vec4(fragPosition, 1.0);
}
