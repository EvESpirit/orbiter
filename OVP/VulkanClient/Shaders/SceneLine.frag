#version 450

// ==============================================================
// SceneLine.frag
// 3D celestial line / grid / constellation fragment shader
// Part of the ORBITER VISUALISATION PROJECT (OVP) - VulkanClient
// Licensed under the MIT License
// ==============================================================

layout(push_constant) uniform SceneLinePushConstants {
	mat4 mWVP;
	vec4 vColor;
	vec4 vParams;
} pc;

layout(location = 0) out vec4 outColor;

void main()
{
	outColor = pc.vColor;
}
