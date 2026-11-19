// ==============================================================
// VkPadTools.h
// Sketchpad drawing tools: VkPadFont, VkPadPen, VkPadBrush
// Part of the ORBITER VISUALISATION PROJECT (OVP) - VulkanClient
// Licensed under the MIT License
// ==============================================================

#ifndef __VKCLIENT_VKPADTOOLS_H
#define __VKCLIENT_VKPADTOOLS_H

#include "Orbitersdk.h"
#include "DrawAPI.h"
#include "VkTextMgr.h"

/**
 * \brief Sketchpad color: raw OAPI value (0xAABBGGRR) plus normalized RGBA.
 */
struct VkSkpColor {
	VkSkpColor() = default;
	explicit VkSkpColor(DWORD abgr) : dclr(abgr), fclr(abgr) {}

	DWORD dclr = 0;        ///< As passed through the API (0xAABBGGRR)
	oapi::FVECTOR4 fclr;   ///< r, g, b, a in [0, 1]
};

/**
 * \brief Font backed by a shared GDI-rasterized glyph atlas.
 */
class VkPadFont : public oapi::Font {
public:
	/**
	 * \brief oapiCreateFont variant.
	 * \note Face names follow D3D9PadFont: "Fixed", "Sans" and "Serif" map to Courier New,
	 *   Arial and Times New Roman; other names fall back to Arial (prop) or Courier New
	 *   unless prefixed with '*', which selects the named face literally.
	 */
	VkPadFont(int height, bool prop, const char *face, FontStyle style = FONT_NORMAL, int orientation = 0);

	/// oapiCreateFontEx variant: face used as given, explicit width/weight/spacing.
	VkPadFont(int height, const char *face, int width, int weight, FontStyle style, float spacing);

	~VkPadFont() override;

	HFONT GetGDIFont() const override { return hFont; }

	const VkTextAtlasPtr &Atlas() const { return atlas; }
	bool IsValid() const { return atlas != nullptr; }
	float Rotation() const { return rotation; } ///< Text rotation [deg]
	DWORD Quality() const { return quality; }

	/// Packed character size: MAKELONG(height - internal leading, average width), as D3D9Pad::GetCharSize.
	DWORD GetCharSize() const;
	/// Line height including internal leading [px].
	int GetLineHeight() const;
	/// Width of a UTF-8/CP-1252 string [px]; len <= 0 means up to the terminating zero.
	int GetTextLength(const char *text, int len = 0) const;
	/// Character index closest to \a pos [px] in a CP-1252 string.
	int GetIndexByPosition(const char *text, int pos, int len = -1) const;

private:
	VkTextAtlasPtr atlas;
	HFONT hFont = nullptr;  ///< GDI font (rotated) for legacy GDI drawing (Mfd.cpp)
	DWORD quality = NONANTIALIASED_QUALITY;
	float rotation = 0.0f;
};

/**
 * \brief Line drawing pen.
 */
class VkPadPen : public oapi::Pen {
public:
	/**
	 * \param style 0 = invisible, 2 = dotted/dashed, any other value = solid
	 * \param width line width [px] (values < 1 are drawn 1 px wide)
	 * \param col line colour (0xAABBGGRR)
	 */
	VkPadPen(int style, int width, DWORD col);
	~VkPadPen() override = default;

	int Style() const { return gdiStyle; } ///< PS_NULL, PS_DOT or PS_SOLID
	bool IsNull() const { return gdiStyle == PS_NULL; }
	bool IsDashed() const { return gdiStyle == PS_DOT; }
	int Width() const { return width; }
	const VkSkpColor &Color() const { return clr; }

private:
	int gdiStyle = PS_SOLID;
	int width = 1;
	VkSkpColor clr;
};

/**
 * \brief Solid fill brush.
 */
class VkPadBrush : public oapi::Brush {
public:
	/// \param col fill colour (0xAABBGGRR)
	explicit VkPadBrush(DWORD col);
	~VkPadBrush() override = default;

	const VkSkpColor &Color() const { return clr; }

private:
	VkSkpColor clr;
};

#endif // !__VKCLIENT_VKPADTOOLS_H
