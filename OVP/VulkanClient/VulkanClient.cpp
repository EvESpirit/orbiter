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
#include "VkPad.h"
#include "VkPanel2D.h"
#include "VkSurface.h"
#include "VkTextMgr.h"
#include "VkPadTools.h"
#include "VkLog.h"
#include "imgui.h"
#include "imgui_impl_vulkan.h"
#include <algorithm>
#include <cctype>
#include <cmath>
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
	panel2d.reset();
	if (skpPipeline) {
		skpPipeline->FlushActivePads();
		skpPipeline.reset();
	}
	if (defaultPanelTex) {
		clbkReleaseSurface(defaultPanelTex);
		defaultPanelTex = nullptr;
	}
	if (defaultFont) {
		clbkReleaseFont(defaultFont);
		defaultFont = nullptr;
	}
	DestroyAllSurfaces();
	VkFontCache::ReleaseGpuResources();
	blitter.reset();
	swapchain.reset();
	ctx.reset();
	stubParticleStreams.clear();
	DestroyAllDrawingTools();
	VkFontCache::Clear();
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

	skpPipeline = std::make_unique<VkPadPipeline>();
	if (!skpPipeline->Init(*ctx, *swapchain)) {
		skpPipeline.reset();
		VkLogWarn("VkPadPipeline initialisation failed; 2D Sketchpad rendering is disabled");
	}
	if (skpPipeline) {
		panel2d = std::make_unique<VkPanel2D>();
		if (!panel2d->Init(*ctx, *swapchain, *skpPipeline)) {
			panel2d.reset();
			VkLogWarn("VkPanel2D initialisation failed; 2D panel rendering is disabled");
		}
	}
	defaultPanelTex = clbkLoadTexture("Null.dds");
	if (!defaultPanelTex)
		VkLogWarn("clbkCreateRenderWindow: Null.dds not found; using black MFD fallback");
	if (!defaultFont)
		defaultFont = clbkCreateFont(20, true, "fixed");

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
#ifdef VKCLIENT_SKETCHPAD_TEST
	SketchPadTest();
#endif
}

