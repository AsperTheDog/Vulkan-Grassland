#version 450

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    mat4 invVPMatrix;
    vec3 lightDir;
    vec3 skyTopColor;
    vec3 skyHorizonColor;
    float horizonFalloff;
    vec3 fogColor;
    float fogStrength;
    vec3 sunColor;
    float sunSize;
    float sunIntensity;
    float sunGlowFalloff;
    float sunGlowIntensity;
    float exposure;
} pc;

vec3 homogenize(vec4 p)
{
	return vec3(p * (1.0 / p.w));
}

void main() {
	vec4 viewCoord = vec4(inUV.x * 2.0 - 1.0, inUV.y * 2.0 - 1.0, 1.0, 1.0);
	vec3 p = homogenize(pc.invVPMatrix * viewCoord);
    vec3 worldDir = normalize(p);

    float t = clamp(pow(clamp(-worldDir.y, 0.0, 1.0), pc.horizonFalloff), 0.0, 1.0);
    vec3 skyColor = mix(pc.skyHorizonColor, pc.skyTopColor, t);

    vec3 sunDir = normalize(-pc.lightDir);
    sunDir.y *= -1.0;
    float cosAngle = clamp(dot(worldDir, sunDir), 0.0, 1.0);
    float sunDisc = smoothstep(cos(pc.sunSize), cos(pc.sunSize * 0.8), cosAngle);
    float sunGlow = pow(cosAngle, pc.sunGlowFalloff);
    float sunHeight = clamp(sunDir.y, 0.0, 1.0);
    vec3 sunTint = mix(vec3(1.0, 0.6, 0.3), vec3(1.0), sunHeight);
    skyColor += sunTint * pc.sunIntensity * sunDisc;
    skyColor += sunTint * pc.sunGlowIntensity * sunGlow;


    skyColor = mix(skyColor, pc.fogColor, pc.fogStrength);

    vec3 mapped = vec3(1.0) - exp(-skyColor * pc.exposure);

    outColor = vec4(mapped, 1.0);
}
