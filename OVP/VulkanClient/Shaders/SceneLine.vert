#version 450

// ==============================================================
// SceneLine.vert
// 3D celestial line / grid / constellation vertex shader
// Part of the ORBITER VISUALISATION PROJECT (OVP) - VulkanClient
// Licensed under the MIT License
// ==============================================================

layout(push_constant) uniform SceneLinePushConstants {
	mat4 mWVP;
	vec4 vColor;
	vec4 vParams; // x = homogeneous w (0.0 for infinity, 1.0 for world-space point)
} pc;

layout(location = 0) in vec3 inPos;

void main()
{
	gl_Position = pc.mWVP * vec4(inPos, pc.vParams.x);
}
