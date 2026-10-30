// ==============================================================
// VulkanClient.cpp
// Main oapi::GraphicsClient implementation for OpenOrbiter Vulkan 1.3
// Part of the ORBITER VISUALISATION PROJECT (OVP) - VulkanClient
// Licensed under the MIT License
// ==============================================================

#define STRICT 1
#define ORBITER_MODULE

#include "VulkanClient.h"
#include "VideoTab.h"
#include "VkConfig.h"
#include "VkLog.h"
#include "imgui.h"
#include <cstring>
#include <filesystem>

using namespace oapi;

namespace {
HINSTANCE g_hInst = nullptr;
VulkanClient *g_vkClient = nullptr;

// STUB (Phase 1): Minimal surface token replaced by VkSurfNative in Phase 1
struct VkPhase0SurfaceStub {
	DWORD w = 256;
	DWORD h = 256;
	int refCount = 1;
	bool managed = false;
};

// STUB (Phase 1): Minimal font handle replaced by VkPadFont in Phase 1
class VkPhase0FontStub : public oapi::Font {
public:
	VkPhase0FontStub(int height, bool prop, const char *face, FontStyle style, int orientation)
		: oapi::Font(height, prop, face, style, orientation)
		, cellH(height != 0 ? (height > 0 ? height : -height) : 14)
	{}
	int cellH;
};

// STUB (Phase 1): Minimal pen handle replaced by VkPadPen in Phase 1
class VkPhase0PenStub : public oapi::Pen {
public:
	VkPhase0PenStub(int style, int width, DWORD col) : oapi::Pen(style, width, col) {}
};

// STUB (Phase 1): Minimal brush handle replaced by VkPadBrush in Phase 1
class VkPhase0BrushStub : public oapi::Brush {
public:
	explicit VkPhase0BrushStub(DWORD col) : oapi::Brush(col) {}
};

// STUB (Phase 1): Safe no-op Sketchpad replaced by VkPad in Phase 1.
// Base oapi::Sketchpad methods contain assert(false) and vessels dereference
// Sketchpad* during panel/MFD setup, so all virtual methods are safely stubbed.
class VkPhase0SketchpadStub : public oapi::Sketchpad {
public:
	VkPhase0SketchpadStub(SURFHANDLE s, DWORD defW, DWORD defH)
		: oapi::Sketchpad(s), surfW(defW), surfH(defH)
	{
		if (s) {
			const auto *st = reinterpret_cast<const VkPhase0SurfaceStub *>(s);
			surfW = st->w;
			surfH = st->h;
		}
	}