#ifdef VKCLIENT_SKETCHPAD_TEST
// ==============================================================
// Unit 1.4: SketchPad parity harness (mirrors D3D9Client::SketchPadTest)
// Part 1 is pixel-exact against D3D9Client.cpp:616-674 -> SketchpadOutput.png.
// Part 2 exercises all Phase-1 Sketchpad features (transforms, wide miter
// joins, dashed lines, CopyTetragon, GradientFillRect, Text) ->
// SketchpadOutput2_Phase1.png; gcCore2 CreateTriangles/CreatePoly + DrawPoly
// and IMAGE_DDS export wait until Phase 8 (gcCore).
// ==============================================================
void VulkanClient::SketchPadTest()
{
	VkLogAlw("[SKPTEST] Running Unit 1.4 SketchPadTest()...");

	// --- Part 1: Direct mirror of D3D9Client::SketchPadTest() (lines 618-673) ---
	SURFHANDLE hSrc = clbkLoadSurface("D3D9/SketchpadTest.dds", OAPISURFACE_TEXTURE);
	SURFHANDLE hTgt = clbkLoadSurface("D3D9/SketchpadTest.dds", OAPISURFACE_RENDERTARGET);
	if (!hSrc || !hTgt) {
		VkLogErr("[SKPTEST] Failed to load D3D9/SketchpadTest.dds (hSrc=%p, hTgt=%p)", hSrc, hTgt);
		if (hSrc) clbkReleaseSurface(hSrc);
		if (hTgt) clbkReleaseSurface(hTgt);
		return;
	}

	clbkSetSurfaceColourKey(hSrc, 0xFFFFFFFF);

	Sketchpad *pSkp = clbkGetSketchpad(hTgt);
	if (!pSkp) {
		VkLogErr("[SKPTEST] clbkGetSketchpad(hTgt) returned null");
		clbkReleaseSurface(hSrc);
		clbkReleaseSurface(hTgt);
		return;
	}

	RECT r = { 17, 1, 31, 15 };
	RECT t = { 1, 33, 31, 63 };
	RECT q = { 0, 16, 16, 32 };
	RECT w = { 49, 1, 63, 15 };

	pSkp->CopyRect(hSrc, &r, 1, 1);
	pSkp->ColorKey(hSrc, &r, 33, 1);
	pSkp->SetBlendState(static_cast<Sketchpad::BlendState>(Sketchpad::BlendState::ALPHABLEND | Sketchpad::BlendState::FILTER_POINT));
	pSkp->StretchRect(hSrc, &r, &t);
	pSkp->SetBlendState();
	pSkp->StretchRect(hSrc, &r, &w);
	pSkp->RotateRect(hSrc, &q, 24, 24, float(PI05));
	pSkp->RotateRect(hSrc, &q, 40, 24, float(PI));
	pSkp->RotateRect(hSrc, &q, 56, 24, float(-PI05));

	RECT tr = { 33, 49, 47, 63 };
	pSkp->ColorFill(0xFFFF00FF, &tr);

	pSkp->QuickPen(0xFF000000);
	pSkp->QuickBrush(0x80FFFF00);
	pSkp->Rectangle(49, 33, 63, 47); // 1px margin
	pSkp->Ellipse(49, 49, 63, 63);   // 1px margin

	// No Pen, Brush Only
	pSkp->SetPen(nullptr);
	pSkp->QuickBrush(0xFF00FF00);
	pSkp->Rectangle(65, 33, 79, 47); // 1px tlm 1px brm
	pSkp->Ellipse(65, 49, 79, 63);   // 1px tlm 1px brm

	pSkp->QuickPen(0xFFFFFFFF);
	pSkp->MoveTo(65, 1);
	pSkp->LineTo(65, 14);
	pSkp->LineTo(78, 14);
	pSkp->LineTo(78, 1);
	pSkp->LineTo(65, 1);

	RECT cr = { 33, 33, 47, 47 };
	pSkp->ClipRect(&cr);
	pSkp->ColorFill(0xFFFFFF00, nullptr);
	pSkp->ClipRect();

	clbkReleaseSketchpad(pSkp);

	const bool savedPart1 = clbkSaveSurfaceToImage(hTgt, "SketchpadOutput", IMAGE_PNG);
	VkLogAlw("[SKPTEST] Part 1 SketchpadOutput.png saved=%d", savedPart1 ? 1 : 0);

	clbkReleaseSurface(hSrc);
	clbkReleaseSurface(hTgt);

	// --- Part 2 (Phase-1 subset): transforms, wide miter pens, CopyTetragon, gradients, text ---
	SURFHANDLE hTgt2 = clbkCreateSurfaceEx(768, 512, OAPISURFACE_RENDERTARGET | OAPISURFACE_TEXTURE);
	if (!hTgt2) {
		VkLogErr("[SKPTEST] Failed to create 768x512 target for Part 2");
		return;
	}

	pSkp = clbkGetSketchpad(hTgt2);
	if (!pSkp) {
		clbkReleaseSurface(hTgt2);
		return;
	}

	pSkp->ColorFill(0xFFFFFFFF, nullptr);

	IVECTOR2 hex[6]{};
	IVECTOR2 hexHalf[6]{};
	for (int i = 0; i < 6; ++i) {
		const double a = (PI2 / 6.0) * double(i);
		hex[i].x = int(std::lround(std::cos(a) * 100.0));
		hex[i].y = int(std::lround(std::sin(a) * 100.0));
		hexHalf[i].x = int(std::lround(std::cos(a) * 50.0));
		hexHalf[i].y = int(std::lround(std::sin(a) * 50.0));
	}

	const IVECTOR2 pos0 = { 128, 128 };
	const IVECTOR2 pos1 = { 128, 384 };
	const IVECTOR2 pos2 = { 384, 128 };
	const IVECTOR2 pos3 = { 640, 128 };
	const IVECTOR2 pos4 = { 384, 384 };
	const IVECTOR2 pos5 = { 640, 384 };
	const FVECTOR2 scaleOne = { 1.0f, 1.0f };
	const FVECTOR2 scaleTwo = { 2.0f, 2.0f };

	pSkp->PushWorldTransform();

	// Scaled filled hexagon + 3px black outline (pos0)
	pSkp->QuickBrush(0xA0000088);
	pSkp->QuickPen(0xA0000000, 3.0f);
	pSkp->SetWorldScaleTransform2D(&scaleTwo, &pos0);
	pSkp->Polygon(hexHalf, 6);

	// Open 5-segment polyline + dashed rectangle (pos1)
	pSkp->SetWorldScaleTransform2D(&scaleOne, &pos1);
	pSkp->SetBrush(nullptr);
	pSkp->QuickPen(0xFFCC2200, 4.0f);
	pSkp->Polyline(hex, 6);

	// Dashed pen + GradientFillRect (pos2)
	pSkp->SetWorldScaleTransform2D(&scaleOne, &pos2);
	RECT gradR = { -90, -70, 90, 70 };
	pSkp->GradientFillRect(&gradR, 0xFFFF8800, 0xFF2244AA, true);
	pSkp->QuickPen(0xFF000000, 3.0f, 2);
	pSkp->SetBrush(nullptr);
	pSkp->Rectangle(-90, -70, 90, 70);

	// Text alignment + rotated text (pos3)
	pSkp->SetWorldScaleTransform2D(&scaleOne, &pos3);
	Font *testFont = clbkCreateFont(22, true, "Sans");
	Font *rotFont = clbkCreateFont(18, false, "Fixed", FontStyle::FONT_NORMAL, 300);
	pSkp->SetFont(testFont);
	pSkp->SetTextColor(0xFF000000);
	pSkp->SetTextAlign(Sketchpad::CENTER, Sketchpad::BASELINE);
	pSkp->Text(0, -20, "VkPad 1.4 Parity", 16);
	pSkp->SetFont(rotFont);
	pSkp->Text(0, 35, "30deg Mono Test", 15);
	pSkp->SetFont(nullptr);

	// Wide 25px pen miter-joined closed hexagon (pos4, mirrors D3D9Client.cpp:781-783)
	pSkp->SetWorldScaleTransform2D(&scaleOne, &pos4);
	pSkp->SetBrush(nullptr);
	pSkp->QuickPen(0xFF000000, 25.0f);
	pSkp->Polygon(hex, 6);

	// CopyTetragon with generic/noisep.dds (pos5, mirrors D3D9Client.cpp:785-795)
	SURFHANDLE hNoise = clbkLoadSurface("generic/noisep.dds", OAPISURFACE_TEXTURE);
	if (hNoise) {
		pSkp->SetWorldScaleTransform2D(&scaleOne, &pos5);
		FVECTOR2 pt[4];
		pt[0] = FVECTOR2(-100.0f, -100.0f);
		pt[1] = FVECTOR2(-100.0f, 50.0f);
		pt[2] = FVECTOR2(100.0f, 100.0f);
		pt[3] = FVECTOR2(100.0f, -50.0f);
		pSkp->CopyTetragon(hNoise, nullptr, pt);
	}

	pSkp->PopWorldTransform();

	// Explicit valid ColorKey tests (Part 1 uses 0xFFFFFFFF == SURF_NO_CK)
	SURFHANDLE hSrcCk = clbkLoadSurface("D3D9/SketchpadTest.dds", OAPISURFACE_TEXTURE);
	if (hSrcCk) {
		RECT ckR = { 17, 1, 31, 15 };
		RECT ckBg1 = { 15, 7, 31, 23 };
		RECT ckBg2 = { 39, 7, 55, 23 };
		pSkp->ColorFill(0xFFFF00FF, &ckBg1);
		clbkSetSurfaceColourKey(hSrcCk, 0xFF000000);
		pSkp->ColorKey(hSrcCk, &ckR, 16, 8);

		pSkp->ColorFill(0xFFFFFF00, &ckBg2);
		clbkSetSurfaceColourKey(hSrcCk, 0x00FFFFFF);
		pSkp->ColorKey(hSrcCk, &ckR, 40, 8);

		clbkSetSurfaceColourKey(hSrcCk, SURF_NO_CK);
	}

	clbkReleaseSketchpad(pSkp);
	clbkReleaseFont(testFont);
	clbkReleaseFont(rotFont);
	if (hSrcCk)
		clbkReleaseSurface(hSrcCk);
	if (hNoise)
		clbkReleaseSurface(hNoise);

	const bool savedPart2 = clbkSaveSurfaceToImage(hTgt2, "SketchpadOutput2_Phase1", IMAGE_PNG);
	VkLogAlw("[SKPTEST] Part 2 SketchpadOutput2_Phase1.png saved=%d", savedPart2 ? 1 : 0);

	clbkReleaseSurface(hTgt2);
}
#endif

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

