#version 450

layout(location = 0) out vec2 uv;  // Pass UV coordinates to tessellation

layout(push_constant) uniform PushConstants {
    uint gridSize;   // Number of patches in one row/column
    float patchSize; // World space size of one patch
} pushConstants;

void main() {
    int quadID = gl_VertexIndex / 4; // Each quad has 4 vertices
    int vertID = gl_VertexIndex % 4; // Which vertex in the quad

    // Hardcoded quad positions
    const vec2 positions[4] = vec2[4](
        vec2(0.0, 0.0), vec2(1.0, 0.0),
        vec2(0.0, 1.0), vec2(1.0, 1.0)
    );

    uint x = quadID % pushConstants.gridSize;
    uint y = quadID / pushConstants.gridSize;

    vec2 worldOffset = vec2(x, y) * pushConstants.patchSize;
    float worldExtent = pushConstants.gridSize * pushConstants.patchSize;

    vec2 position = positions[vertID] * pushConstants.patchSize + worldOffset;
    gl_Position = vec4(position.x, 0.0, position.y, 1.0);
    uv = position / worldExtent;
}