	Font *SetFont(Font *f) override { Font *prev = curFont; curFont = f; return prev; } // STUB
	Pen *SetPen(Pen *p) override { Pen *prev = curPen; curPen = p; return prev; } // STUB
	Brush *SetBrush(Brush *b) override { Brush *prev = curBrush; curBrush = b; return prev; } // STUB
	void SetTextAlign(TAlign_horizontal = LEFT, TAlign_vertical = TOP) override {} // STUB
	DWORD SetTextColor(DWORD col) override { DWORD prev = textCol; textCol = col; return prev; } // STUB
	DWORD SetBackgroundColor(DWORD col) override { DWORD prev = bgCol; bgCol = col; return prev; } // STUB
	void SetBackgroundMode(BkgMode) override {} // STUB
	DWORD GetCharSize() override { return (8u << 16) | 14u; } // STUB: non-zero metrics prevent divide-by-zero
	DWORD GetTextWidth(const char *str, int len = 0) override { // STUB
		if (!str) return 0;
		size_t n = (len > 0) ? (size_t)len : std::strlen(str);
		return (DWORD)(n * 8);
	}
	void SetOrigin(int x, int y) override { origX = x; origY = y; } // STUB
	void GetOrigin(int *x, int *y) const override { if (x) *x = origX; if (y) *y = origY; } // STUB
	bool Text(int, int, const char *, int) override { return true; } // STUB
	void Pixel(int, int, DWORD) override {} // STUB
	void MoveTo(int, int) override {} // STUB
	void LineTo(int, int) override {} // STUB
	void Line(int, int, int, int) override {} // STUB
	void Rectangle(int, int, int, int) override {} // STUB
	void Ellipse(int, int, int, int) override {} // STUB
	void Polygon(const IVECTOR2 *, int) override {} // STUB
	void Polyline(const IVECTOR2 *, int) override {} // STUB
	void GetRenderSurfaceSize(LPSIZE size) override { // STUB
		if (size) { size->cx = (LONG)surfW; size->cy = (LONG)surfH; }
	}
	void QuickPen(DWORD, float = 1.0f, DWORD = 1) override {} // STUB
	void QuickBrush(DWORD) override {} // STUB
	void SetGlobalLineScale(float = 1.0f, float = 1.0f) override {} // STUB
	void SetWorldTransform(const FMATRIX4 * = NULL) override {} // STUB
	const FMATRIX4 *ViewMatrix() const override { return &identMat; } // STUB
	const FMATRIX4 *ProjectionMatrix() const override { return &identMat; } // STUB
	const FMATRIX4 *GetViewProjectionMatrix() const override { return &identMat; } // STUB
	void SetViewMode(SkpView = ORTHO) override {} // STUB
	void SetWorldTransform2D(float = 1.0f, float = 0.0f, const IVECTOR2 * = NULL, const IVECTOR2 * = NULL) override {} // STUB
	void ClipRect(const LPRECT = NULL) override {} // STUB
	void Clipper(int, const VECTOR3 * = NULL, double = 0.0, double = 0.0) override {} // STUB
	void DepthEnable(bool) override {} // STUB
	int DrawMeshGroup(const MESHHANDLE, DWORD, MeshFlags = MeshFlags::SMOOTH_SHADE, const SURFHANDLE = NULL) override { return 0; } // STUB
	void CopyRect(const SURFHANDLE, const LPRECT, int, int) override {} // STUB
	void StretchRect(const SURFHANDLE, const LPRECT = NULL, const LPRECT = NULL) override {} // STUB
	void RotateRect(const SURFHANDLE, const LPRECT, int, int, float = 0.0f, float = 1.0f, float = 1.0f) override {} // STUB
	void ColorKey(const SURFHANDLE, const LPRECT, int, int) override {} // STUB
	void TextEx(float, float, const char *, float = 1.0f, float = 0.0f) override {} // STUB
	void DrawPoly(const HPOLY, DWORD = 0) override {} // STUB
	void Lines(const FVECTOR2 *, int) override {} // STUB
	void SetViewMatrix(const FMATRIX4 * = NULL) override {} // STUB
	void SetProjectionMatrix(const FMATRIX4 * = NULL) override {} // STUB
	const FMATRIX4 *GetColorMatrix() override { return &identMat; } // STUB
	void SetColorMatrix(const FMATRIX4 * = NULL) override {} // STUB
	void SetBrightness(const FVECTOR4 * = NULL) override {} // STUB
	FVECTOR4 GetRenderParam(RenderParam) override { return FVECTOR4(0, 0, 0, 0); } // STUB
	void SetRenderParam(RenderParam, const FVECTOR4 * = NULL) override {} // STUB
	void SetBlendState(BlendState = (BlendState)(BlendState::ALPHABLEND | BlendState::FILTER_LINEAR)) override {} // STUB
	void PushWorldTransform() override {} // STUB
	void PopWorldTransform() override {} // STUB
	void SetWorldScaleTransform2D(const FVECTOR2 * = NULL, const IVECTOR2 * = NULL) override {} // STUB
	void GradientFillRect(const LPRECT, DWORD, DWORD, bool = false) override {} // STUB
	void ColorFill(DWORD, const LPRECT) override {} // STUB
	void StretchRegion(const skpRegion *, const SURFHANDLE, const LPRECT) override {} // STUB
	void CopyTetragon(const SURFHANDLE, const LPRECT, const FVECTOR2[4]) override {} // STUB
	void FillTetragon(DWORD, const FVECTOR2[4]) override {} // STUB
	void ColorCompatibility(bool) override {} // STUB
	void Clear(DWORD = 0, bool = true, bool = true) override {} // STUB
	void SetClipDistance(float, float) override {} // STUB
	void SetWorldBillboard(const FVECTOR3 &, float = 1.0f, bool = true, const FVECTOR3 * = NULL) override {} // STUB

private:
	DWORD surfW = 1280;
	DWORD surfH = 720;
	int origX = 0;
	int origY = 0;
	DWORD textCol = 0xFFFFFF;
	DWORD bgCol = 0;
	Font *curFont = nullptr;
	Pen *curPen = nullptr;
	Brush *curBrush = nullptr;
	FMATRIX4 identMat{};
};
} // namespace

extern "C" {
__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;

// STUB (Phase 5): Do not export gcBindCoreMethod / gcGetGUICore until Phase 5
// populates every gcCore2 function pointer.
}

