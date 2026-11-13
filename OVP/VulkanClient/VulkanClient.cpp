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
#include "VkContext.h"
#include "VkSwapchain.h"
#include "VkBlit.h"
#include "VkSurface.h"
#include "VkLog.h"
#include "imgui.h"
#include "imgui_impl_vulkan.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>

using namespace oapi;

namespace {
HINSTANCE g_hInst = nullptr;
VulkanClient *g_vkClient = nullptr;

std::string NormalizeTexKey(const char *fname)
{
	if (!fname)
		return {};
	std::string key(fname);
	for (char &ch : key) {
		if (ch == '/')
			ch = '\\';
		else
			ch = (char)std::tolower((unsigned char)ch);
	}
	return key;
}

// STUB (Unit 1.3): Minimal font handle replaced by VkPadFont in Unit 1.3
class VkPhase0FontStub : public oapi::Font {
public:
	VkPhase0FontStub(int height, bool prop, const char *face, FontStyle style, int orientation)
		: oapi::Font(height, prop, face, style, orientation)
		, cellH(height != 0 ? (height > 0 ? height : -height) : 14)
	{}
	int cellH;
};

// STUB (Unit 1.3): Minimal pen handle replaced by VkPadPen in Unit 1.3
class VkPhase0PenStub : public oapi::Pen {
public:
	VkPhase0PenStub(int style, int width, DWORD col) : oapi::Pen(style, width, col) {}
};

// STUB (Unit 1.3): Minimal brush handle replaced by VkPadBrush in Unit 1.3
class VkPhase0BrushStub : public oapi::Brush {
public:
	explicit VkPhase0BrushStub(DWORD col) : oapi::Brush(col) {}
};

// STUB (Unit 1.3): Safe no-op Sketchpad replaced by VkPad in Unit 1.3.
// Base oapi::Sketchpad methods contain assert(false) and vessels dereference
// Sketchpad* during panel/MFD setup, so all virtual methods are safely stubbed.
class VkPhase0SketchpadStub : public oapi::Sketchpad {
public:
	VkPhase0SketchpadStub(SURFHANDLE s, DWORD defW, DWORD defH)
		: oapi::Sketchpad(s), surfW(defW), surfH(defH)
	{
		if (s && g_vkClient) {
			const auto *st = reinterpret_cast<const VkSurfNative *>(s);
			if (g_vkClient->IsValidSurface(st)) {
				surfW = st->GetWidth();
				surfH = st->GetHeight();
			}
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

// Restyle Orbiter's 10x10 fullscreen dummy popup into a borderless window.
// Exclusive fullscreen is never used, so styles 0 and 1 are identical here.
// style 2 = work area (keeps the taskbar), pageflip = span all monitors.
void ApplyBorderlessWindow(HWND hWnd, const oapi::GraphicsClient::VIDEODATA *data)
{
	RECT rc{ 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
	if (data->style == 2)
		SystemParametersInfoA(SPI_GETWORKAREA, 0, &rc, 0);
	if (data->pageflip) {
		rc.left  = GetSystemMetrics(SM_XVIRTUALSCREEN);
		rc.right = rc.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
	}

	SetWindowLongPtrA(hWnd, GWL_STYLE, WS_POPUP | WS_CLIPCHILDREN | WS_VISIBLE);
	SetWindowPos(hWnd, HWND_TOP, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
		SWP_SHOWWINDOW | SWP_FRAMECHANGED);
}

// Win32 surface hook required by ImGui_ImplVulkan when multi-viewports are enabled
// (imgui_impl_win32.cpp lives in Orbiter.exe without Vulkan headers).
int ImGuiWin32CreateVkSurface(ImGuiViewport *viewport, ImU64 vkInstance, const void *vkAllocator, ImU64 *outVkSurface)
{
	VkWin32SurfaceCreateInfoKHR createInfo{};
	createInfo.sType     = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
	createInfo.hwnd      = static_cast<HWND>(viewport->PlatformHandleRaw ? viewport->PlatformHandleRaw : viewport->PlatformHandle);
	createInfo.hinstance = ::GetModuleHandleA(nullptr);
	return static_cast<int>(vkCreateWin32SurfaceKHR(
		reinterpret_cast<VkInstance>(static_cast<uintptr_t>(vkInstance)),
		&createInfo,
		static_cast<const VkAllocationCallbacks *>(vkAllocator),
		reinterpret_cast<VkSurfaceKHR *>(outVkSurface)));
}

void CheckImGuiVkResult(VkResult err)
{
	if (err != VK_SUCCESS)
		VkLogErr("ImGui Vulkan error: VkResult = %d", (int)err);
}
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
	if (imguiInitialized)
		clbkImGuiShutdown();
	DestroyAllSurfaces();
	blitter.reset();
	swapchain.reset();
	ctx.reset();
	stubParticleStreams.clear();
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
	VkLogAlw("================ clbkCreateRenderWindow ================");

	if (g_vkConfig)
		g_vkConfig->WriteParams();

	VIDEODATA *data = GetVideoData();
	bFullscreen = false;

	HWND hWnd = GraphicsClient::clbkCreateRenderWindow();
	if (!hWnd) {
		VkLogErr("Render window creation failed");
		return NULL;
	}
	renderWnd = hWnd;
	bltGroupTgt = RENDERTGT_NONE;
	SetWindowTextA(hWnd, "[VulkanClient]");

	if (data->fullscreen)
		ApplyBorderlessWindow(hWnd, data);
	UpdateViewportSize(hWnd);

	RECT rcClient{};
	if (GetClientRect(hWnd, &rcClient)) {
		HDC hdcWnd = ::GetDC(hWnd);
		if (hdcWnd) {
			HBRUSH hBlack = (HBRUSH)GetStockObject(BLACK_BRUSH);
			FillRect(hdcWnd, &rcClient, hBlack);
			::ReleaseDC(hWnd, hdcWnd);
		}
		ValidateRect(hWnd, nullptr);
	}

	VkLogInfo("Render window %ux%u (%s)", viewW, viewH, data->fullscreen ? "borderless" : "windowed");

	ctx = std::make_unique<VkContext>();
	const bool validation = g_vkConfig && g_vkConfig->ValidationLayers;
	if (!ctx->Create(hWnd, data->deviceidx, validation)) {
		ctx.reset();
		VkLogErr("Vulkan initialisation failed; rendering is disabled for this session");
		MessageBoxA(hWnd,
			"Failed to initialise Vulkan 1.3.\nSee VulkanClient.log for details.",
			"VulkanClient Error", MB_OK | MB_ICONERROR);
		return hWnd;
	}

	swapchain = std::make_unique<VkSwapchain>();
	if (!swapchain->Create(*ctx, viewW, viewH, data->novsync)) {
		swapchain.reset();
		ctx.reset();
		VkLogErr("Swapchain initialisation failed; rendering is disabled for this session");
		MessageBoxA(hWnd,
			"Failed to create Vulkan swapchain.\nSee VulkanClient.log for details.",
			"VulkanClient Error", MB_OK | MB_ICONERROR);
		return hWnd;
	}

	blitter = std::make_unique<VkBlitPipeline>();
	if (!blitter->Create(*ctx)) {
		blitter.reset();
		VkLogWarn("VkBlitPipeline creation failed; scaled/color-keyed blits are disabled");
	}

	viewW = swapchain->Extent().width;
	viewH = swapchain->Extent().height;

	RenderSplashScreen(hWnd);

	return hWnd;
}

void VulkanClient::clbkSetSplashScreen(const char *fname, DWORD textCol)
{
	customSplashPath = fname ? fname : "";
	customSplashCol  = textCol;
}

void VulkanClient::RenderSplashScreen(HWND hWnd)
{
	if (!swapchain || !swapchain->IsValid() || viewW == 0 || viewH == 0)
		return;

	swapchain->FillScene(nullptr, 0);

	SURFHANDLE splashSurf = nullptr;
	DWORD imgW = 0, imgH = 0;

	if (!customSplashPath.empty()) {
		splashSurf = clbkLoadSurface(customSplashPath.c_str(), OAPISURFACE_UNCOMPRESS | OAPISURFACE_RENDERTARGET | OAPISURFACE_TEXTURE, true);
		if (splashSurf)
			clbkGetSurfaceSize(splashSurf, &imgW, &imgH);
	}

	if (!splashSurf) {
		HMODULE hOrbiter = GetModuleHandleA(nullptr);
		HRSRC hRes = FindResourceA(hOrbiter, MAKEINTRESOURCEA(292), "IMAGE");
		if (hRes) {
			HGLOBAL hGlob = LoadResource(hOrbiter, hRes);
			auto *pData = static_cast<BYTE *>(LockResource(hGlob));
			DWORD nSize = SizeofResource(hOrbiter, hRes);
			if (pData && nSize > 0) {
				const double scale = std::min((double)viewW / 1920.0, (double)viewH / 1200.0);
				const DWORD bmw = std::max<DWORD>(1, (DWORD)(1920.0 * scale + 0.5));
				const DWORD bmh = std::max<DWORD>(1, (DWORD)(1200.0 * scale + 0.5));
				HBITMAP hbm = ReadImageFromMemory(pData, nSize, bmw, bmh);
				if (hbm) {
					splashSurf = clbkCreateSurface(hbm);
					DeleteObject(hbm);
					if (splashSurf)
						clbkGetSurfaceSize(splashSurf, &imgW, &imgH);
				}
			}
		}
	}

	if (splashSurf && imgW > 0 && imgH > 0) {
		HDC hDC = clbkGetSurfaceDC(splashSurf);
		if (hDC) {
			LOGFONTA lf{};
			lf.lfHeight        = 18;
			lf.lfWeight        = FW_BOLD;
			lf.lfCharSet       = ANSI_CHARSET;
			lf.lfOutPrecision  = OUT_DEFAULT_PRECIS;
			lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
			lf.lfQuality       = ANTIALIASED_QUALITY;
			strcpy_s(lf.lfFaceName, "Courier New");

			HFONT hFont = CreateFontIndirectA(&lf);
			HGDIOBJ hOld = hFont ? SelectObject(hDC, hFont) : nullptr;
			SetTextColor(hDC, (COLORREF)customSplashCol);
			SetBkMode(hDC, TRANSPARENT);

			static const char *const kMonths[] = {
				"???", "Jan", "Feb", "Mar", "Apr", "May", "Jun",
				"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
			};
			DWORD ver = oapiGetOrbiterVersion();
			const DWORD yr = ver / 10000;
			ver -= yr * 10000;
			DWORD mo = ver / 100;
			const DWORD dy = ver - mo * 100;
			if (mo > 12)
				mo = 0;

			char buildLine[128]{};
			std::snprintf(buildLine, sizeof(buildLine), "Build %s %lu 20%lu [%u]",
				kMonths[mo], (unsigned long)dy, (unsigned long)yr, oapiGetOrbiterVersion());

#ifdef _DEBUG
			const char *clientLine = "VulkanClient (Vulkan 1.3 Debug Build)";
#else
			const char *clientLine = "VulkanClient (Vulkan 1.3 Release Build)";
#endif

			const int xc = (int)(imgW * 750u / 1280u);
			const int yc = (int)(imgH * 545u / 800u);
			TextOutA(hDC, xc, yc + 0 * 20, "ORBITER Space Flight Simulator", 30);
			TextOutA(hDC, xc, yc + 1 * 20, buildLine, (int)std::strlen(buildLine));
			TextOutA(hDC, xc, yc + 2 * 20, clientLine, (int)std::strlen(clientLine));

			if (hOld)
				SelectObject(hDC, hOld);
			if (hFont)
				DeleteObject(hFont);
			clbkReleaseSurfaceDC(splashSurf, hDC);
		}

		const double scale = std::min((double)viewW / (double)imgW, (double)viewH / (double)imgH);
		const DWORD tgtW = std::max<DWORD>(1, (DWORD)(imgW * scale + 0.5));
		const DWORD tgtH = std::max<DWORD>(1, (DWORD)(imgH * scale + 0.5));
		const DWORD tgtX = (viewW > tgtW) ? (viewW - tgtW) / 2 : 0;
		const DWORD tgtY = (viewH > tgtH) ? (viewH - tgtH) / 2 : 0;

		clbkScaleBlt(RENDERTGT_MAINWINDOW, tgtX, tgtY, tgtW, tgtH, splashSurf, 0, 0, imgW, imgH, 0);
		swapchain->FlushOffscreenCommands();
		clbkReleaseSurface(splashSurf);
	}

	swapchain->FlushOffscreenCommands();
	swapchain->Present(hWnd);
}

void VulkanClient::UpdateViewportSize(HWND hWnd)
{
	RECT rc{};
	if (hWnd && GetClientRect(hWnd, &rc)) {
		if (rc.right > rc.left) viewW = (DWORD)(rc.right - rc.left);
		if (rc.bottom > rc.top) viewH = (DWORD)(rc.bottom - rc.top);
	}
}

void VulkanClient::clbkPostCreation()
{
	// STUB (Phase 2): Post-creation scene initialization
}

void VulkanClient::clbkCloseSession(bool fastclose)
{
	(void)fastclose;
	stubParticleStreams.clear();
}

void VulkanClient::ReclaimRetiredSurfaces()
{
	if (retiredSurfaces.empty())
		return;

	constexpr uint64_t kRetireDelay = VkSwapchain::kMaxFramesInFlight + 1;
	size_t dst = 0;
	for (size_t i = 0; i < retiredSurfaces.size(); ++i) {
		if (frameCounter >= retiredSurfaces[i].retireFrame + kRetireDelay)
			delete retiredSurfaces[i].surf;
		else
			retiredSurfaces[dst++] = retiredSurfaces[i];
	}
	retiredSurfaces.resize(dst);
}

void VulkanClient::DestroyAllSurfaces()
{
	if (ctx && ctx->Device())
		vkDeviceWaitIdle(ctx->Device());

	sharedTextures.clear();
	for (const RetiredSurface &entry : retiredSurfaces)
		delete entry.surf;
	retiredSurfaces.clear();
	for (VkSurfNative *s : surfaceCatalog)
		delete s;
	surfaceCatalog.clear();
	frameCounter = 0;
}

void VulkanClient::clbkDestroyRenderWindow(bool fastclose)
{
	VkLogAlw("================ clbkDestroyRenderWindow ================");
	if (imguiInitialized)
		clbkImGuiShutdown();
	DestroyAllSurfaces();
	blitter.reset();
	GraphicsClient::clbkDestroyRenderWindow(fastclose);
	swapchain.reset();
	ctx.reset();
	renderWnd = nullptr;
	bltGroupTgt = RENDERTGT_NONE;
	customSplashPath.clear();
	customSplashCol = 0xA06060;
}

void VulkanClient::clbkBeginFrame()
{
	imguiFrameRendered = false;
	if (!swapchain)
		return;
	HWND hWnd = GetRenderWindow() ? GetRenderWindow() : renderWnd;
	if (swapchain->BeginFrame(hWnd)) {
		viewW = swapchain->Extent().width;
		viewH = swapchain->Extent().height;
		++frameCounter;
		ReclaimRetiredSurfaces();
	}
}

void VulkanClient::clbkRenderScene()
{
	// STUB (Phase 2): Replace solid clear with 3D scene pass
	if (swapchain && swapchain->IsFrameRecording()) {
		const VkClearColorValue clearCol = { { 1.0f, 0.0f, 1.0f, 1.0f } };
		swapchain->ClearScene(clearCol);
	}
	Render2DOverlay();
}

void VulkanClient::clbkEndFrame()
{
	if (swapchain)
		swapchain->EndFrame();

	if (imguiInitialized && imguiFrameRendered) {
		imguiFrameRendered = false;
		if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();
		}
	}
}

bool VulkanClient::clbkDisplayFrame()
{
	if (!swapchain)
		return false;
	HWND hWnd = GetRenderWindow() ? GetRenderWindow() : renderWnd;
	return swapchain->Present(hWnd);
}

void VulkanClient::clbkImGuiInit()
{
	if (!ctx || !swapchain || !swapchain->IsValid()) {
		ImGuiIO &io = ImGui::GetIO();
		io.ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;
		unsigned char *pixels = nullptr;
		int w = 0, h = 0;
		io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
		io.Fonts->SetTexID((ImTextureID)1);
		return;
	}

	ImGui::GetPlatformIO().Platform_CreateVkSurface = ImGuiWin32CreateVkSurface;

	imguiMainFormat     = swapchain->SceneFormat();
	imguiViewportFormat = swapchain->SwapchainFormat();

	ImGui_ImplVulkan_InitInfo initInfo{};
	initInfo.ApiVersion         = VK_API_VERSION_1_3;
	initInfo.Instance           = ctx->Instance();
	initInfo.PhysicalDevice     = ctx->PhysicalDevice();
	initInfo.Device             = ctx->Device();
	initInfo.QueueFamily        = ctx->GraphicsQueueFamily();
	initInfo.Queue              = ctx->GraphicsQueue();
	initInfo.DescriptorPoolSize = 1024;
	initInfo.MinImageCount      = VkSwapchain::kMaxFramesInFlight;
	initInfo.ImageCount         = std::max<uint32_t>(swapchain->ImageCount(), VkSwapchain::kMaxFramesInFlight);
	initInfo.UseDynamicRendering = true;
	initInfo.CheckVkResultFn    = CheckImGuiVkResult;

	initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
	initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount    = 1;
	initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &imguiMainFormat;

	initInfo.PipelineInfoForViewports.MSAASamples         = VK_SAMPLE_COUNT_1_BIT;
	initInfo.PipelineInfoForViewports.SwapChainImageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	initInfo.PipelineInfoForViewports.PipelineRenderingCreateInfo.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	initInfo.PipelineInfoForViewports.PipelineRenderingCreateInfo.colorAttachmentCount    = 1;
	initInfo.PipelineInfoForViewports.PipelineRenderingCreateInfo.pColorAttachmentFormats = &imguiViewportFormat;

	if (!ImGui_ImplVulkan_Init(&initInfo)) {
		VkLogErr("ImGui_ImplVulkan_Init failed");
		return;
	}

	imguiInitialized   = true;
	imguiFrameRendered = false;

	if (!CreateImGuiPlaceholderTexture())
		VkLogWarn("Failed to create ImGui placeholder surface texture");

	VkLogInfo("ImGui Vulkan 1.3 backend initialised (dynamic rendering, viewports=%d)",
		(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) ? 1 : 0);
}

void VulkanClient::clbkImGuiShutdown()
{
	if (!imguiInitialized)
		return;

	if (ctx && ctx->Device())
		vkDeviceWaitIdle(ctx->Device());

	for (VkSurfNative *s : surfaceCatalog)
		s->ReleaseImGuiDescriptorSet();
	for (const RetiredSurface &entry : retiredSurfaces)
		entry.surf->ReleaseImGuiDescriptorSet();

	DestroyImGuiPlaceholderTexture();
	ImGui_ImplVulkan_Shutdown();

	imguiInitialized   = false;
	imguiFrameRendered = false;
	VkLogInfo("ImGui Vulkan 1.3 backend shut down");
}

void VulkanClient::clbkImGuiNewFrame()
{
	if (imguiInitialized)
		ImGui_ImplVulkan_NewFrame();
}

void VulkanClient::clbkImGuiRenderDrawData()
{
	ImGui::Render();
	if (!imguiInitialized)
		return;

	imguiFrameRendered = true;
	if (swapchain && swapchain->IsFrameRecording()) {
		ImDrawData *drawData = ImGui::GetDrawData();
		if (drawData) {
			if (drawData->Textures != nullptr) {
				for (ImTextureData *tex : *drawData->Textures) {
					if (tex->Status != ImTextureStatus_OK)
						ImGui_ImplVulkan_UpdateTexture(tex);
				}
			}
			if (swapchain->BeginSceneOverlayPass()) {
				ImGui_ImplVulkan_RenderDrawData(drawData, swapchain->CurrentCommandBuffer());
				swapchain->EndSceneOverlayPass();
			}
		}
	}
}

uint64_t VulkanClient::clbkImGuiSurfaceTexture(SURFHANDLE surf)
{
	if (!surf || !imguiInitialized || imguiPlaceholderSampler == VK_NULL_HANDLE)
		return (uint64_t)imguiPlaceholderDescSet;

	auto *s = reinterpret_cast<VkSurfNative *>(surf);
	if (surfaceCatalog.count(s) == 0)
		return (uint64_t)imguiPlaceholderDescSet;

	VkDescriptorSet ds = s->GetImGuiDescriptorSet(imguiPlaceholderSampler);
	return ds != VK_NULL_HANDLE ? (uint64_t)ds : (uint64_t)imguiPlaceholderDescSet;
}

bool VulkanClient::CreateImGuiPlaceholderTexture()
{
	if (!ctx || !swapchain)
		return false;

	VkImageCreateInfo imgInfo{};
	imgInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imgInfo.imageType     = VK_IMAGE_TYPE_2D;
	imgInfo.format        = VK_FORMAT_R8G8B8A8_UNORM;
	imgInfo.extent.width  = 1;
	imgInfo.extent.height = 1;
	imgInfo.extent.depth  = 1;
	imgInfo.mipLevels     = 1;
	imgInfo.arrayLayers   = 1;
	imgInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
	imgInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
	imgInfo.usage         = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	imgInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
	imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	VmaAllocationCreateInfo allocInfo{};
	allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

	VkResult vr = vmaCreateImage(ctx->Allocator(), &imgInfo, &allocInfo, &imguiPlaceholderImage, &imguiPlaceholderAlloc, nullptr);
	if (vr != VK_SUCCESS) {
		VkLogErr("vmaCreateImage (imguiPlaceholderImage) failed (VkResult = %d)", (int)vr);
		return false;
	}

	VkCommandBuffer cmd = swapchain->OffscreenCommandBuffer();
	if (cmd == VK_NULL_HANDLE) {
		DestroyImGuiPlaceholderTexture();
		return false;
	}

	VkImageMemoryBarrier2 toClear{};
	toClear.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	toClear.srcStageMask        = VK_PIPELINE_STAGE_2_NONE;
	toClear.srcAccessMask       = VK_ACCESS_2_NONE;
	toClear.dstStageMask        = VK_PIPELINE_STAGE_2_CLEAR_BIT;
	toClear.dstAccessMask       = VK_ACCESS_2_TRANSFER_WRITE_BIT;
	toClear.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
	toClear.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	toClear.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toClear.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toClear.image               = imguiPlaceholderImage;
	toClear.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
	toClear.subresourceRange.baseMipLevel   = 0;
	toClear.subresourceRange.levelCount     = 1;
	toClear.subresourceRange.baseArrayLayer = 0;
	toClear.subresourceRange.layerCount     = 1;

	VkDependencyInfo depClear{};
	depClear.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	depClear.imageMemoryBarrierCount = 1;
	depClear.pImageMemoryBarriers    = &toClear;
	vkCmdPipelineBarrier2(cmd, &depClear);

	const VkClearColorValue clearVal = { { 1.0f, 1.0f, 1.0f, 1.0f } };
	VkImageSubresourceRange clearRange{};
	clearRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
	clearRange.baseMipLevel   = 0;
	clearRange.levelCount     = 1;
	clearRange.baseArrayLayer = 0;
	clearRange.layerCount     = 1;
	vkCmdClearColorImage(cmd, imguiPlaceholderImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearVal, 1, &clearRange);

	VkImageMemoryBarrier2 toSample{};
	toSample.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	toSample.srcStageMask        = VK_PIPELINE_STAGE_2_CLEAR_BIT;
	toSample.srcAccessMask       = VK_ACCESS_2_TRANSFER_WRITE_BIT;
	toSample.dstStageMask        = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
	toSample.dstAccessMask       = VK_ACCESS_2_SHADER_READ_BIT;
	toSample.oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	toSample.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	toSample.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toSample.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toSample.image               = imguiPlaceholderImage;
	toSample.subresourceRange    = clearRange;

	VkDependencyInfo depSample{};
	depSample.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	depSample.imageMemoryBarrierCount = 1;
	depSample.pImageMemoryBarriers    = &toSample;
	vkCmdPipelineBarrier2(cmd, &depSample);

	if (!swapchain->FlushOffscreenCommands()) {
		DestroyImGuiPlaceholderTexture();
		return false;
	}

	VkImageViewCreateInfo viewInfo{};
	viewInfo.sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image      = imguiPlaceholderImage;
	viewInfo.viewType   = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format     = VK_FORMAT_R8G8B8A8_UNORM;
	viewInfo.subresourceRange = clearRange;

	vr = vkCreateImageView(ctx->Device(), &viewInfo, nullptr, &imguiPlaceholderView);
	if (vr != VK_SUCCESS) {
		VkLogErr("vkCreateImageView (imguiPlaceholderView) failed (VkResult = %d)", (int)vr);
		DestroyImGuiPlaceholderTexture();
		return false;
	}

	VkSamplerCreateInfo sampInfo{};
	sampInfo.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	sampInfo.magFilter    = VK_FILTER_LINEAR;
	sampInfo.minFilter    = VK_FILTER_LINEAR;
	sampInfo.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	sampInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampInfo.maxLod       = 1.0f;

	vr = vkCreateSampler(ctx->Device(), &sampInfo, nullptr, &imguiPlaceholderSampler);
	if (vr != VK_SUCCESS) {
		VkLogErr("vkCreateSampler (imguiPlaceholderSampler) failed (VkResult = %d)", (int)vr);
		DestroyImGuiPlaceholderTexture();
		return false;
	}

	imguiPlaceholderDescSet = ImGui_ImplVulkan_AddTexture(
		imguiPlaceholderSampler,
		imguiPlaceholderView,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	return imguiPlaceholderDescSet != VK_NULL_HANDLE;
}

void VulkanClient::DestroyImGuiPlaceholderTexture()
{
	if (imguiPlaceholderDescSet != VK_NULL_HANDLE) {
		ImGui_ImplVulkan_RemoveTexture(imguiPlaceholderDescSet);
		imguiPlaceholderDescSet = VK_NULL_HANDLE;
	}
	if (ctx && ctx->Device()) {
		if (imguiPlaceholderSampler != VK_NULL_HANDLE) {
			vkDestroySampler(ctx->Device(), imguiPlaceholderSampler, nullptr);
			imguiPlaceholderSampler = VK_NULL_HANDLE;
		}
		if (imguiPlaceholderView != VK_NULL_HANDLE) {
			vkDestroyImageView(ctx->Device(), imguiPlaceholderView, nullptr);
			imguiPlaceholderView = VK_NULL_HANDLE;
		}
		if (imguiPlaceholderImage != VK_NULL_HANDLE) {
			vmaDestroyImage(ctx->Allocator(), imguiPlaceholderImage, imguiPlaceholderAlloc);
			imguiPlaceholderImage = VK_NULL_HANDLE;
			imguiPlaceholderAlloc = VK_NULL_HANDLE;
		}
	}
}

void *VulkanClient::clbkGetExtendedInterface(const char *name)
{
	// STUB (Phase 5): Extended interface query
	(void)name;
	return nullptr;
}

// ==============================================================
// Surface & Texture Management (Unit 1.1)
// ==============================================================

SURFHANDLE VulkanClient::clbkLoadTexture(const char *fname, DWORD flags)
{
	DWORD attrib = OAPISURFACE_TEXTURE;
	if (flags & 0x1) attrib |= OAPISURFACE_SYSMEM;
	if (flags & 0x2) attrib |= OAPISURFACE_UNCOMPRESS | OAPISURFACE_RENDERTARGET;
	if (flags & 0x4) attrib |= OAPISURFACE_NOMIPMAPS;
	if (flags & 0x8) attrib |= OAPISURFACE_SHARED;

	return clbkLoadSurface(fname, attrib, false);
}

SURFHANDLE VulkanClient::clbkLoadSurface(const char *fname, DWORD attrib, bool bPath)
{
	if (!ctx || !swapchain || !fname || !fname[0])
		return nullptr;

	constexpr DWORD kNonShareableMask = OAPISURFACE_RENDERTARGET | OAPISURFACE_GDI | OAPISURFACE_SYSMEM;
	const bool shareable = ((attrib & kNonShareableMask) == 0) && ((attrib & OAPISURFACE_SHARED) != 0);

	std::string key;
	if (shareable) {
		key = NormalizeTexKey(fname);
		auto it = sharedTextures.find(key);
		if (it != sharedTextures.end())
			return reinterpret_cast<SURFHANDLE>(it->second);
	}

	VkSurfNative *surf = VkSurfNative::LoadFromFile(*this, *ctx, *swapchain, fname, attrib, bPath);
	if (!surf)
		return nullptr;

	surfaceCatalog.insert(surf);
	if (shareable) {
		surf->SetManaged(true);
		sharedTextures[key] = surf;
	}
	return reinterpret_cast<SURFHANDLE>(surf);
}

void VulkanClient::clbkReleaseTexture(SURFHANDLE hTex)
{
	clbkReleaseSurface(hTex);
}

SURFHANDLE VulkanClient::clbkCreateSurfaceEx(DWORD w, DWORD h, DWORD attrib)
{
	if (!ctx || !swapchain || w == 0 || h == 0)
		return nullptr;

	VkSurfNative *surf = VkSurfNative::Create(*ctx, *swapchain, w, h, attrib);
	if (!surf)
		return nullptr;

	surf->SetName("clbkCreateSurfaceEx");
	surfaceCatalog.insert(surf);
	return reinterpret_cast<SURFHANDLE>(surf);
}

SURFHANDLE VulkanClient::clbkCreateSurface(DWORD w, DWORD h, SURFHANDLE hTemplate)
{
	if (!ctx || !swapchain || w == 0 || h == 0)
		return nullptr;

	DWORD attrib = OAPISURFACE_PF_XRGB | OAPISURFACE_RENDERTARGET | OAPISURFACE_TEXTURE;
	if (hTemplate) {
		auto *tmpl = reinterpret_cast<VkSurfNative *>(hTemplate);
		if (surfaceCatalog.count(tmpl))
			attrib = tmpl->GetOAPIFlags();
	}

	VkSurfNative *surf = VkSurfNative::Create(*ctx, *swapchain, w, h, attrib);
	if (!surf)
		return nullptr;

	surf->SetName("clbkCreateSurface");
	surfaceCatalog.insert(surf);
	return reinterpret_cast<SURFHANDLE>(surf);
}

SURFHANDLE VulkanClient::clbkCreateSurface(HBITMAP hBmp)
{
	if (!ctx || !swapchain || !hBmp)
		return nullptr;

	SURFHANDLE hSurf = GraphicsClient::clbkCreateSurface(hBmp);
	if (hSurf) {
		auto *s = reinterpret_cast<VkSurfNative *>(hSurf);
		if (surfaceCatalog.count(s))
			s->SetName("clbkCreateSurface_HBITMAP");
	}
	return hSurf;
}

SURFHANDLE VulkanClient::clbkCreateTexture(DWORD w, DWORD h)
{
	if (!ctx || !swapchain || w == 0 || h == 0)
		return nullptr;

	VkSurfNative *surf = VkSurfNative::Create(
		*ctx,
		*swapchain,
		w,
		h,
		OAPISURFACE_PF_XRGB | OAPISURFACE_RENDERTARGET | OAPISURFACE_TEXTURE);
	if (!surf)
		return nullptr;

	surf->SetName("clbkCreateTexture");
	surfaceCatalog.insert(surf);
	return reinterpret_cast<SURFHANDLE>(surf);
}

void VulkanClient::clbkIncrSurfaceRef(SURFHANDLE surf)
{
	if (!surf)
		return;
	auto *s = reinterpret_cast<VkSurfNative *>(surf);
	if (surfaceCatalog.count(s))
		s->IncRef();
}

bool VulkanClient::clbkReleaseSurface(SURFHANDLE surf)
{
	if (!surf)
		return false;
	auto *s = reinterpret_cast<VkSurfNative *>(surf);
	auto it = surfaceCatalog.find(s);
	if (it == surfaceCatalog.end())
		return false;

	if (s->IsManaged() || (s->GetOAPIFlags() & OAPISURFACE_ORIGIN))
		return false;

	if (s->DecRef()) {
		if (bltGroupTgt == surf)
			bltGroupTgt = RENDERTGT_NONE;
		surfaceCatalog.erase(it);
		retiredSurfaces.push_back({ s, frameCounter });
		return true;
	}
	return false;
}

bool VulkanClient::clbkGetSurfaceSize(SURFHANDLE surf, DWORD *w, DWORD *h)
{
	if (!w || !h)
		return false;
	if (!surf) {
		*w = viewW;
		*h = viewH;
		return true;
	}
	const auto *s = reinterpret_cast<const VkSurfNative *>(surf);
	if (surfaceCatalog.count(const_cast<VkSurfNative *>(s)) == 0)
		return false;
	*w = s->GetWidth();
	*h = s->GetHeight();
	return true;
}

bool VulkanClient::clbkSetSurfaceColourKey(SURFHANDLE surf, DWORD ckey)
{
	if (!surf)
		return false;
	auto *s = reinterpret_cast<VkSurfNative *>(surf);
	if (surfaceCatalog.count(s) == 0)
		return false;
	s->SetColorKey(ckey);
	return true;
}

DWORD VulkanClient::clbkGetDeviceColour(BYTE r, BYTE g, BYTE b)
{
	return ((DWORD)r << 16) | ((DWORD)g << 8) | (DWORD)b;
}

bool VulkanClient::clbkCopyBitmap(SURFHANDLE pdds, HBITMAP hbm, int x, int y, int dx, int dy)
{
	if (!pdds || !hbm)
		return false;
	auto *s = reinterpret_cast<VkSurfNative *>(pdds);
	if (surfaceCatalog.count(s) == 0)
		return false;
	if (s->CopyFromHBITMAP(hbm, x, y, dx, dy)) {
		s->SetName("clbkCopyBitmap");
		return true;
	}
	return false;
}

bool VulkanClient::clbkSaveSurfaceToImage(SURFHANDLE surf, const char *fname, ImageFileFormat fmt, float quality)
{
	if (!swapchain)
		return false;

	std::vector<uint8_t> bgra;
	uint32_t w = 0, h = 0;

	if (!surf) {
		if (!swapchain->ReadbackSceneTarget(bgra, w, h) || w == 0 || h == 0)
			return false;
	} else {
		auto *s = reinterpret_cast<VkSurfNative *>(surf);
		if (surfaceCatalog.count(s) == 0)
			return false;
		if (!s->ReadbackBgra(bgra, w, h) || w == 0 || h == 0)
			return false;
	}

	if (!fname) {
		HWND hWnd = GetRenderWindow();
		if (!OpenClipboard(hWnd))
			return false;

		BITMAPINFO bmi{};
		bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
		bmi.bmiHeader.biWidth       = (LONG)w;
		bmi.bmiHeader.biHeight      = -(LONG)h;
		bmi.bmiHeader.biPlanes      = 1;
		bmi.bmiHeader.biBitCount    = 32;
		bmi.bmiHeader.biCompression = BI_RGB;

		void *bits = nullptr;
		HDC hdc = GetDC(hWnd);
		HBITMAP hbm = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
		if (hdc)
			ReleaseDC(hWnd, hdc);

		bool ok = false;
		if (hbm && bits) {
			std::memcpy(bits, bgra.data(), bgra.size());
			EmptyClipboard();
			ok = (SetClipboardData(CF_BITMAP, hbm) != nullptr);
			if (!ok)
				DeleteObject(hbm);
		}
		CloseClipboard();
		return ok;
	}

	ImageData id{};
	id.width   = w;
	id.height  = h;
	id.bpp     = 24;
	id.stride  = ((w * 24u + 31u) & ~31u) >> 3;
	id.bufsize = id.stride * h;

	std::vector<BYTE> bgr(id.bufsize, 0);
	for (uint32_t y = 0; y < h; ++y) {
		const uint8_t *srcRow = bgra.data() + (size_t)y * w * 4;
		BYTE *dstRow = bgr.data() + (size_t)y * id.stride;
		for (uint32_t x = 0; x < w; ++x) {
			dstRow[x * 3 + 0] = srcRow[x * 4 + 0];
			dstRow[x * 3 + 1] = srcRow[x * 4 + 1];
			dstRow[x * 3 + 2] = srcRow[x * 4 + 2];
		}
	}
	id.data = bgr.data();

	return WriteImageDataToFile(id, fname, fmt, quality);
}

// ==============================================================
// Surface Blitting, Filling & Win32 GDI Bridge (Unit 1.2)
// ==============================================================

int VulkanClient::clbkBeginBltGroup(SURFHANDLE tgt)
{
	if (tgt == RENDERTGT_NONE)
		return -2;
	if (bltGroupTgt != RENDERTGT_NONE)
		return -1;
	if (tgt != RENDERTGT_MAINWINDOW) {
		auto *s = reinterpret_cast<VkSurfNative *>(tgt);
		if (surfaceCatalog.count(s) == 0)
			return -3;
	}
	bltGroupTgt = tgt;
	return 0;
}

int VulkanClient::clbkEndBltGroup()
{
	if (bltGroupTgt == RENDERTGT_NONE)
		return -2;
	bltGroupTgt = RENDERTGT_NONE;
	return 0;
}

bool VulkanClient::clbkBlt(SURFHANDLE tgt, DWORD tgtx, DWORD tgty, SURFHANDLE src, DWORD flag) const
{
	if (!src || src == RENDERTGT_NONE)
		return false;
	const auto *srcSurf = reinterpret_cast<const VkSurfNative *>(src);
	if (surfaceCatalog.count(const_cast<VkSurfNative *>(srcSurf)) == 0)
		return false;
	return clbkScaleBlt(tgt, tgtx, tgty, srcSurf->GetWidth(), srcSurf->GetHeight(),
		src, 0, 0, srcSurf->GetWidth(), srcSurf->GetHeight(), flag);
}

bool VulkanClient::clbkBlt(SURFHANDLE tgt, DWORD tgtx, DWORD tgty, SURFHANDLE src, DWORD srcx, DWORD srcy, DWORD w, DWORD h, DWORD flag) const
{
	return clbkScaleBlt(tgt, tgtx, tgty, w, h, src, srcx, srcy, w, h, flag);
}

bool VulkanClient::clbkScaleBlt(
	SURFHANDLE tgt, DWORD tgtx, DWORD tgty, DWORD tgtw, DWORD tgth,
	SURFHANDLE src, DWORD srcx, DWORD srcy, DWORD srcw, DWORD srch, DWORD flag) const
{
	if (!ctx || !swapchain || !src || src == RENDERTGT_NONE || tgt == RENDERTGT_NONE)
		return false;

	auto *srcSurf = reinterpret_cast<VkSurfNative *>(src);
	if (surfaceCatalog.count(srcSurf) == 0)
		return false;

	const bool tgtIsMain = (tgt == RENDERTGT_MAINWINDOW);
	VkSurfNative *tgtSurf = nullptr;
	if (!tgtIsMain) {
		tgtSurf = reinterpret_cast<VkSurfNative *>(tgt);
		if (surfaceCatalog.count(tgtSurf) == 0)
			return false;
		if (tgtSurf->IsCompressed() && !tgtSurf->Decompress())
			return false;
	}

	const uint32_t srcTexW = srcSurf->GetWidth();
	const uint32_t srcTexH = srcSurf->GetHeight();
	const uint32_t dstTexW = tgtIsMain ? swapchain->SceneExtent().width : tgtSurf->GetWidth();
	const uint32_t dstTexH = tgtIsMain ? swapchain->SceneExtent().height : tgtSurf->GetHeight();

	if (srcw == 0 || srch == 0 || tgtw == 0 || tgth == 0 ||
		srcTexW == 0 || srcTexH == 0 || dstTexW == 0 || dstTexH == 0)
	{
		return true;
	}

	const RECT rs = { (LONG)srcx, (LONG)srcy, (LONG)srcx + (LONG)srcw, (LONG)srcy + (LONG)srch };
	const RECT rt = { (LONG)tgtx, (LONG)tgty, (LONG)tgtx + (LONG)tgtw, (LONG)tgty + (LONG)tgth };

	// Match D3D7 / D3D9Client unclipped rectangle rejection semantics
	if (rs.left < 0 || rs.top < 0 || rs.right > (LONG)srcTexW || rs.bottom > (LONG)srcTexH)
		return true;
	if (rt.left < 0 || rt.top < 0 || rt.right > (LONG)dstTexW || rt.bottom > (LONG)dstTexH)
		return true;
	if (rs.left >= rs.right || rs.top >= rs.bottom || rt.left >= rt.right || rt.top >= rt.bottom)
		return true;

	VkCommandBuffer cmd = swapchain->ActiveCommandBuffer();
	if (cmd == VK_NULL_HANDLE)
		return false;

	const DWORD srcCK = srcSurf->GetColorKey();
	const bool useColorKey = (srcCK != SURF_NO_CK) && (((flag & BLT_SRCCOLORKEY) != 0) || (srcCK != 0));
	const bool isScaled = (srcw != tgtw) || (srch != tgth);

	// Same-surface blit (e.g. HUD / ShuttleA ADI): route through per-surface temporary image
	if (tgtSurf && srcSurf == tgtSurf) {
		if (!srcSurf->EnsureTempBlitImage())
			return false;

		srcSurf->TransitionToTransferSrc(cmd);

		VkImageMemoryBarrier2 tmpToDst{};
		tmpToDst.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
		tmpToDst.srcStageMask        = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
		tmpToDst.srcAccessMask       = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
		tmpToDst.dstStageMask        = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
		tmpToDst.dstAccessMask       = VK_ACCESS_2_TRANSFER_WRITE_BIT;
		tmpToDst.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
		tmpToDst.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		tmpToDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		tmpToDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		tmpToDst.image               = srcSurf->GetTempBlitImage();
		tmpToDst.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

		VkDependencyInfo dep{};
		dep.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		dep.imageMemoryBarrierCount = 1;
		dep.pImageMemoryBarriers    = &tmpToDst;
		vkCmdPipelineBarrier2(cmd, &dep);

		VkImageCopy2 copyRegion{};
		copyRegion.sType          = VK_STRUCTURE_TYPE_IMAGE_COPY_2;
		copyRegion.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
		copyRegion.srcOffset      = { rs.left, rs.top, 0 };
		copyRegion.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
		copyRegion.dstOffset      = { 0, 0, 0 };
		copyRegion.extent         = { srcw, srch, 1 };

		VkCopyImageInfo2 copyInfo{};
		copyInfo.sType          = VK_STRUCTURE_TYPE_COPY_IMAGE_INFO_2;
		copyInfo.srcImage       = srcSurf->GetImage();
		copyInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		copyInfo.dstImage       = srcSurf->GetTempBlitImage();
		copyInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		copyInfo.regionCount    = 1;
		copyInfo.pRegions       = &copyRegion;
		vkCmdCopyImage2(cmd, &copyInfo);

		bool ok = false;
		if (!useColorKey) {
			VkImageMemoryBarrier2 tmpToSrc = tmpToDst;
			tmpToSrc.srcStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
			tmpToSrc.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
			tmpToSrc.dstStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
			tmpToSrc.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
			tmpToSrc.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			tmpToSrc.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			dep.pImageMemoryBarriers = &tmpToSrc;
			vkCmdPipelineBarrier2(cmd, &dep);

			tgtSurf->TransitionToTransferDst(cmd);

			VkImageBlit2 blitRegion{};
			blitRegion.sType          = VK_STRUCTURE_TYPE_IMAGE_BLIT_2;
			blitRegion.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
			blitRegion.srcOffsets[0]  = { 0, 0, 0 };
			blitRegion.srcOffsets[1]  = { (int32_t)srcw, (int32_t)srch, 1 };
			blitRegion.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
			blitRegion.dstOffsets[0]  = { rt.left, rt.top, 0 };
			blitRegion.dstOffsets[1]  = { rt.right, rt.bottom, 1 };

			VkBlitImageInfo2 blitInfo{};
			blitInfo.sType          = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2;
			blitInfo.srcImage       = srcSurf->GetTempBlitImage();
			blitInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			blitInfo.dstImage       = tgtSurf->GetImage();
			blitInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			blitInfo.regionCount    = 1;
			blitInfo.pRegions       = &blitRegion;
			blitInfo.filter         = isScaled ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
			vkCmdBlitImage2(cmd, &blitInfo);
			ok = true;
		} else if (blitter) {
			VkImageMemoryBarrier2 tmpToSample = tmpToDst;
			tmpToSample.srcStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
			tmpToSample.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
			tmpToSample.dstStageMask  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
			tmpToSample.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
			tmpToSample.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			tmpToSample.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			dep.pImageMemoryBarriers  = &tmpToSample;
			vkCmdPipelineBarrier2(cmd, &dep);

			tgtSurf->TransitionToColorAttachment(cmd);
			const RECT tmpRs = { 0, 0, (LONG)srcw, (LONG)srch };
			const uint32_t slot = swapchain->ActiveSlotIndex();
			ok = blitter->Blit(
				cmd, slot, swapchain->SlotGeneration(slot),
				srcSurf->GetTempBlitView(), srcTexW, srcTexH,
				tgtSurf->GetRenderTargetView(), tgtSurf->GetFormat(), dstTexW, dstTexH,
				tmpRs, rt, true, srcCK, false);
		}

		if (tgtSurf->GetMipMaps() > 1)
			tgtSurf->RegenerateMipmaps(cmd);
		tgtSurf->MarkGpuDirty();
		return ok;
	}

	const VkFormat dstFormat = tgtIsMain ? swapchain->SceneFormat() : tgtSurf->GetFormat();
	const bool dstHasAlpha = tgtIsMain ? false : tgtSurf->HasAlpha();
	const bool canFastBlit = !useColorKey &&
		!srcSurf->IsCompressed() &&
		(srcSurf->GetFormat() == dstFormat) &&
		(srcSurf->HasAlpha() || !dstHasAlpha);

	if (canFastBlit) {
		srcSurf->TransitionToTransferSrc(cmd);
		if (tgtIsMain) {
			if (swapchain->SceneLayout() == VK_IMAGE_LAYOUT_UNDEFINED &&
				(rt.left > 0 || rt.top > 0 || rt.right < (LONG)dstTexW || rt.bottom < (LONG)dstTexH))
			{
				swapchain->FillScene(nullptr, 0);
			}
			swapchain->TransitionSceneLayout(
				cmd,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
				VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
				VK_PIPELINE_STAGE_2_TRANSFER_BIT,
				VK_ACCESS_2_TRANSFER_WRITE_BIT);
		} else {
			tgtSurf->TransitionToTransferDst(cmd);
		}

		VkImageBlit2 blitRegion{};
		blitRegion.sType          = VK_STRUCTURE_TYPE_IMAGE_BLIT_2;
		blitRegion.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
		blitRegion.srcOffsets[0]  = { rs.left, rs.top, 0 };
		blitRegion.srcOffsets[1]  = { rs.right, rs.bottom, 1 };
		blitRegion.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
		blitRegion.dstOffsets[0]  = { rt.left, rt.top, 0 };
		blitRegion.dstOffsets[1]  = { rt.right, rt.bottom, 1 };

		VkBlitImageInfo2 blitInfo{};
		blitInfo.sType          = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2;
		blitInfo.srcImage       = srcSurf->GetImage();
		blitInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		blitInfo.dstImage       = tgtIsMain ? swapchain->SceneImage() : tgtSurf->GetImage();
		blitInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		blitInfo.regionCount    = 1;
		blitInfo.pRegions       = &blitRegion;
		blitInfo.filter         = isScaled ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
		vkCmdBlitImage2(cmd, &blitInfo);

		if (tgtSurf) {
			if (tgtSurf->GetMipMaps() > 1)
				tgtSurf->RegenerateMipmaps(cmd);
			tgtSurf->MarkGpuDirty();
		}
		return true;
	}

	if (!blitter)
		return false;

	srcSurf->TransitionToShaderRead(cmd);

	VkImageView dstView = VK_NULL_HANDLE;
	if (tgtIsMain) {
		if (swapchain->SceneLayout() == VK_IMAGE_LAYOUT_UNDEFINED)
			swapchain->FillScene(nullptr, 0);
		swapchain->TransitionSceneLayout(
			cmd,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
			VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
			VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
		dstView = swapchain->SceneImageView();
	} else {
		tgtSurf->TransitionToColorAttachment(cmd);
		dstView = tgtSurf->GetRenderTargetView();
	}

	const uint32_t slot = swapchain->ActiveSlotIndex();
	const bool linearFilter = isScaled && !useColorKey;
	const bool ok = blitter->Blit(
		cmd, slot, swapchain->SlotGeneration(slot),
		srcSurf->GetImageView(), srcTexW, srcTexH,
		dstView, dstFormat, dstTexW, dstTexH,
		rs, rt, useColorKey, srcCK, linearFilter);

	if (tgtSurf) {
		if (tgtSurf->GetMipMaps() > 1)
			tgtSurf->RegenerateMipmaps(cmd);
		tgtSurf->MarkGpuDirty();
	}
	return ok;
}

bool VulkanClient::clbkFillSurface(SURFHANDLE surf, DWORD col) const
{
	if (!swapchain || surf == RENDERTGT_NONE)
		return false;
	if (surf == RENDERTGT_MAINWINDOW)
		return swapchain->FillScene(nullptr, col);

	auto *s = reinterpret_cast<VkSurfNative *>(surf);
	if (surfaceCatalog.count(s) == 0)
		return false;
	return s->Fill(nullptr, col);
}

bool VulkanClient::clbkFillSurface(SURFHANDLE surf, DWORD tgtx, DWORD tgty, DWORD w, DWORD h, DWORD col) const
{
	if (!swapchain || surf == RENDERTGT_NONE)
		return false;
	const RECT rc = { (LONG)tgtx, (LONG)tgty, (LONG)tgtx + (LONG)w, (LONG)tgty + (LONG)h };
	if (surf == RENDERTGT_MAINWINDOW)
		return swapchain->FillScene(&rc, col);

	auto *s = reinterpret_cast<VkSurfNative *>(surf);
	if (surfaceCatalog.count(s) == 0)
		return false;
	return s->Fill(&rc, col);
}

HDC VulkanClient::clbkGetSurfaceDC(SURFHANDLE surf)
{
	if (!surf || surf == RENDERTGT_NONE)
		return nullptr;
	auto *s = reinterpret_cast<VkSurfNative *>(surf);
	if (surfaceCatalog.count(s) == 0)
		return nullptr;
	return s->GetDC();
}

void VulkanClient::clbkReleaseSurfaceDC(SURFHANDLE surf, HDC hDC)
{
	if (!surf || surf == RENDERTGT_NONE || !hDC)
		return;
	auto *s = reinterpret_cast<VkSurfNative *>(surf);
	if (surfaceCatalog.count(s) == 0)
		return;
	s->ReleaseDC(hDC);
}

// ==============================================================
// Phase 1 (Unit 1.3) / Phase 4 Placeholders
// ==============================================================

Sketchpad *VulkanClient::clbkGetSketchpad(SURFHANDLE surf)
{
	// STUB (Unit 1.3)
	return new VkPhase0SketchpadStub(surf, viewW, viewH);
}

void VulkanClient::clbkReleaseSketchpad(Sketchpad *sp)
{
	// STUB (Unit 1.3)
	delete sp;
}

Font *VulkanClient::clbkCreateFont(int height, bool prop, const char *face, FontStyle style, int orientation) const
{
	// STUB (Unit 1.3)
	return new VkPhase0FontStub(height, prop, face ? face : "Sans", style, orientation);
}

Font *VulkanClient::clbkCreateFontEx(int height, char *face, int width, int weight, FontStyle style, float spacing) const
{
	// STUB (Unit 1.3)
	(void)width;
	(void)weight;
	(void)spacing;
	return new VkPhase0FontStub(height, true, face ? face : "Sans", style, 0);
}

void VulkanClient::clbkReleaseFont(Font *font) const
{
	// STUB (Unit 1.3)
	delete font;
}

Pen *VulkanClient::clbkCreatePen(int style, int width, DWORD col) const
{
	// STUB (Unit 1.3)
	return new VkPhase0PenStub(style, width, col);
}

void VulkanClient::clbkReleasePen(Pen *pen) const
{
	// STUB (Unit 1.3)
	delete pen;
}

Brush *VulkanClient::clbkCreateBrush(DWORD col) const
{
	// STUB (Unit 1.3)
	return new VkPhase0BrushStub(col);
}

void VulkanClient::clbkReleaseBrush(Brush *brush) const
{
	// STUB (Unit 1.3)
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
