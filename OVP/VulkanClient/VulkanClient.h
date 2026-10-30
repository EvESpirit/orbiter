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
#include <memory>
#include <vector>

class VideoTab;

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

	// STUB (Unit 0.3): Dear ImGui Vulkan 1.3 dynamic-rendering + multi-viewport backend
	void clbkImGuiInit() override;
	void clbkImGuiShutdown() override;
	void clbkImGuiNewFrame() override;
	void clbkImGuiRenderDrawData() override;
	uint64_t clbkImGuiSurfaceTexture(SURFHANDLE surf) override;

	// STUB (Phase 5): Extended client interface query
	void *clbkGetExtendedInterface(const char *name) override;

	// STUB (Phase 1): Surface, texture, font, pen, brush, and Sketchpad placeholders
	SURFHANDLE clbkLoadTexture(const char *fname, DWORD flags = 0) override;
	SURFHANDLE clbkLoadSurface(const char *fname, DWORD attrib, bool bPath = false) override;
	void clbkReleaseTexture(SURFHANDLE hTex) override;
	SURFHANDLE clbkCreateSurfaceEx(DWORD w, DWORD h, DWORD attrib) override;
	SURFHANDLE clbkCreateSurface(DWORD w, DWORD h, SURFHANDLE hTemplate = NULL) override;
	SURFHANDLE clbkCreateTexture(DWORD w, DWORD h) override;
	void clbkIncrSurfaceRef(SURFHANDLE surf) override;
	bool clbkReleaseSurface(SURFHANDLE surf) override;
	bool clbkGetSurfaceSize(SURFHANDLE surf, DWORD *w, DWORD *h) override;
	bool clbkCopyBitmap(SURFHANDLE pdds, HBITMAP hbm, int x, int y, int dx, int dy) override;

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

protected:
	bool clbkUseLaunchpadVideoTab() const override { return true; }

	// STUB (Unit 0.2): Render window, VkContext, swapchain, and frame-bracket lifecycle
	HWND clbkCreateRenderWindow() override;
	void clbkPostCreation() override;
	void clbkCloseSession(bool fastclose) override;
	void clbkDestroyRenderWindow(bool fastclose) override;

	void clbkBeginFrame() override;
	void clbkRenderScene() override;
	void clbkEndFrame() override;
	bool clbkDisplayFrame() override;

private:
	std::unique_ptr<VideoTab> vtab;
	std::vector<SURFHANDLE> managedStubSurfaces;                     // STUB (Phase 1): Global textures awaiting session cleanup
	std::vector<std::unique_ptr<ParticleStream>> stubParticleStreams; // STUB (Phase 4): Vessel exhaust stream placeholders
	DWORD viewW = 1280;                                             ///< Active render viewport width [px]
	DWORD viewH = 720;                                              ///< Active render viewport height [px]
	bool bFullscreen = false;                                       ///< True when running in fullscreen mode
};

} // namespace oapi

#endif // !__VKCLIENT_VULKANCLIENT_H