// ==============================================================
// DLL Module Entry Points
// ==============================================================

DLLCLBK void InitModule(HINSTANCE hDLL)
{
	g_hInst = hDLL;
	g_vkConfig = new VkConfig();

	VkLogInit("VulkanClient.log", g_vkConfig->DebugLvl);
	VkLogAlw("================ InitModule (VulkanClient) ================");

	if (g_vkConfig->ShaderCacheUse) {
		std::error_code ec;
		std::filesystem::create_directories("Cache/VulkanClient/Shaders", ec);
	}

	g_vkClient = new VulkanClient(hDLL);
	if (!oapiRegisterGraphicsClient(g_vkClient)) {
		VkLogErr("oapiRegisterGraphicsClient failed");
		delete g_vkClient;
		g_vkClient = nullptr;
	}
}

DLLCLBK void ExitModule(HINSTANCE hDLL)
{
	(void)hDLL;
	VkLogAlw("================ ExitModule (VulkanClient) ================");

	if (g_vkClient) {
		oapiUnregisterGraphicsClient(g_vkClient);
		delete g_vkClient;
		g_vkClient = nullptr;
	}

	delete g_vkConfig;
	g_vkConfig = nullptr;

	VkLogClose();
}

// ==============================================================
// VulkanClient Implementation
// ==============================================================

VulkanClient::VulkanClient(HINSTANCE hInstance)
	: GraphicsClient(hInstance)
{
}

VulkanClient::~VulkanClient()
{
	stubParticleStreams.clear();
	for (SURFHANDLE h : managedStubSurfaces)
		delete reinterpret_cast<VkPhase0SurfaceStub *>(h);
	managedStubSurfaces.clear();
	vtab.reset();
}

bool VulkanClient::clbkInitialise()
{
	VkLogAlw("clbkInitialise (Orbiter build %d)", oapiGetOrbiterVersion());

	if (!VkEnsureVolkInitialized())
		return false;

	if (!GraphicsClient::clbkInitialise())
		return false;

	if (g_vkConfig)
		g_vkConfig->SyncFromCore(this);

	if (LaunchpadVideoTab()) {
		vtab = std::make_unique<VideoTab>(this, ModuleInstance(), OrbiterInstance(), LaunchpadVideoTab());
		return vtab->Initialise();
	}

	return true;
}

void VulkanClient::clbkRefreshVideoData()
{
	if (vtab)
		vtab->UpdateConfigData();
}

INT_PTR VulkanClient::LaunchpadVideoWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (vtab)
		return vtab->WndProc(hWnd, uMsg, wParam, lParam);
	return FALSE;
}

bool VulkanClient::clbkFullscreenMode() const
{
	return bFullscreen;
}

void VulkanClient::clbkGetViewportSize(DWORD *width, DWORD *height) const
{
	if (width)  *width  = viewW;
	if (height) *height = viewH;
}

bool VulkanClient::clbkGetRenderParam(DWORD prm, DWORD *value) const
{
	if (!value)
		return false;

	switch (prm) {
	case RP_COLOURDEPTH:
		*value = 32;
		return true;
	case RP_ZBUFFERDEPTH:
		*value = 32;
		return true;
	case RP_STENCILDEPTH:
		*value = 8;
		return true;
	case RP_MAXLIGHTS:
		*value = 8;
		return true;
	case RP_REQUIRETEXPOW2:
		*value = 0;
		return true;
	case RP_TERRAINFLATS:
		*value = (g_vkConfig && g_vkConfig->bFlats) ? 1 : 0;
		return true;
	default:
		return false;
	}
}

HWND VulkanClient::clbkCreateRenderWindow()
{
	// STUB (Unit 0.2): Unit 0.2 initializes VkContext and VkSwapchainKHR on this window
	if (g_vkConfig)
		g_vkConfig->WriteParams();

	VIDEODATA *data = GetVideoData();
	bFullscreen = data->fullscreen;
	viewW = (data->winw >= 320) ? (DWORD)data->winw : 1280;
	viewH = (data->winh >= 200) ? (DWORD)data->winh : 720;

	HWND hWnd = GraphicsClient::clbkCreateRenderWindow();
	RECT rc{};
	if (hWnd && GetClientRect(hWnd, &rc)) {
		if (rc.right > rc.left) viewW = (DWORD)(rc.right - rc.left);
		if (rc.bottom > rc.top) viewH = (DWORD)(rc.bottom - rc.top);
	}
	return hWnd;
}