// Drawing tools do not hold device resources, so they are not tied to a session
// (modules such as DeltaGlider create pens in InitModule and release them in ExitModule,
// which may run after this client is unregistered).
void VulkanClient::DestroyAllDrawingTools()
{
	if (!fontCatalog.empty())
		VkLogInfo("Freeing %zu fonts still held at module exit", fontCatalog.size());
	for (Font *f : fontCatalog)
		delete static_cast<VkPadFont *>(f);
	fontCatalog.clear();

	if (!penCatalog.empty())
		VkLogInfo("Freeing %zu pens still held at module exit", penCatalog.size());
	for (Pen *p : penCatalog)
		delete static_cast<VkPadPen *>(p);
	penCatalog.clear();

	if (!brushCatalog.empty())
		VkLogInfo("Freeing %zu brushes still held at module exit", brushCatalog.size());
	for (Brush *b : brushCatalog)
		delete static_cast<VkPadBrush *>(b);
	brushCatalog.clear();
}

void VulkanClient::clbkDestroyRenderWindow(bool fastclose)
{
	VkLogAlw("================ clbkDestroyRenderWindow ================");
	if (imguiInitialized)
		clbkImGuiShutdown();
	panel2d.reset();
	if (skpPipeline) {
		skpPipeline->FlushActivePads();
		skpPipeline.reset();
	}
	if (defaultPanelTex) {
		clbkReleaseSurface(defaultPanelTex);
		defaultPanelTex = nullptr;
	}
	if (defaultFont) {
		clbkReleaseFont(defaultFont);
		defaultFont = nullptr;
	}
	DestroyAllSurfaces();
	VkFontCache::ReleaseGpuResources();
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
		if (skpPipeline)
			skpPipeline->OnSurfaceDestroyed(surf);
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
	// Core WriteImageDataToFile only indexes [".bmp", ".png", ".jpg", ".tif"].
	if (fname && (int(fmt) < int(IMAGE_BMP) || int(fmt) > int(IMAGE_TIF)))
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
// 2D Panel, Glass Cockpit & HUD Mesh Rendering (Unit 1.3c)
// ==============================================================

void VulkanClient::clbkRender2DPanel(SURFHANDLE *hSurf, MESHHANDLE hMesh, MATRIX3 *T, bool additive)
{
	clbkRender2DPanel(hSurf, hMesh, T, 1.0f, additive);
}

void VulkanClient::clbkRender2DPanel(SURFHANDLE *hSurf, MESHHANDLE hMesh, MATRIX3 *T, float alpha, bool additive)
{
	if (panel2d)
		panel2d->Render(*this, hSurf, hMesh, T, alpha, additive, defaultPanelTex);
}

// ==============================================================
// Sketchpad (Unit 1.3) / Phase 4 Placeholders
// ==============================================================

Sketchpad *VulkanClient::clbkGetSketchpad(SURFHANDLE surf)
{
	if (!skpPipeline)
		return nullptr;
	if (!defaultFont)
		defaultFont = clbkCreateFont(20, true, "fixed");
	return skpPipeline->AcquirePad(*this, defaultFont, surf);
}

void VulkanClient::clbkReleaseSketchpad(Sketchpad *sp)
{
	if (!skpPipeline) {
		delete sp;
		return;
	}
	skpPipeline->ReleasePad(sp);
}

Font *VulkanClient::clbkCreateFont(int height, bool prop, const char *face, FontStyle style, int orientation) const
{
	auto *font = new VkPadFont(height, prop, face, style, orientation);
	if (!font->IsValid())
		VkLogWarn("clbkCreateFont: no glyph atlas for '%s' height %d", face ? face : "(null)", height);
	fontCatalog.insert(font);
#ifdef VKCLIENT_FONT_DEBUG
	if (ctx && swapchain && font->IsValid())
		font->Atlas()->GetSurface(*ctx, *swapchain); // Upload immediately to exercise the GPU readback check
#endif
	return font;
}

Font *VulkanClient::clbkCreateFontEx(int height, char *face, int width, int weight, FontStyle style, float spacing) const
{
	auto *font = new VkPadFont(height, face, width, weight, style, spacing);
	if (!font->IsValid())
		VkLogWarn("clbkCreateFontEx: no glyph atlas for '%s' height %d", face ? face : "(null)", height);
	fontCatalog.insert(font);
#ifdef VKCLIENT_FONT_DEBUG
	if (ctx && swapchain && font->IsValid())
		font->Atlas()->GetSurface(*ctx, *swapchain); // Upload immediately to exercise the GPU readback check
#endif
	return font;
}

void VulkanClient::clbkReleaseFont(Font *font) const
{
	if (!font || fontCatalog.erase(font) == 0)
		return;
	delete static_cast<VkPadFont *>(font);
}

Pen *VulkanClient::clbkCreatePen(int style, int width, DWORD col) const
{
	auto *pen = new VkPadPen(style, width, col);
	penCatalog.insert(pen);
	return pen;
}

void VulkanClient::clbkReleasePen(Pen *pen) const
{
	if (!pen || penCatalog.erase(pen) == 0)
		return;
	delete static_cast<VkPadPen *>(pen);
}

Brush *VulkanClient::clbkCreateBrush(DWORD col) const
{
	auto *brush = new VkPadBrush(col);
	brushCatalog.insert(brush);
	return brush;
}

void VulkanClient::clbkReleaseBrush(Brush *brush) const
{
	if (!brush || brushCatalog.erase(brush) == 0)
		return;
	delete static_cast<VkPadBrush *>(brush);
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
