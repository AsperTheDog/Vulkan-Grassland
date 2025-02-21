#version 450
layout(quads, equal_spacing, cw) in;

layout(push_constant) uniform PushConstants {
    layout(offset = 44) float heightScale;
    mat4 mvpMatrix;
} pushConstants;

layout(binding = 0) uniform sampler2D heightmap;

layout(location = 0) in vec2 inUV[];  // Received from tess control shader

layout(location = 0 ) out vec2 uv;   // Send to fragment shader

void main() {
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

    // Apply MVP transformation
    gl_Position = pushConstants.mvpMatrix * vec4(worldPos, 1.0);
}