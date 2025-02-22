#version 450
layout(quads, equal_spacing, cw) in;

layout(push_constant) uniform PushConstants {
    layout(offset = 44) float heightScale;
    mat4 mvpMatrix;
    float uvOffsetScale;
} pushConstants;

layout(binding = 0) uniform sampler2D heightmap;

layout(location = 0) in vec2 inUV[];
layout(location = 0) out vec2 uv;
layout(location = 1) out vec3 normal;

void main() {
    // Compute interpolated UVs
    vec2 uv1 = mix(inUV[0], inUV[1], gl_TessCoord.x);
    vec2 uv2 = mix(inUV[2], inUV[3], gl_TessCoord.x);
    uv = mix(uv1, uv2, gl_TessCoord.y);

    // Interpolate world-space position from input control points
    vec3 worldPos = mix(
        mix(gl_in[0].gl_Position.xyz, gl_in[1].gl_Position.xyz, gl_TessCoord.x),
        mix(gl_in[2].gl_Position.xyz, gl_in[3].gl_Position.xyz, gl_TessCoord.x),
        gl_TessCoord.y
    );

    // Apply heightmap displacement
    float height = texture(heightmap, uv).r * pushConstants.heightScale;
    worldPos.y -= height; // Adjust Y based on heightmap

    // === Compute Normal from Heightmap with Grid Scaling ===

    // Sample height differences in texture space
    float heightL = texture(heightmap, uv + vec2(-pushConstants.uvOffsetScale, 0)).r * pushConstants.heightScale;
    float heightR = texture(heightmap, uv + vec2(pushConstants.uvOffsetScale, 0)).r * pushConstants.heightScale;
    float heightD = texture(heightmap, uv + vec2(0, -pushConstants.uvOffsetScale)).r * pushConstants.heightScale;
    float heightU = texture(heightmap, uv + vec2(0, pushConstants.uvOffsetScale)).r * pushConstants.heightScale;

    // Compute tangent vectors in world space
    vec3 tangentX = vec3(2.0, heightR - heightL, 0.0);
    vec3 tangentZ = vec3(0.0, heightU - heightD, 2.0);

    // Compute normal as cross product of tangent vectors
    normal = normalize(cross(tangentZ, tangentX));

    // Apply MVP transformation
    gl_Position = pushConstants.mvpMatrix * vec4(worldPos, 1.0);
}
