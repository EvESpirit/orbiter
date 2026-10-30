// ==============================================================
// VideoTab.h
// Launchpad "Video" tab controller for VulkanClient
// Part of the ORBITER VISUALISATION PROJECT (OVP) - VulkanClient
// Licensed under the MIT License
// ==============================================================

#ifndef __VKCLIENT_VIDEOTAB_H
#define __VKCLIENT_VIDEOTAB_H

#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>
#include "VkAPI.h"

namespace oapi { class VulkanClient; }

/**
 * \brief Hardware and driver metadata for a discovered Vulkan physical device.
 */
struct VkAdapterInfo {
	std::string name;                                              ///< Raw device name
	std::string label;                                             ///< Dropdown label (name + type + VRAM)
	VkPhysicalDeviceType deviceType = VK_PHYSICAL_DEVICE_TYPE_OTHER; ///< Device type
	uint32_t apiVersion = 0;                                       ///< Vulkan API version
	uint32_t driverVersion = 0;                                    ///< Vendor driver version
	uint32_t vendorID = 0;                                         ///< PCI vendor ID
	uint32_t deviceID = 0;                                         ///< PCI device ID
	uint64_t deviceLocalMB = 0;                                    ///< Device-local memory [MB]
	bool supportsVulkan13 = false;                                 ///< Supports Vulkan 1.3 or higher
};

/**
 * \brief Fullscreen desktop/monitor display mode entry.
 */
struct VkDisplayModeInfo {
	uint32_t width = 0;       ///< Width [px]
	uint32_t height = 0;      ///< Height [px]
	uint32_t refreshRate = 0; ///< Refresh rate [Hz]
};

/**
 * \brief Controller for Orbiter's Launchpad "Video" tab dialog.
 */
class VideoTab {
public:
	VideoTab(oapi::VulkanClient *gc, HINSTANCE hInst, HINSTANCE hOrbiterInst, HWND hVideoTab);
	~VideoTab() = default;

	bool Initialise();
	INT_PTR WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
	void UpdateConfigData();

	const std::vector<VkAdapterInfo> &GetAdapters() const { return adapters; }
	const std::vector<VkDisplayModeInfo> &GetDisplayModes() const { return displayModes; }

	static bool ProbeVulkanAdapters(std::vector<VkAdapterInfo> &outAdapters, bool *outHasValidationLayer = nullptr);
	static void ProbeDisplayModes(std::vector<VkDisplayModeInfo> &outModes, VkDisplayModeInfo *outDesktopMode = nullptr);

private:
	void SelectAdapter(int index);
	void SelectMode(int index);
	void SelectFullscreen(bool bFull);
	void SelectWidth();
	void SelectHeight();

	static INT_PTR CALLBACK AdvancedDlgProcWrp(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
	INT_PTR AdvancedDlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

	oapi::VulkanClient *gclient;
	HINSTANCE hInst;
	HINSTANCE hOrbiterInst;
	HWND hTab;

	std::vector<VkAdapterInfo> adapters;
	std::vector<VkDisplayModeInfo> displayModes;
	VkDisplayModeInfo desktopMode;
	bool validationLayerInstalled = false;
	int selectedAdapterIdx = 0;
	int aspectIdx = 2;              ///< 0 = 4:3, 1 = 16:10, 2 = 16:9
	bool updatingDimensions = false; ///< Guard against recursive EN_CHANGE
};

#endif // !__VKCLIENT_VIDEOTAB_H
