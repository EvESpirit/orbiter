#version 450

// ==============================================================
// Panel2D.vert
// 2D instrument panel, glass cockpit MFD & HUD mesh vertex shader
// Part of the ORBITER VISUALISATION PROJECT (OVP) - VulkanClient
// Licensed under the MIT License
// ==============================================================

layout(push_constant) uniform Panel2DPushConstants {
	vec4  scaleOffset;
	float alpha;
} pc;

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inUV;

layout(location = 0) out vec2 vUV;

void main()
{
	vUV = inUV;
	gl_Position = vec4(
		inPos.x * pc.scaleOffset.x + pc.scaleOffset.y,
		inPos.y * pc.scaleOffset.z + pc.scaleOffset.w,
		0.0,
		1.0);
}
