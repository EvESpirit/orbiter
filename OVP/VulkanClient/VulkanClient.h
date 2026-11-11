// ==============================================================
// VulkanClient.h
// Main oapi::GraphicsClient implementation for OpenOrbiter Vulkan 1.3
// Part of the ORBITER VISUALISATION PROJECT (OVP) - VulkanClient
// Licensed under the MIT License
// ==============================================================

#ifndef __VKCLIENT_VULKANCLIENT_H
#define __VKCLIENT_VULKANCLIENT_H

#include "Orbitersdk.h"
#include "GraphicsAPI.h"
#include "VkAPI.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class VideoTab;
class VkContext;
class VkSwapchain;
class VkSurfNative;

namespace oapi {

/**
 * \brief Native Vulkan 1.3 rendering client plugin (`oapi::GraphicsClient`) for OpenOrbiter.
 */
class VulkanClient : public GraphicsClient {
public:
	explicit VulkanClient(HINSTANCE hInstance);
	~VulkanClient() override;

	bool clbkInitialise() override;
	void clbkRefreshVideoData() override;
	INT_PTR LaunchpadVideoWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) override;

	bool clbkFullscreenMode() const override;
	void clbkGetViewportSize(DWORD *width, DWORD *height) const override;
	bool clbkGetRenderParam(DWORD prm, DWORD *value) const override;

	void clbkImGuiInit() override;
	void clbkImGuiShutdown() override;
	void clbkImGuiNewFrame() override;
	void clbkImGuiRenderDrawData() override;
	uint64_t clbkImGuiSurfaceTexture(SURFHANDLE surf) override;

	// STUB (Phase 5): Extended client interface query
	void *clbkGetExtendedInterface(const char *name) override;

	// Surface & texture management (Unit 1.1)
	SURFHANDLE clbkLoadTexture(const char *fname, DWORD flags = 0) override;
	SURFHANDLE clbkLoadSurface(const char *fname, DWORD attrib, bool bPath = false) override;
	void clbkReleaseTexture(SURFHANDLE hTex) override;
	SURFHANDLE clbkCreateSurfaceEx(DWORD w, DWORD h, DWORD attrib) override;
	SURFHANDLE clbkCreateSurface(DWORD w, DWORD h, SURFHANDLE hTemplate = NULL) override;
	SURFHANDLE clbkCreateSurface(HBITMAP hBmp) override;
	SURFHANDLE clbkCreateTexture(DWORD w, DWORD h) override;
	void clbkIncrSurfaceRef(SURFHANDLE surf) override;
	bool clbkReleaseSurface(SURFHANDLE surf) override;
	bool clbkGetSurfaceSize(SURFHANDLE surf, DWORD *w, DWORD *h) override;
	bool clbkSetSurfaceColourKey(SURFHANDLE surf, DWORD ckey) override;
	DWORD clbkGetDeviceColour(BYTE r, BYTE g, BYTE b) override;
	bool clbkCopyBitmap(SURFHANDLE pdds, HBITMAP hbm, int x, int y, int dx, int dy) override;
	bool clbkSaveSurfaceToImage(SURFHANDLE surf, const char *fname, ImageFileFormat fmt, float quality = 0.7f) override;

	// STUB (Unit 1.3): Font, pen, brush, and Sketchpad placeholders
	Sketchpad *clbkGetSketchpad(SURFHANDLE surf) override;
	void clbkReleaseSketchpad(Sketchpad *sp) override;
	Font *clbkCreateFont(int height, bool prop, const char *face, FontStyle style = FontStyle::FONT_NORMAL, int orientation = 0) const override;
	Font *clbkCreateFontEx(int height, char *face, int width = 0, int weight = 400, FontStyle style = FontStyle::FONT_NORMAL, float spacing = 0.0f) const override;
	void clbkReleaseFont(Font *font) const override;
	Pen *clbkCreatePen(int style, int width, DWORD col) const override;
	void clbkReleasePen(Pen *pen) const override;
	Brush *clbkCreateBrush(DWORD col) const override;
	void clbkReleaseBrush(Brush *brush) const override;

	// STUB (Phase 4): Particle and exhaust stream placeholders
	ParticleStream *clbkCreateParticleStream(PARTICLESTREAMSPEC *pss) override;
	ParticleStream *clbkCreateExhaustStream(PARTICLESTREAMSPEC *pss, OBJHANDLE hVessel, const double *lvl, const VECTOR3 *ref, const VECTOR3 *dir) override;
	ParticleStream *clbkCreateExhaustStream(PARTICLESTREAMSPEC *pss, OBJHANDLE hVessel, const double *lvl, const VECTOR3 &ref, const VECTOR3 &dir) override;
	ParticleStream *clbkCreateReentryStream(PARTICLESTREAMSPEC *pss, OBJHANDLE hVessel) override;

	bool IsValidSurface(const VkSurfNative *surf) const { return surfaceCatalog.count(const_cast<VkSurfNative *>(surf)) != 0; }

protected:
	bool clbkUseLaunchpadVideoTab() const override { return true; }

	HWND clbkCreateRenderWindow() override;
	void clbkPostCreation() override;
	void clbkCloseSession(bool fastclose) override;
	void clbkDestroyRenderWindow(bool fastclose) override;

	void clbkBeginFrame() override;
	void clbkRenderScene() override;
	void clbkEndFrame() override;
	bool clbkDisplayFrame() override;

private:
	void UpdateViewportSize(HWND hWnd);
	bool CreateImGuiPlaceholderTexture();
	void DestroyImGuiPlaceholderTexture();
	void DestroyAllSurfaces();

	std::unique_ptr<VideoTab> vtab;
	std::unique_ptr<VkContext> ctx;                                  ///< Per-session Vulkan device (null outside a session)
	std::unique_ptr<VkSwapchain> swapchain;                          ///< Per-session swapchain and scene target
	std::unordered_set<VkSurfNative *> surfaceCatalog;               ///< All live VkSurfNative instances in the session
	std::unordered_map<std::string, VkSurfNative *> sharedTextures;  ///< Shared/managed texture cache keyed by relative path
	std::vector<std::unique_ptr<ParticleStream>> stubParticleStreams; // STUB (Phase 4): Vessel exhaust stream placeholders
	DWORD viewW = 1280;                                              ///< Active render viewport width [px]
	DWORD viewH = 720;                                               ///< Active render viewport height [px]
	bool bFullscreen = false;                                        ///< Always false: full screen is a borderless window, never exclusive

	bool imguiInitialized = false;                                   ///< True while ImGui_ImplVulkan backend is active
	bool imguiFrameRendered = false;                                 ///< True when clbkImGuiRenderDrawData ran this frame
	VkFormat imguiMainFormat = VK_FORMAT_B8G8R8A8_UNORM;             ///< Color attachment format for main viewport dynamic rendering
	VkFormat imguiViewportFormat = VK_FORMAT_B8G8R8A8_UNORM;         ///< Color attachment format for secondary viewports
	VkImage imguiPlaceholderImage = VK_NULL_HANDLE;                  ///< 1x1 fallback texture when a null/invalid SURFHANDLE is queried
	VmaAllocation imguiPlaceholderAlloc = VK_NULL_HANDLE;
	VkImageView imguiPlaceholderView = VK_NULL_HANDLE;
	VkSampler imguiPlaceholderSampler = VK_NULL_HANDLE;
	VkDescriptorSet imguiPlaceholderDescSet = VK_NULL_HANDLE;
};

} // namespace oapi

#endif // !__VKCLIENT_VULKANCLIENT_H
