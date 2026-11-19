// ==============================================================
// VkTextMgr.cpp
// GDI-rasterized glyph atlas (VkTextAtlas) and process-wide font cache
// Part of the ORBITER VISUALISATION PROJECT (OVP) - VulkanClient
// Licensed under the MIT License
// ==============================================================

#define STRICT 1
#include "VkTextMgr.h"
#include "VkSurface.h"
#include "VkLog.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>

// Build with VKCLIENT_FONT_DEBUG defined (e.g. environment CL=/DVKCLIENT_FONT_DEBUG) to dump every
// atlas to VkFont_<face>_<height>_w<weight>_q<quality>.bmp and verify the GPU upload by readback.

namespace {

constexpr uint32_t kAtlasWidth = 2048;
constexpr uint32_t kAtlasWidthMax = 4096; // Guaranteed minimum of maxImageDimension2D

// Glyph layout identical to D3D9Text::Init so text rasterizes pixel-identically.
// Returns the required atlas height for the given width.
uint32_t LayoutGlyphs(const SIZE (&ext)[256], int maxCharW, int a, int d, uint32_t texW, POINT (&pos)[256])
{
	const int h = a + d;
	int x = 5;
	int y = 5 + h; // baseline
	for (int c = 0; c < 256; ++c) {
		pos[c].x = x;
		pos[c].y = y;
		x += ext[c].cx + 4;
		if (x + maxCharW >= (int)texW) {
			x = 5;
			y += h + 5;
		}
	}
	return (uint32_t)(y + h + 1);
}

#ifdef VKCLIENT_FONT_DEBUG
void DumpCoverageBmp(const char *fname, const std::vector<uint8_t> &cov, uint32_t w, uint32_t h)
{
	FILE *f = std::fopen(fname, "wb");
	if (!f)
		return;
	const uint32_t rowBytes = (w * 3u + 3u) & ~3u;
	BITMAPFILEHEADER bfh{};
	BITMAPINFOHEADER bih{};
	bfh.bfType    = 0x4D42;
	bfh.bfOffBits = sizeof(bfh) + sizeof(bih);
	bfh.bfSize    = bfh.bfOffBits + rowBytes * h;
	bih.biSize     = sizeof(bih);
	bih.biWidth    = (LONG)w;
	bih.biHeight   = -(LONG)h;
	bih.biPlanes   = 1;
	bih.biBitCount = 24;
	std::fwrite(&bfh, sizeof(bfh), 1, f);
	std::fwrite(&bih, sizeof(bih), 1, f);
	std::vector<uint8_t> row(rowBytes, 0);
	for (uint32_t y = 0; y < h; ++y) {
		for (uint32_t x = 0; x < w; ++x)
			row[x * 3 + 0] = row[x * 3 + 1] = row[x * 3 + 2] = cov[(size_t)y * w + x];
		std::fwrite(row.data(), 1, rowBytes, f);
	}
	std::fclose(f);
}
#endif

std::mutex g_cacheMutex;
std::vector<std::pair<VkFontCache::Key, VkTextAtlasPtr>> g_cache;

} // namespace

// ==============================================================
// VkTextAtlas
// ==============================================================

VkTextAtlas::~VkTextAtlas()
{
	// gpuSurf is intentionally not touched here: atlases may outlive the device.
	// VkFontCache::ReleaseGpuResources() frees GPU textures while the device is alive.
}

