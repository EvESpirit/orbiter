#version 450

// ==============================================================
// CelSphere.vert
// Tiled celestial background & star-image sphere vertex shader
// Part of the ORBITER VISUALISATION PROJECT (OVP) - VulkanClient
// Licensed under the MIT License
// ==============================================================

layout(push_constant) uniform CelSpherePushConstants {
	mat4 mWVP;
	vec4 vColor;
} pc;

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec2 inUV;

layout(location = 0) out vec2 vUV;

void main()
{
	gl_Position = pc.mWVP * vec4(inPos, 0.0);
	vUV = inUV;
}
