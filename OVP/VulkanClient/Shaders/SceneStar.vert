#version 450

// ==============================================================
// SceneStar.vert
// Celestial sphere Hipparcos/Tycho star-dot vertex shader
// Part of the ORBITER VISUALISATION PROJECT (OVP) - VulkanClient
// Licensed under the MIT License
// ==============================================================

layout(push_constant) uniform SceneStarPushConstants {
	mat4 mWVP;
	vec4 vSkyColor; // rgb = background sky subtraction, w = intensity scale
} pc;

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec3 vColor;

void main()
{
	gl_Position = pc.mWVP * vec4(inPos, 0.0);
	gl_PointSize = 1.0;
	vColor = max(inColor.rgb * pc.vSkyColor.w - pc.vSkyColor.rgb, vec3(0.0));
}
