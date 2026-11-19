// ==============================================================
// VkTextMgr.h
// GDI-rasterized glyph atlas (VkTextAtlas) and process-wide font cache
// Part of the ORBITER VISUALISATION PROJECT (OVP) - VulkanClient
// Licensed under the MIT License
// ==============================================================

#ifndef __VKCLIENT_VKTEXTMGR_H
#define __VKCLIENT_VKTEXTMGR_H

#include <windows.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class VkContext;
class VkSwapchain;
class VkSurfNative;

/**
 * \brief Placement of one character inside the glyph atlas.
 */
struct VkGlyph {
	float w = 0.0f;   ///< Quad width [px] (advance + 3 px padding)
	float h = 0.0f;   ///< Quad height [px] (ascent + descent + 2)
	float sp = 0.0f;  ///< Horizontal advance [px]
	float tx0 = 0.0f, ty0 = 0.0f; ///< Normalized top-left texture coordinate
	float tx1 = 0.0f, ty1 = 0.0f; ///< Normalized bottom-right texture coordinate
};

/**
 * \brief Immutable glyph atlas for character codes 0..255 of one GDI font.
 *
 * Glyphs are rasterized once with GDI on the CPU, so atlases can be created
 * without a Vulkan device. The GPU texture is uploaded on first use and can be
 * dropped at device teardown and re-uploaded in a later session.
 */
class VkTextAtlas {
public:
	VkTextAtlas() = default;
	~VkTextAtlas();

	VkTextAtlas(const VkTextAtlas &) = delete;
	VkTextAtlas &operator=(const VkTextAtlas &) = delete;

	/// Rasterizes the atlas. The caller keeps ownership of \a hFont.
	bool Init(HFONT hFont, float spacing = 0.0f);

	const TEXTMETRICA &Metrics() const { return tm; }
	const LOGFONTA &LogFont() const { return lf; }
	const VkGlyph &Glyph(uint8_t c) const { return glyphs[c]; }
	float Spacing() const { return spacing; }
	float CellHeight() const { return glyphs[0].h; }
	int LineSpace(int percent = 0) const { return tm.tmHeight + (tm.tmHeight * percent) / 100; }

	/// Width of a CP-1252 string [px]; len <= 0 means up to the terminating zero.
	float Length(const char *str, int len = 0) const;
	/// Advance of a single character [px].
	float Length(uint8_t c) const { return glyphs[c].sp + spacing; }
	/// Index of the character boundary closest to \a pos [px]; len < 0 means up to the terminating zero.
	int GetIndex(const char *str, float pos, int len = -1) const;

	uint32_t TexWidth() const { return texW; }
	uint32_t TexHeight() const { return texH; }
	bool HasTexture() const { return !coverage.empty(); }
	const std::vector<uint8_t> &Coverage() const { return coverage; }

	/// Returns the GPU atlas texture, uploading it on first use (nullptr if unavailable).
	VkSurfNative *GetSurface(VkContext &context, VkSwapchain &swap);
	/// Frees the GPU texture. The device must be idle.
	void ReleaseGpu();

private:
	TEXTMETRICA tm{};
	LOGFONTA lf{};
	VkGlyph glyphs[256]{};
	float spacing = 0.0f;
	uint32_t texW = 0;
	uint32_t texH = 0;
	std::vector<uint8_t> coverage; ///< texW * texH glyph coverage, 1 byte per pixel
	VkSurfNative *gpuSurf = nullptr;
};

using VkTextAtlasPtr = std::shared_ptr<VkTextAtlas>;

/**
 * \brief Process-lifetime cache sharing atlases between identical fonts.
 */
namespace VkFontCache {

/// Font parameters identifying an atlas.
struct Key {
	std::string face;
	int height = 0;
	int width = 0;
	int weight = FW_NORMAL;
	bool italic = false;
	bool underline = false;
	bool strikeout = false;
	DWORD quality = NONANTIALIASED_QUALITY;
	float spacing = 0.0f;

	bool operator==(const Key &o) const;
};

/// Returns the cached atlas for \a key, rasterizing it on first request (nullptr on failure).
VkTextAtlasPtr Acquire(const Key &key);
/// Frees every atlas GPU texture. The device must be idle.
void ReleaseGpuResources();
/// Destroys all cached atlases (GPU textures must already be released).
void Clear();

} // namespace VkFontCache

/// Converts UTF-8 text to Windows-1252 (atlas code page); invalid UTF-8 is passed through as legacy CP-1252.
std::string VkUtf8ToCp1252(const char *utf8, int len = 0);

/// Creates a GDI font with the parameters of an atlas key (escapement in 1/10 deg).
HFONT VkCreateGdiFont(const VkFontCache::Key &key, int escapement = 0);

#endif // !__VKCLIENT_VKTEXTMGR_H