bool VkTextAtlas::Init(HFONT hFont, float space)
{
	if (!hFont) {
		VkLogErr("VkTextAtlas::Init: NULL font");
		return false;
	}

	spacing = space;
	GetObjectA(hFont, sizeof(LOGFONTA), &lf);

	HDC hDC = CreateCompatibleDC(nullptr);
	if (!hDC) {
		VkLogErr("VkTextAtlas::Init: CreateCompatibleDC failed");
		return false;
	}
	HGDIOBJ hOldFont = SelectObject(hDC, hFont);

	if (!GetTextMetricsA(hDC, &tm)) {
		VkLogErr("VkTextAtlas::Init: GetTextMetrics failed (%s)", lf.lfFaceName);
		SelectObject(hDC, hOldFont);
		DeleteDC(hDC);
		return false;
	}

	SIZE ext[256]{};
	for (int c = 0; c < 256; ++c) {
		const char ch = (char)c;
		GetTextExtentPoint32A(hDC, &ch, 1, &ext[c]);
	}

	const int a = tm.tmAscent + 1;
	const int d = tm.tmDescent + 1;
	const int h = a + d;
	POINT pos[256]{};

	texW = kAtlasWidth;
	texH = LayoutGlyphs(ext, tm.tmMaxCharWidth, a, d, texW, pos);
	if (texH > kAtlasWidth) {
		texW = kAtlasWidthMax;
		texH = LayoutGlyphs(ext, tm.tmMaxCharWidth, a, d, texW, pos);
	}

	for (int c = 0; c < 256; ++c) {
		VkGlyph &g = glyphs[c];
		g.sp = (float)ext[c].cx;
		g.w  = (float)(ext[c].cx + 3);
		g.h  = (float)h;
	}

	if (texH > kAtlasWidthMax || tm.tmMaxCharWidth + 5 >= (int)texW) {
		VkLogWarn("Font '%s' height %d is too large for a glyph atlas (metrics only)", lf.lfFaceName, (int)lf.lfHeight);
		texW = texH = 0;
		SelectObject(hDC, hOldFont);
		DeleteDC(hDC);
		return true;
	}

	BITMAPINFO bmi{};
	bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth       = (LONG)texW;
	bmi.bmiHeader.biHeight      = -(LONG)texH;
	bmi.bmiHeader.biPlanes      = 1;
	bmi.bmiHeader.biBitCount    = 32;
	bmi.bmiHeader.biCompression = BI_RGB;

	void *bits = nullptr;
	HBITMAP hDib = CreateDIBSection(hDC, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
	if (!hDib || !bits) {
		VkLogErr("VkTextAtlas::Init: CreateDIBSection %ux%u failed", texW, texH);
		if (hDib) DeleteObject(hDib);
		SelectObject(hDC, hOldFont);
		DeleteDC(hDC);
		texW = texH = 0;
		return false;
	}
	std::memset(bits, 0, (size_t)texW * texH * 4u);
	HGDIOBJ hOldBmp = SelectObject(hDC, hDib);

	SetTextAlign(hDC, TA_BASELINE | TA_LEFT);
	SetTextColor(hDC, RGB(255, 255, 255));
	SetBkColor(hDC, RGB(0, 0, 0));
	SetBkMode(hDC, TRANSPARENT);

	const float tw = 1.0f / (float)texW;
	const float th = 1.0f / (float)texH;
	for (int c = 0; c < 256; ++c) {
		const char ch = (char)c;
		TextOutA(hDC, pos[c].x, pos[c].y, &ch, 1);

		VkGlyph &g = glyphs[c];
		g.tx0 = (float)(pos[c].x - 1) * tw;
		g.tx1 = (float)(pos[c].x - 1 + ext[c].cx + 3) * tw;
		g.ty0 = (float)(pos[c].y - a) * th;
		g.ty1 = (float)(pos[c].y + d) * th;
	}
	GdiFlush();

	// Coverage = max(0.7 R, G): the D3D9 Sketchpad.fx font term, which also tames ClearType fringes.
	const size_t numPx = (size_t)texW * texH;
	coverage.resize(numPx);
	const uint8_t *src = static_cast<const uint8_t *>(bits);
	for (size_t i = 0; i < numPx; ++i) {
		const float r = (float)src[i * 4 + 2];
		const float g = (float)src[i * 4 + 1];
		coverage[i] = (uint8_t)std::min(255.0f, std::max(r * 0.7f, g) + 0.5f);
	}

	SelectObject(hDC, hOldBmp);
	SelectObject(hDC, hOldFont);
	DeleteObject(hDib);
	DeleteDC(hDC);

#ifdef VKCLIENT_FONT_DEBUG
	char dumpName[128];
	std::snprintf(dumpName, sizeof(dumpName), "VkFont_%s_%d_w%d_q%d.bmp",
		lf.lfFaceName, (int)lf.lfHeight, (int)lf.lfWeight, (int)lf.lfQuality);
	DumpCoverageBmp(dumpName, coverage, texW, texH);
#endif
	return true;
}

float VkTextAtlas::Length(const char *str, int len) const
{
	if (!str)
		return 0.0f;
	const auto *s = reinterpret_cast<const uint8_t *>(str);
	float l = 0.0f;
	for (int i = 0; (i < len || len <= 0) && s[i]; ++i)
		l += glyphs[s[i]].sp + spacing;
	l -= spacing;
	return std::max(0.0f, l);
}

int VkTextAtlas::GetIndex(const char *str, float pos, int len) const
{
	if (!str)
		return 0;
	const auto *s = reinterpret_cast<const uint8_t *>(str);
	float best = 1e6f;
	float l = 0.0f;
	int idx = 0;
	for (int i = 0; i < len || len < 0; ++i) {
		const float del = std::fabs(pos - l);
		if (del < best) {
			best = del;
			idx = i;
		} else {
			break;
		}
		if (s[i] == 0)
			break;
		l += glyphs[s[i]].sp + spacing;
	}
	return idx;
}

VkSurfNative *VkTextAtlas::GetSurface(VkContext &context, VkSwapchain &swap)
{
	if (gpuSurf || coverage.empty())
		return gpuSurf;

	gpuSurf = VkSurfNative::Create(context, swap, texW, texH, OAPISURFACE_TEXTURE | OAPISURFACE_ALPHA);
	if (!gpuSurf) {
		VkLogErr("Glyph atlas texture %ux%u creation failed (%s)", texW, texH, lf.lfFaceName);
		return nullptr;
	}

	std::vector<uint8_t> bgra(coverage.size() * 4u);
	for (size_t i = 0; i < coverage.size(); ++i) {
		bgra[i * 4 + 0] = 255;
		bgra[i * 4 + 1] = 255;
		bgra[i * 4 + 2] = 255;
		bgra[i * 4 + 3] = coverage[i];
	}
	if (!gpuSurf->UploadBgra(bgra.data(), bgra.size())) {
		VkLogErr("Glyph atlas upload failed (%s)", lf.lfFaceName);
		delete gpuSurf;
		gpuSurf = nullptr;
		return nullptr;
	}

	char surfName[96];
	std::snprintf(surfName, sizeof(surfName), "FontAtlas:%s:%d", lf.lfFaceName, (int)lf.lfHeight);
	gpuSurf->SetName(surfName);

#ifdef VKCLIENT_FONT_DEBUG
	std::vector<uint8_t> back;
	uint32_t bw = 0, bh = 0;
	size_t mismatches = 0;
	if (gpuSurf->ReadbackBgra(back, bw, bh) && bw == texW && bh == texH) {
		for (size_t i = 0; i < coverage.size(); ++i)
			if (back[i * 4 + 3] != coverage[i] || back[i * 4 + 2] != 255)
				++mismatches;
		VkLogAlw("[FONTDBG] atlas '%s' %d: GPU readback %ux%u, mismatches=%zu", lf.lfFaceName, (int)lf.lfHeight, bw, bh, mismatches);
	} else {
		VkLogErr("[FONTDBG] atlas '%s' %d: GPU readback failed", lf.lfFaceName, (int)lf.lfHeight);
	}
#endif
	return gpuSurf;
}

void VkTextAtlas::ReleaseGpu()
{
	delete gpuSurf;
	gpuSurf = nullptr;
}

// ==============================================================
// VkFontCache
// ==============================================================

bool VkFontCache::Key::operator==(const Key &o) const
{
	return height == o.height && width == o.width && weight == o.weight &&
		italic == o.italic && underline == o.underline && strikeout == o.strikeout &&
		quality == o.quality && spacing == o.spacing && _stricmp(face.c_str(), o.face.c_str()) == 0;
}

VkTextAtlasPtr VkFontCache::Acquire(const Key &key)
{
	std::lock_guard<std::mutex> lock(g_cacheMutex);
	for (const auto &entry : g_cache)
		if (entry.first == key)
			return entry.second;

	HFONT hFont = VkCreateGdiFont(key, 0);
	if (!hFont) {
		VkLogErr("CreateFont failed (%s, %d)", key.face.c_str(), key.height);
		return nullptr;
	}
	auto atlas = std::make_shared<VkTextAtlas>();
	const bool ok = atlas->Init(hFont, key.spacing);
	DeleteObject(hFont);
	if (!ok)
		return nullptr;

	VkLogInfo("New font atlas '%s' height=%d weight=%d quality=%u -> %ux%u (%u KB)",
		key.face.c_str(), key.height, key.weight, (unsigned)key.quality,
		atlas->TexWidth(), atlas->TexHeight(), atlas->TexWidth() * atlas->TexHeight() * 4u / 1024u);
	g_cache.emplace_back(key, atlas);
	return atlas;
}

void VkFontCache::ReleaseGpuResources()
{
	std::lock_guard<std::mutex> lock(g_cacheMutex);
	for (auto &entry : g_cache)
		entry.second->ReleaseGpu();
}

void VkFontCache::Clear()
{
	std::lock_guard<std::mutex> lock(g_cacheMutex);
	if (!g_cache.empty())
		VkLogInfo("Clearing font cache (%zu atlases)", g_cache.size());
	g_cache.clear();
}

// ==============================================================
// Helpers
// ==============================================================

std::string VkUtf8ToCp1252(const char *utf8, int len)
{
	if (!utf8)
		return {};
	const int n = (len > 0) ? len : (int)std::strlen(utf8);
	if (n <= 0)
		return {};

	bool ascii = true;
	for (int i = 0; i < n && ascii; ++i)
		ascii = (static_cast<uint8_t>(utf8[i]) < 0x80);
	if (ascii)
		return std::string(utf8, (size_t)n);

	// MB_ERR_INVALID_CHARS fails on non-UTF-8 input: assume a legacy CP-1252 string.
	const int wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, n, nullptr, 0);
	if (wlen <= 0)
		return std::string(utf8, (size_t)n);
	std::wstring wide((size_t)wlen, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, utf8, n, wide.data(), wlen);

	const int clen = WideCharToMultiByte(1252, 0, wide.data(), wlen, nullptr, 0, nullptr, nullptr);
	if (clen <= 0)
		return std::string(utf8, (size_t)n);
	std::string out((size_t)clen, '\0');
	WideCharToMultiByte(1252, 0, wide.data(), wlen, out.data(), clen, nullptr, nullptr);
	return out;
}

HFONT VkCreateGdiFont(const VkFontCache::Key &key, int escapement)
{
	// Same parameters as D3D9PadFont: ANSI charset, CLIP_STROKE_PRECIS, pitch/family 49.
	return CreateFontA(key.height, key.width, escapement, escapement, key.weight,
		key.italic, key.underline, key.strikeout, ANSI_CHARSET, OUT_DEFAULT_PRECIS,
		CLIP_STROKE_PRECIS, key.quality, 49, key.face.c_str());
}
