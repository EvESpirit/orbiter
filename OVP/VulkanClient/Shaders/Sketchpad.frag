#version 450

// ==============================================================
// Sketchpad.frag
// 2D Sketchpad fragment shader (port of D3D9Client Sketchpad.fx SketchpadPS)
// Part of the ORBITER VISUALISATION PROJECT (OVP) - VulkanClient
// Licensed under the MIT License
// ==============================================================

layout(set = 0, binding = 0, std140) uniform SkpParams {
	mat4 gWVP;
	mat4 gColorMatrix;
	vec4 gPen;
	vec4 gKey;
	vec4 gSize;
	vec4 gTarget;
	vec4 gWidth;
	vec4 gGamma;
	vec4 gNoiseColor;
	vec4 gMisc;
	ivec4 gFlags;
} p;

layout(set = 0, binding = 1) uniform sampler2D tTex;   // source texture
layout(set = 0, binding = 2) uniform sampler2D tFnt;   // glyph atlas, coverage in alpha
layout(set = 0, binding = 3) uniform sampler2D tNoise;

const int F_TEX     = 1;
const int F_FONT    = 2;
const int F_KEY     = 4;
const int F_DASH    = 8;
const int F_EFFECTS = 32;

#define TSW 2 // fragment / pen / texture colour switch
#define CSW 1 // colour key / font switch

const float kKeyTolerance = 0.01;

layout(location = 0) in vec4 vSw;
layout(location = 1) in vec4 vTex;
layout(location = 2) in float vLen;
layout(location = 3) in vec4 vColor;

layout(location = 0) out vec4 outColor;

void main()
{
	const int fl = p.gFlags.x;

	vec4 t = vec4(1.0);
	float f = 1.0;
	if ((fl & F_FONT) != 0) f = texture(tFnt, vTex.zw).a;
	if ((fl & F_TEX) != 0)  t = texture(tTex, vTex.xy);

	// Select colour source
	vec4 c = vColor;
	if (vSw[TSW] > 0.2) c = p.gPen;
	if (vSw[TSW] > 0.8) c = t;
	if (vSw[CSW] > 0.8) c.a *= f;

	// Colour keying
	if ((fl & F_TEX) != 0 && (fl & F_KEY) != 0) {
		vec4 x = abs(c - p.gKey);
		if (x.r < kKeyTolerance && x.g < kKeyTolerance && x.b < kKeyTolerance) {
			if (vSw[CSW] > 0.2 && vSw[CSW] < 0.8) discard;
		}
	}

	if ((fl & F_DASH) != 0) {
		if (fract(vLen * p.gWidth.y) > 0.5) discard;
	}

	if ((fl & F_EFFECTS) != 0) {
		c = p.gColorMatrix * c;
		c.rgb = pow(max(c.rgb, vec3(0.0)), p.gGamma.rgb);
		c.rgb += clamp(max(c.r, max(c.g, c.b)) - 1.0, 0.0, 1.0);
		float noise = texture(tNoise, floor(gl_FragCoord.xy) * (1.0 / 128.0) + vec2(p.gMisc.x, p.gMisc.x * 7.0)).r * 2.0 - 1.0;
		c.rgb += mix(vec3(1.0), c.rgb, p.gNoiseColor.a) * p.gNoiseColor.rgb * noise;
	}

	outColor = clamp(c, 0.0, 1.0);
}
