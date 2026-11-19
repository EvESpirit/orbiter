// ==============================================================
// VkPadTools.cpp
// Sketchpad drawing tools: VkPadFont, VkPadPen, VkPadBrush
// Part of the ORBITER VISUALISATION PROJECT (OVP) - VulkanClient
// Licensed under the MIT License
// ==============================================================

#define STRICT 1
#include "VkPadTools.h"
#include "VkConfig.h"
#include <cstring>

namespace {

const char *const kFixedFace = "Courier New";
const char *const kSansFace  = "Arial";
const char *const kSerifFace = "Times New Roman";

// D3D9PadFont face policy (D3D9Pad.cpp:1860-1870)
const char *ResolveFace(const char *face, bool prop)
{
	if (!face || !face[0])
		return prop ? kSansFace : kFixedFace;
	if (face[0] == '*')
		return face + 1;
	if (!_stricmp(face, "fixed")) return kFixedFace;
	if (!_stricmp(face, "sans"))  return kSansFace;
	if (!_stricmp(face, "serif")) return kSerifFace;
	if (_stricmp(face, kFixedFace) && _stricmp(face, kSansFace) && _stricmp(face, kSerifFace))
		return prop ? kSansFace : kFixedFace;
	return face;
}

int SketchpadFontPreset()
{
	return g_vkConfig ? g_vkConfig->SketchpadFont : 2;
}

// FONT_CRISP / FONT_ANTIALIAS override the configured default.
DWORD ApplyStyleQuality(DWORD quality, int style)
{
	if (style & FONT_CRISP)     quality = NONANTIALIASED_QUALITY;
	if (style & FONT_ANTIALIAS) quality = ANTIALIASED_QUALITY;
	return quality;
}

void FillStyle(VkFontCache::Key &key, int style)
{
	key.italic    = (style & FONT_ITALIC) != 0;
	key.underline = (style & FONT_UNDERLINE) != 0;
	key.strikeout = (style & FONT_STRIKEOUT) != 0;
}

} // namespace

// ==============================================================
// VkPadFont
// ==============================================================

VkPadFont::VkPadFont(int height, bool prop, const char *face, FontStyle style, int orientation)
	: oapi::Font(height, prop, face, style, orientation)
{
	const char *resolved = ResolveFace(face, prop);
	rotation = (orientation != 0) ? (float)orientation * 0.1f : 0.0f;

	// Default quality as D3D9PadFont (D3D9Pad.cpp:1894-1899): 0 = crisp, 1 = proof, 2 = ClearType
	const int preset = SketchpadFontPreset();
	quality = NONANTIALIASED_QUALITY;
	if (preset == 1) quality = PROOF_QUALITY;
	if (preset >= 2) quality = CLEARTYPE_QUALITY;
	quality = ApplyStyleQuality(quality, style);

	VkFontCache::Key key;
	key.face    = resolved;
	key.height  = height;
	key.weight  = (style & FONT_BOLD) ? FW_BOLD : FW_NORMAL;
	key.quality = quality;
	FillStyle(key, style);

	// The atlas is unrotated; rotation is applied when the text is drawn.
	atlas = VkFontCache::Acquire(key);

	hFont = VkCreateGdiFont(key, orientation);
	if (!hFont) {
		key.face = prop ? kSansFace : kFixedFace;
		hFont = VkCreateGdiFont(key, orientation);
	}
}

VkPadFont::VkPadFont(int height, const char *face, int width, int weight, FontStyle style, float spacing)
	: oapi::Font(height, false, face, style, 0)
{
	// Default quality as D3D9PadFont (D3D9Pad.cpp:1964-1969): 0 = crisp, 1 = antialiased, 2 = proof
	const int preset = SketchpadFontPreset();
	quality = NONANTIALIASED_QUALITY;
	if (preset == 1) quality = ANTIALIASED_QUALITY;
	if (preset >= 2) quality = PROOF_QUALITY;
	quality = ApplyStyleQuality(quality, style);

	VkFontCache::Key key;
	key.face    = (face && face[0]) ? face : kSansFace;
	key.height  = height;
	key.width   = width;
	key.weight  = weight;
	key.quality = quality;
	key.spacing = spacing;
	FillStyle(key, style);

	atlas = VkFontCache::Acquire(key);
	hFont = VkCreateGdiFont(key, 0);
}

VkPadFont::~VkPadFont()
{
	if (hFont)
		DeleteObject(hFont);
}

DWORD VkPadFont::GetCharSize() const
{
	if (!atlas)
		return 0;
	const TEXTMETRICA &tm = atlas->Metrics();
	return MAKELONG(tm.tmHeight - tm.tmInternalLeading, tm.tmAveCharWidth);
}

int VkPadFont::GetLineHeight() const
{
	return atlas ? (int)atlas->Metrics().tmHeight : 0;
}

int VkPadFont::GetTextLength(const char *text, int len) const
{
	if (!atlas || !text)
		return 0;
	const std::string str = VkUtf8ToCp1252(text, len);
	return (int)atlas->Length(str.c_str(), (int)str.length());
}

int VkPadFont::GetIndexByPosition(const char *text, int pos, int len) const
{
	if (!atlas || !text)
		return 0;
	return atlas->GetIndex(text, (float)pos, len);
}

// ==============================================================
// VkPadPen / VkPadBrush
// ==============================================================

VkPadPen::VkPadPen(int style, int w, DWORD col)
	: oapi::Pen(style, w, col)
	, width(w < 1 ? 1 : w)
	, clr(col)
{
	switch (style) {
	case 0:  gdiStyle = PS_NULL;  break;
	case 2:  gdiStyle = PS_DOT;   break;
	default: gdiStyle = PS_SOLID; break;
	}
}

VkPadBrush::VkPadBrush(DWORD col)
	: oapi::Brush(col)
	, clr(col)
{
}
