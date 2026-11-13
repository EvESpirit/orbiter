#version 450

// ==============================================================
// Blit.frag
// 2D textured & color-keyed blit fragment shader
// Part of the ORBITER VISUALISATION PROJECT (OVP) - VulkanClient
// Licensed under the MIT License
// ==============================================================

layout(set = 0, binding = 0) uniform sampler2D uTex;

layout(push_constant) uniform BlitPushConstants {
	vec4 dstRectNdc;
	vec4 uvRect;
	vec4 colorKey; // rgb = color key in [0..1], a = tolerance (> 0 active, <= 0 disabled)
} pc;

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

void main()
{
	vec4 c = texture(uTex, vUV);
	if (pc.colorKey.a > 0.0) {
		vec3 diff = abs(c.rgb - pc.colorKey.rgb);
		if (diff.r < pc.colorKey.a && diff.g < pc.colorKey.a && diff.b < pc.colorKey.a)
			discard;
	}
	outColor = c;
}
