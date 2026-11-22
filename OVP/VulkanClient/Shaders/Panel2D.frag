#version 450

// ==============================================================
// Panel2D.frag
// 2D instrument panel, glass cockpit MFD & HUD mesh fragment shader
// Part of the ORBITER VISUALISATION PROJECT (OVP) - VulkanClient
// Licensed under the MIT License
// ==============================================================

layout(set = 0, binding = 0) uniform sampler2D uTex;

layout(push_constant) uniform Panel2DPushConstants {
	vec4  scaleOffset;
	float alpha;
} pc;

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

void main()
{
	vec4 c = texture(uTex, vUV);
	outColor = vec4(c.rgb, c.a * pc.alpha);
}
