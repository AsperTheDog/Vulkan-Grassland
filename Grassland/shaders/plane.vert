#version 450

layout(location = 0) out vec2 uv;  // Pass UV coordinates to tessellation

layout(push_constant) uniform PushConstants {
    uint gridSize;    // Number of patches in one row/column
    float patchSize;  // World space size of one patch
    vec2 cameraTile;  // Tile coords where the camera is
} pushConstants;

void main() {
    int quadID = gl_VertexIndex / 4; // Each quad has 4 vertices
    int vertID = gl_VertexIndex % 4; // Which vertex in the quad

    // Hardcoded quad positions
    const vec2 positions[4] = vec2[4](
        vec2(0.0, 0.0), vec2(1.0, 0.0),
        vec2(0.0, 1.0), vec2(1.0, 1.0)
    );

    float halfPoint = (pushConstants.gridSize / 2) * pushConstants.patchSize;

    uint x = quadID % pushConstants.gridSize;
    uint y = quadID / pushConstants.gridSize;

    vec2 tileCoords = vec2(x, y) * pushConstants.patchSize;
    vec2 vertexOffset = positions[vertID] * pushConstants.patchSize;
    vec2 worldOffset = pushConstants.cameraTile - vec2(halfPoint);
    float worldExtent = pushConstants.gridSize * pushConstants.patchSize;

    vec2 localPos = vertexOffset + tileCoords;
    vec2 worldPos = localPos + worldOffset;
    gl_Position = vec4(worldPos.x, 0.0, worldPos.y, 1.0);
    uv = localPos / worldExtent;
}