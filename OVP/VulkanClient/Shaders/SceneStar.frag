#version 450

// ==============================================================
// SceneStar.frag
// Celestial sphere Hipparcos/Tycho star-dot fragment shader
// Part of the ORBITER VISUALISATION PROJECT (OVP) - VulkanClient
// Licensed under the MIT License
// ==============================================================

layout(location = 0) in vec3 vColor;
layout(location = 0) out vec4 outColor;

void main()
{
	outColor = vec4(vColor, 1.0);
}
