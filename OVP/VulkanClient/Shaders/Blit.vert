#version 450

// ==============================================================
// Blit.vert
// Fullscreen / sub-rectangle 2D textured & color-keyed blit vertex shader
// Part of the ORBITER VISUALISATION PROJECT (OVP) - VulkanClient
// Licensed under the MIT License
// ==============================================================

layout(push_constant) uniform BlitPushConstants {
	vec4 dstRectNdc; // xy = top-left NDC (-1..+1), zw = bottom-right NDC (-1..+1)
	vec4 uvRect;     // xy = uv0, zw = uv1
	vec4 colorKey;   // rgb = color key in [0..1], a = tolerance (> 0 active, <= 0 disabled)
} pc;

layout(location = 0) out vec2 vUV;

void main()
{
	const vec2 corners[6] = vec2[6](
		vec2(0.0, 0.0),
		vec2(1.0, 0.0),
		vec2(1.0, 1.0),
		vec2(0.0, 0.0),
		vec2(1.0, 1.0),
		vec2(0.0, 1.0)
	);

	vec2 t = corners[gl_VertexIndex];
	vec2 ndc = mix(pc.dstRectNdc.xy, pc.dstRectNdc.zw, t);
	vUV = mix(pc.uvRect.xy, pc.uvRect.zw, t);
	gl_Position = vec4(ndc, 0.0, 1.0);
}
