#version 450

// ==============================================================
// CelSphere.frag
// Tiled celestial background & star-image sphere fragment shader
// Part of the ORBITER VISUALISATION PROJECT (OVP) - VulkanClient
// Licensed under the MIT License
// ==============================================================

layout(set = 0, binding = 0) uniform sampler2D uTex;

layout(push_constant) uniform CelSpherePushConstants {
	mat4 mWVP;
	vec4 vColor;
} pc;

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

void main()
{
	vec4 c = texture(uTex, vUV);
	outColor = vec4(c.rgb * pc.vColor.rgb, pc.vColor.a);
}
