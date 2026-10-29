// ==============================================================
// VulkanClient.h
// Part of the ORBITER VISUALISATION PROJECT (OVP) - VulkanClient
// Licensed under the MIT License
// ==============================================================

#ifndef __VKCLIENT_VULKANCLIENT_H
#define __VKCLIENT_VULKANCLIENT_H

#include "Orbitersdk.h"
#include "GraphicsAPI.h"

namespace oapi {

class VulkanClient : public GraphicsClient {
public:
	explicit VulkanClient(HINSTANCE hInstance);
	~VulkanClient() override;

	bool clbkInitialise() override;
	bool clbkFullscreenMode() const override;
	void clbkGetViewportSize(DWORD *width, DWORD *height) const override;
	bool clbkGetRenderParam(DWORD prm, DWORD *value) const override;
	void *clbkGetExtendedInterface(const char *name) override; // STUB

	void clbkImGuiInit() override {} // STUB
	void clbkImGuiShutdown() override {} // STUB
	void clbkImGuiNewFrame() override {} // STUB
	void clbkImGuiRenderDrawData() override {} // STUB
	uint64_t clbkImGuiSurfaceTexture(SURFHANDLE) override { return 0; } // STUB

protected:
	bool clbkUseLaunchpadVideoTab() const override { return false; } // STUB: VideoTab wired in Unit 0.1b

	HWND clbkCreateRenderWindow() override; // STUB
	void clbkPostCreation() override; // STUB
	void clbkCloseSession(bool fastclose) override; // STUB
	void clbkDestroyRenderWindow(bool fastclose) override; // STUB

	void clbkBeginFrame() override; // STUB
	void clbkRenderScene() override; // STUB
	void clbkEndFrame() override; // STUB
	bool clbkDisplayFrame() override; // STUB

private:
	DWORD viewW = 1280;
	DWORD viewH = 720;
	bool bFullscreen = false;
};

} // namespace oapi

#endif // !__VKCLIENT_VULKANCLIENT_H