void VulkanClient::clbkPostCreation()
{
	// STUB (Phase 2): Post-creation scene initialization
}

void VulkanClient::clbkCloseSession(bool fastclose)
{
	// STUB (Unit 0.2 / Phase 1): Release session stubs
	(void)fastclose;
	stubParticleStreams.clear();
	for (SURFHANDLE h : managedStubSurfaces)
		delete reinterpret_cast<VkPhase0SurfaceStub *>(h);
	managedStubSurfaces.clear();
}

void VulkanClient::clbkDestroyRenderWindow(bool fastclose)
{
	// STUB (Unit 0.2): Swapchain and context teardown
	GraphicsClient::clbkDestroyRenderWindow(fastclose);
}

void VulkanClient::clbkBeginFrame()
{
	// STUB (Unit 0.2): Frame bracket begin
}

void VulkanClient::clbkRenderScene()
{
	// STUB (Unit 0.2 / Phase 2): Scene render pass
	Render2DOverlay();
}

void VulkanClient::clbkEndFrame()
{
	// STUB (Unit 0.2 / Unit 0.3): Frame bracket end
}

bool VulkanClient::clbkDisplayFrame()
{
	// STUB (Unit 0.2): Swapchain presentation
	return true;
}

void VulkanClient::clbkImGuiInit()
{
	// STUB (Unit 0.3): Initialize dummy font atlas texture to prevent Core assert
	ImGuiIO &io = ImGui::GetIO();
	io.ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;
	unsigned char *pixels = nullptr;
	int w = 0, h = 0;
	io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
	io.Fonts->SetTexID((ImTextureID)1);
}

void VulkanClient::clbkImGuiShutdown()
{
	// STUB (Unit 0.3): ImGui backend shutdown
}

void VulkanClient::clbkImGuiNewFrame()
{
	// STUB (Unit 0.3): ImGui frame start
}

void VulkanClient::clbkImGuiRenderDrawData()
{
	// STUB (Unit 0.3): Finalize ImGui draw lists
	ImGui::Render();
}

uint64_t VulkanClient::clbkImGuiSurfaceTexture(SURFHANDLE surf)
{
	// STUB (Unit 0.3 / Phase 1): Return descriptor set for VkSurfNative
	(void)surf;
	return 1;
}

void *VulkanClient::clbkGetExtendedInterface(const char *name)
{
	// STUB (Phase 5): Extended interface query
	(void)name;
	return nullptr;
}

// ==============================================================
// Phase 0 Surface / 2D / Particle Placeholders
// ==============================================================

SURFHANDLE VulkanClient::clbkLoadTexture(const char *fname, DWORD flags)
{
	// STUB (Phase 1): Texture loader
	(void)fname;
	auto *s = new VkPhase0SurfaceStub{ 256, 256, 1, (flags & 0x8) != 0 };
	if (s->managed)
		managedStubSurfaces.push_back(reinterpret_cast<SURFHANDLE>(s));
	return reinterpret_cast<SURFHANDLE>(s);
}

SURFHANDLE VulkanClient::clbkLoadSurface(const char *fname, DWORD attrib, bool bPath)
{
	// STUB (Phase 1): Surface loader
	(void)fname;
	(void)attrib;
	(void)bPath;
	auto *s = new VkPhase0SurfaceStub{ 256, 256, 1, false };
	return reinterpret_cast<SURFHANDLE>(s);
}

void VulkanClient::clbkReleaseTexture(SURFHANDLE hTex)
{
	// STUB (Phase 1)
	clbkReleaseSurface(hTex);
}

SURFHANDLE VulkanClient::clbkCreateSurfaceEx(DWORD w, DWORD h, DWORD attrib)
{
	// STUB (Phase 1): Surface allocator
	(void)attrib;
	auto *s = new VkPhase0SurfaceStub{ w ? w : 1, h ? h : 1, 1, false };
	return reinterpret_cast<SURFHANDLE>(s);
}

SURFHANDLE VulkanClient::clbkCreateSurface(DWORD w, DWORD h, SURFHANDLE hTemplate)
{
	// STUB (Phase 1)
	(void)hTemplate;
	return clbkCreateSurfaceEx(w, h, 0);
}

SURFHANDLE VulkanClient::clbkCreateTexture(DWORD w, DWORD h)
{
	// STUB (Phase 1)
	return clbkCreateSurfaceEx(w, h, 0);
}

void VulkanClient::clbkIncrSurfaceRef(SURFHANDLE surf)
{
	// STUB (Phase 1)
	if (!surf)
		return;
	auto *s = reinterpret_cast<VkPhase0SurfaceStub *>(surf);
	++s->refCount;
}

