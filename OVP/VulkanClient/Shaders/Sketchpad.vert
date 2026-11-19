#version 450

// ==============================================================
// Sketchpad.vert
// 2D Sketchpad vertex shader (port of D3D9Client Sketchpad.fx OrthoVS)
// Part of the ORBITER VISUALISATION PROJECT (OVP) - VulkanClient
// Licensed under the MIT License
// ==============================================================

layout(location = 0) in vec3 inPos; // x, y, pattern length
layout(location = 1) in vec4 inDir; // next point (or texcoord) in xy, previous point in zw
layout(location = 2) in vec4 inClr; // D3DCOLOR layout (0xAABBGGRR OAPI colour read as B8G8R8A8)
layout(location = 3) in vec4 inFnc; // feature switches, same packing as the colour

layout(set = 0, binding = 0, std140) uniform SkpParams {
	mat4 gWVP;         // World * ortho projection (D3D row-major memory, use M * v)
	mat4 gColorMatrix;
	vec4 gPen;
	vec4 gKey;
	vec4 gSize;        // 1/texture size in xy, font scale in zw
	vec4 gTarget;      // 2/target size in xy, target size in zw
	vec4 gWidth;       // pen width, pattern scale, even-width pixel offset
	vec4 gGamma;
	vec4 gNoiseColor;
	vec4 gMisc;        // x = random
	ivec4 gFlags;      // x = feature bits
} p;

const int F_WIDE = 16;

#define LSW 0 // length switch
#define SSW 3 // point side switch

layout(location = 0) out vec4 vSw;
layout(location = 1) out vec4 vTex;
layout(location = 2) out float vLen;
layout(location = 3) out vec4 vColor;

void main()
{
	vec4 posH = p.gWVP * vec4(inPos.xy, 0.0, 1.0);
	vec4 prvH = p.gWVP * vec4(inDir.zw, 0.0, 1.0);

	if (inFnc[LSW] > 0.5)
		vLen = length((posH.xy - prvH.xy) * p.gTarget.zw * 0.5);
	else
		vLen = inPos.z;

	float fSide = round(inFnc[SSW] * 2.0 - 1.0);
	if ((p.gFlags.x & F_WIDE) != 0 && fSide != 0.0) {
		vec4 nxtH = p.gWVP * vec4(inDir.xy, 0.0, 1.0);
		vec2 pixH = p.gTarget.xy * p.gWidth.z * abs(fSide);

		nxtH.xy -= pixH;
		posH.xy -= pixH;
		prvH.xy -= pixH;

		vec2 dNxt = nxtH.xy - posH.xy;
		vec2 dPrv = posH.xy - prvH.xy;
		vec2 nxtS = (dot(dNxt, dNxt) > 1e-12) ? normalize(dNxt) : vec2(1.0, 0.0);
		vec2 prvS = (dot(dPrv, dPrv) > 1e-12) ? normalize(dPrv) : nxtS;
		vec2 sumS = nxtS + prvS;
		vec2 dirS = (dot(sumS, sumS) > 1e-12) ? normalize(sumS) : vec2(-nxtS.y, nxtS.x);
		vec2 latW = dirS * (0.45 * p.gWidth.x) * inversesqrt(max(0.1, 0.5 + dot(nxtS, prvS) * 0.5));

		posH += vec4(latW.y, -latW.x, 0.0, 0.0) * p.gTarget * fSide;
	}

	vColor = inClr.bgra;
	vSw = inFnc;
	vTex = vec4(inDir.xy * p.gSize.xy, inDir.xy * p.gSize.zw);
	gl_Position = vec4(posH.xyz, 1.0);
}