bool VulkanClient::clbkReleaseSurface(SURFHANDLE surf)
{
	// STUB (Phase 1)
	if (!surf)
		return false;
	auto *s = reinterpret_cast<VkPhase0SurfaceStub *>(surf);
	if (s->managed)
		return true;
	if (--s->refCount <= 0)
		delete s;
	return true;
}

bool VulkanClient::clbkGetSurfaceSize(SURFHANDLE surf, DWORD *w, DWORD *h)
{
	// STUB (Phase 1)
	if (!surf) {
		if (w) *w = viewW;
		if (h) *h = viewH;
		return true;
	}
	const auto *s = reinterpret_cast<const VkPhase0SurfaceStub *>(surf);
	if (w) *w = s->w;
	if (h) *h = s->h;
	return true;
}

bool VulkanClient::clbkCopyBitmap(SURFHANDLE pdds, HBITMAP hbm, int x, int y, int dx, int dy)
{
	// STUB (Phase 1)
	(void)pdds;
	(void)hbm;
	(void)x;
	(void)y;
	(void)dx;
	(void)dy;
	return true;
}

Sketchpad *VulkanClient::clbkGetSketchpad(SURFHANDLE surf)
{
	// STUB (Phase 1)
	return new VkPhase0SketchpadStub(surf, viewW, viewH);
}

void VulkanClient::clbkReleaseSketchpad(Sketchpad *sp)
{
	// STUB (Phase 1)
	delete sp;
}

Font *VulkanClient::clbkCreateFont(int height, bool prop, const char *face, FontStyle style, int orientation) const
{
	// STUB (Phase 1)
	return new VkPhase0FontStub(height, prop, face ? face : "Sans", style, orientation);
}

Font *VulkanClient::clbkCreateFontEx(int height, char *face, int width, int weight, FontStyle style, float spacing) const
{
	// STUB (Phase 1)
	(void)width;
	(void)weight;
	(void)spacing;
	return new VkPhase0FontStub(height, true, face ? face : "Sans", style, 0);
}

void VulkanClient::clbkReleaseFont(Font *font) const
{
	// STUB (Phase 1)
	delete font;
}

Pen *VulkanClient::clbkCreatePen(int style, int width, DWORD col) const
{
	// STUB (Phase 1)
	return new VkPhase0PenStub(style, width, col);
}

void VulkanClient::clbkReleasePen(Pen *pen) const
{
	// STUB (Phase 1)
	delete pen;
}

Brush *VulkanClient::clbkCreateBrush(DWORD col) const
{
	// STUB (Phase 1)
	return new VkPhase0BrushStub(col);
}

void VulkanClient::clbkReleaseBrush(Brush *brush) const
{
	// STUB (Phase 1)
	delete brush;
}

ParticleStream *VulkanClient::clbkCreateParticleStream(PARTICLESTREAMSPEC *pss)
{
	// STUB (Phase 4): Particle stream placeholder
	auto ps = std::make_unique<ParticleStream>(this, pss);
	ParticleStream *raw = ps.get();
	stubParticleStreams.push_back(std::move(ps));
	return raw;
}

ParticleStream *VulkanClient::clbkCreateExhaustStream(PARTICLESTREAMSPEC *pss, OBJHANDLE hVessel, const double *lvl, const VECTOR3 *ref, const VECTOR3 *dir)
{
	// STUB (Phase 4): Exhaust stream placeholder
	ParticleStream *ps = clbkCreateParticleStream(pss);
	ps->Attach(hVessel, ref, dir, lvl);
	return ps;
}

ParticleStream *VulkanClient::clbkCreateExhaustStream(PARTICLESTREAMSPEC *pss, OBJHANDLE hVessel, const double *lvl, const VECTOR3 &ref, const VECTOR3 &dir)
{
	// STUB (Phase 4): Exhaust stream placeholder
	ParticleStream *ps = clbkCreateParticleStream(pss);
	ps->Attach(hVessel, ref, dir, lvl);
	return ps;
}

ParticleStream *VulkanClient::clbkCreateReentryStream(PARTICLESTREAMSPEC *pss, OBJHANDLE hVessel)
{
	// STUB (Phase 4): Reentry stream placeholder
	ParticleStream *ps = clbkCreateParticleStream(pss);
	ps->Attach(hVessel, _V(0, 0, 0), _V(0, 0, 1), nullptr);
	return ps;
}
