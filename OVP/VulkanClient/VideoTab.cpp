// ==============================================================
// VideoTab.cpp
// Launchpad "Video" tab controller for VulkanClient
// Part of the ORBITER VISUALISATION PROJECT (OVP) - VulkanClient
// Licensed under the MIT License
// ==============================================================

#define STRICT 1
#include "Orbitersdk.h"
#include "resource_video.h"
#include "Resource.h"
#include "VulkanClient.h"
#include "VideoTab.h"
#include "VkConfig.h"
#include "VkLog.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace oapi;

namespace {
// Aspect-ratio presets for IDC_VID_4X3, IDC_VID_16X10, IDC_VID_16X9
const int kAspectW[3] = { 4, 16, 16 };
const int kAspectH[3] = { 3, 10, 9 };

// Option tables for IDD_VK_ADVANCED combo boxes
const int kAnisoValues[] = { 1, 2, 4, 8, 16 };
const int kMsaaValues[]  = { 1, 2, 4, 8 };

const char *DeviceTypeShortName(VkPhysicalDeviceType t)
{
	switch (t) {
	case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   return "Discrete";
	case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "Integrated";
	case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    return "Virtual";
	case VK_PHYSICAL_DEVICE_TYPE_CPU:            return "Software/CPU";
	default:                                     return "GPU";
	}
}
} // namespace

VideoTab::VideoTab(VulkanClient *gc, HINSTANCE _hInst, HINSTANCE _hOrbiterInst, HWND hVideoTab)
	: gclient(gc)
	, hInst(_hInst)
	, hOrbiterInst(_hOrbiterInst)
	, hTab(hVideoTab)
{
}

// ==============================================================
// Hardware & Display Discovery
// ==============================================================

bool VideoTab::ProbeVulkanAdapters(std::vector<VkAdapterInfo> &outAdapters, bool *outHasValidationLayer)
{
	outAdapters.clear();
	if (outHasValidationLayer)
		*outHasValidationLayer = false;

	if (!VkEnsureVolkInitialized())
		return false;

	// Check whether validation layers are available for this binary's architecture
	uint32_t layerCount = 0;
	if (vkEnumerateInstanceLayerProperties(&layerCount, nullptr) == VK_SUCCESS && layerCount > 0) {
		std::vector<VkLayerProperties> layers(layerCount);
		if (vkEnumerateInstanceLayerProperties(&layerCount, layers.data()) == VK_SUCCESS) {
			for (const auto &lp : layers) {
				if (std::strcmp(lp.layerName, "VK_LAYER_KHRONOS_validation") == 0) {
					if (outHasValidationLayer)
						*outHasValidationLayer = true;
					break;
				}
			}
		}
	}

	// Spin up a short-lived instance to enumerate physical devices without holding driver allocations
	VkApplicationInfo appInfo{};
	appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName   = "OpenOrbiter";
	appInfo.applicationVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
	appInfo.pEngineName        = "OVP-VulkanClient";
	appInfo.engineVersion      = VK_MAKE_API_VERSION(0, 1, 0, 0);
	appInfo.apiVersion         = VK_API_VERSION_1_3;

	const char *instanceExts[] = {
		VK_KHR_SURFACE_EXTENSION_NAME,
		VK_KHR_WIN32_SURFACE_EXTENSION_NAME
	};

	VkInstanceCreateInfo ici{};
	ici.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	ici.pApplicationInfo        = &appInfo;
	ici.enabledExtensionCount   = 2;
	ici.ppEnabledExtensionNames = instanceExts;

	VkInstance probeInst = VK_NULL_HANDLE;
	VkResult res = vkCreateInstance(&ici, nullptr, &probeInst);
	if (res != VK_SUCCESS || probeInst == VK_NULL_HANDLE) {
		VkLogErr("ProbeVulkanAdapters: vkCreateInstance failed (VkResult=%d)", (int)res);
		return false;
	}

	volkLoadInstanceOnly(probeInst);

	uint32_t gpuCount = 0;
	res = vkEnumeratePhysicalDevices(probeInst, &gpuCount, nullptr);
	if (res == VK_SUCCESS && gpuCount > 0) {
		std::vector<VkPhysicalDevice> gpus(gpuCount);
		if (vkEnumeratePhysicalDevices(probeInst, &gpuCount, gpus.data()) == VK_SUCCESS) {
			outAdapters.reserve(gpuCount);
			for (VkPhysicalDevice gpu : gpus) {
				VkPhysicalDeviceProperties props{};
				vkGetPhysicalDeviceProperties(gpu, &props);

				VkPhysicalDeviceMemoryProperties memProps{};
				vkGetPhysicalDeviceMemoryProperties(gpu, &memProps);

				uint64_t localBytes = 0;
				for (uint32_t h = 0; h < memProps.memoryHeapCount; ++h) {
					if (memProps.memoryHeaps[h].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
						localBytes += memProps.memoryHeaps[h].size;
				}

				VkAdapterInfo info{};
				info.name             = props.deviceName;
				info.deviceType       = props.deviceType;
				info.apiVersion       = props.apiVersion;
				info.driverVersion    = props.driverVersion;
				info.vendorID         = props.vendorID;
				info.deviceID         = props.deviceID;
				info.deviceLocalMB    = localBytes / (1024ULL * 1024ULL);
				info.supportsVulkan13 = (props.apiVersion >= VK_API_VERSION_1_3);

				char labelBuf[320];
				std::snprintf(labelBuf, sizeof(labelBuf), "%s (%s, %llu MB%s)",
					props.deviceName,
					DeviceTypeShortName(props.deviceType),
					(unsigned long long)info.deviceLocalMB,
					info.supportsVulkan13 ? "" : ", <VK 1.3");
				info.label = labelBuf;

				outAdapters.push_back(std::move(info));
			}
		}
	}

	vkDestroyInstance(probeInst, nullptr);
	return !outAdapters.empty();
}

void VideoTab::ProbeDisplayModes(std::vector<VkDisplayModeInfo> &outModes, VkDisplayModeInfo *outDesktopMode)
{
	outModes.clear();

	// Record current desktop mode
	DEVMODEA curDm{};
	curDm.dmSize = sizeof(curDm);
	if (EnumDisplaySettingsA(nullptr, ENUM_CURRENT_SETTINGS, &curDm)) {
		if (outDesktopMode) {
			outDesktopMode->width       = curDm.dmPelsWidth;
			outDesktopMode->height      = curDm.dmPelsHeight;
			outDesktopMode->refreshRate = curDm.dmDisplayFrequency;
		}
	} else if (outDesktopMode) {
		outDesktopMode->width       = (uint32_t)GetSystemMetrics(SM_CXSCREEN);
		outDesktopMode->height      = (uint32_t)GetSystemMetrics(SM_CYSCREEN);
		outDesktopMode->refreshRate = 60;
	}

	// Enumerate unique 32-bpp display modes >= 640x480
	DEVMODEA dm{};
	dm.dmSize = sizeof(dm);
	for (DWORD i = 0; EnumDisplaySettingsA(nullptr, i, &dm); ++i) {
		if (dm.dmBitsPerPel < 32 || dm.dmPelsWidth < 640 || dm.dmPelsHeight < 480)
			continue;

		VkDisplayModeInfo m{ dm.dmPelsWidth, dm.dmPelsHeight, dm.dmDisplayFrequency };
		bool exists = false;
		for (const auto &existing : outModes) {
			if (existing.width == m.width && existing.height == m.height && existing.refreshRate == m.refreshRate) {
				exists = true;
				break;
			}
		}
		if (!exists)
			outModes.push_back(m);
	}

	std::sort(outModes.begin(), outModes.end(), [](const VkDisplayModeInfo &a, const VkDisplayModeInfo &b) {
		if (a.width != b.width) return a.width < b.width;
		if (a.height != b.height) return a.height < b.height;
		return a.refreshRate < b.refreshRate;
	});

	if (outModes.empty() && outDesktopMode && outDesktopMode->width > 0)
		outModes.push_back(*outDesktopMode);
}

// ==============================================================
// Launchpad Video Tab Initialisation & Event Handling
// ==============================================================

bool VideoTab::Initialise()
{
	if (!hTab)
		return true;

	GraphicsClient::VIDEODATA *data = gclient->GetVideoData();
	data->forceenum  = false;
	data->trystencil = false;

	// Reset combo boxes
	SendDlgItemMessageA(hTab, IDC_VID_DEVICE, CB_RESETCONTENT, 0, 0);
	SendDlgItemMessageA(hTab, IDC_VID_MODE,   CB_RESETCONTENT, 0, 0);
	SendDlgItemMessageA(hTab, IDC_VID_BPP,    CB_RESETCONTENT, 0, 0);

	if (!ProbeVulkanAdapters(adapters, &validationLayerInstalled)) {
		VkLogErr("VideoTab::Initialise: No Vulkan-capable physical devices found");
		MessageBoxA(hTab,
			"No Vulkan 1.3 capable graphics adapters were detected on this system.",
			"VulkanClient Error", MB_OK | MB_ICONERROR);
		return false;
	}

	for (size_t i = 0; i < adapters.size(); ++i) {
		const auto &ad = adapters[i];
		VkLogAlw("Adapter %zu: %s [VK %u.%u.%u, VRAM %llu MB]",
			i, ad.name.c_str(),
			VK_API_VERSION_MAJOR(ad.apiVersion),
			VK_API_VERSION_MINOR(ad.apiVersion),
			VK_API_VERSION_PATCH(ad.apiVersion),
			(unsigned long long)ad.deviceLocalMB);
		SendDlgItemMessageA(hTab, IDC_VID_DEVICE, CB_ADDSTRING, 0, (LPARAM)ad.label.c_str());
	}

	// Default to first discrete Vulkan 1.3+ GPU if unset or out of range
	if (data->deviceidx < 0 || data->deviceidx >= (int)adapters.size()) {
		data->deviceidx = 0;
		for (size_t i = 0; i < adapters.size(); ++i) {
			if (adapters[i].supportsVulkan13 && adapters[i].deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
				data->deviceidx = (int)i;
				break;
			}
		}
	}
	selectedAdapterIdx = data->deviceidx;
	SendDlgItemMessageA(hTab, IDC_VID_DEVICE, CB_SETCURSEL, data->deviceidx, 0);

	// Populate display modes
	ProbeDisplayModes(displayModes, &desktopMode);
	for (size_t k = 0; k < displayModes.size(); ++k) {
		char cbuf[64];
		std::snprintf(cbuf, sizeof(cbuf), "%u x %u  %uHz",
			displayModes[k].width, displayModes[k].height, displayModes[k].refreshRate);
		SendDlgItemMessageA(hTab, IDC_VID_MODE, CB_ADDSTRING, 0, (LPARAM)cbuf);
		SendDlgItemMessageA(hTab, IDC_VID_MODE, CB_SETITEMDATA, k,
			(LPARAM)((displayModes[k].height << 16) | (displayModes[k].width & 0xFFFF)));
	}

	// Default to desktop native resolution
	if (data->modeidx < 0 || data->modeidx >= (int)displayModes.size()) {
		data->modeidx = (int)displayModes.size() - 1;
		for (size_t k = 0; k < displayModes.size(); ++k) {
			if (displayModes[k].width == desktopMode.width &&
				displayModes[k].height == desktopMode.height) {
				data->modeidx = (int)k;
				if (displayModes[k].refreshRate == desktopMode.refreshRate)
					break;
			}
		}
	}
	SendDlgItemMessageA(hTab, IDC_VID_MODE, CB_SETCURSEL, data->modeidx, 0);

	// Repurpose legacy BPP combo box for fullscreen presentation style
	SendDlgItemMessageA(hTab, IDC_VID_BPP, CB_ADDSTRING, 0, (LPARAM)"Borderless Full Screen");
	SendDlgItemMessageA(hTab, IDC_VID_BPP, CB_ADDSTRING, 0, (LPARAM)"Full Screen Window");
	SendDlgItemMessageA(hTab, IDC_VID_BPP, CB_ADDSTRING, 0, (LPARAM)"Window with Taskbar");
	if (data->style < 0 || data->style > 2)
		data->style = 1;
	SendDlgItemMessageA(hTab, IDC_VID_BPP, CB_SETCURSEL, data->style, 0);

	SetWindowTextA(GetDlgItem(hTab, IDC_VID_STATIC6), "Full Screen Mode");
	SendDlgItemMessageA(hTab, IDC_VID_VSYNC, BM_SETCHECK, data->novsync ? BST_CHECKED : BST_UNCHECKED, 0);

	if (data->winw < 320) data->winw = 1280;
	if (data->winh < 200) data->winh = 720;

	// Populate window dimensions with recursion guard
	updatingDimensions = true;
	SetWindowTextA(GetDlgItem(hTab, IDC_VID_WIDTH),  std::to_string(data->winw).c_str());
	SetWindowTextA(GetDlgItem(hTab, IDC_VID_HEIGHT), std::to_string(data->winh).c_str());
	updatingDimensions = false;

	// Aspect ratio matching
	int matchedAspect = 0;
	if (data->winw == (4 * data->winh) / 3 || data->winh == (3 * data->winw) / 4)
		matchedAspect = 1;
	else if (data->winw == (16 * data->winh) / 10 || data->winh == (10 * data->winw) / 16)
		matchedAspect = 2;
	else if (data->winw == (16 * data->winh) / 9 || data->winh == (9 * data->winw) / 16)
		matchedAspect = 3;

	SendDlgItemMessageA(hTab, IDC_VID_ASPECT, BM_SETCHECK, matchedAspect ? BST_CHECKED : BST_UNCHECKED, 0);
	aspectIdx = matchedAspect ? (matchedAspect - 1) : 2;
	for (int i = 0; i < 3; ++i)
		SendDlgItemMessageA(hTab, IDC_VID_4X3 + i, BM_SETCHECK, (i == aspectIdx) ? BST_CHECKED : BST_UNCHECKED, 0);

	SendDlgItemMessageA(hTab, IDC_VID_ENUM,     BM_SETCHECK, BST_UNCHECKED, 0);
	SendDlgItemMessageA(hTab, IDC_VID_STENCIL,  BM_SETCHECK, BST_UNCHECKED, 0);
	SendDlgItemMessageA(hTab, IDC_VID_PAGEFLIP, BM_SETCHECK, data->pageflip ? BST_CHECKED : BST_UNCHECKED, 0);

	SelectFullscreen(data->fullscreen);

	ShowWindow(GetDlgItem(hTab, IDC_VID_INFO), SW_SHOW);
	SetWindowTextA(GetDlgItem(hTab, IDC_VID_INFO), "Advanced");

	return true;
}

INT_PTR VideoTab::WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	(void)lParam;
	GraphicsClient::VIDEODATA *data = gclient->GetVideoData();

	switch (uMsg) {
	case WM_INITDIALOG:
		return TRUE;

	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDC_VID_DEVICE:
			if (HIWORD(wParam) == CBN_SELCHANGE) {
				int idx = (int)SendDlgItemMessageA(hWnd, IDC_VID_DEVICE, CB_GETCURSEL, 0, 0);
				if (idx != CB_ERR) SelectAdapter(idx);
				return TRUE;
			}
			break;

		case IDC_VID_MODE:
			if (HIWORD(wParam) == CBN_SELCHANGE) {
				int idx = (int)SendDlgItemMessageA(hWnd, IDC_VID_MODE, CB_GETCURSEL, 0, 0);
				if (idx != CB_ERR) SelectMode(idx);
				return TRUE;
			}
			break;

		case IDC_VID_BPP:
			if (HIWORD(wParam) == CBN_SELCHANGE) {
				int s = (int)SendDlgItemMessageA(hWnd, IDC_VID_BPP, CB_GETCURSEL, 0, 0);
				if (s != CB_ERR) data->style = s;
				SelectFullscreen(data->fullscreen);
				return TRUE;
			}
			break;

		case IDC_VID_FULL:
			if (HIWORD(wParam) == BN_CLICKED) {
				data->fullscreen = true;
				SelectFullscreen(true);
				return TRUE;
			}
			break;

		case IDC_VID_WINDOW:
			if (HIWORD(wParam) == BN_CLICKED) {
				data->fullscreen = false;
				SelectFullscreen(false);
				return TRUE;
			}
			break;

		case IDC_VID_WIDTH:
			if (HIWORD(wParam) == EN_CHANGE && !updatingDimensions) {
				SelectWidth();
				return TRUE;
			}
			break;

		case IDC_VID_HEIGHT:
			if (HIWORD(wParam) == EN_CHANGE && !updatingDimensions) {
				SelectHeight();
				return TRUE;
			}
			break;

		case IDC_VID_ASPECT:
			if (HIWORD(wParam) == BN_CLICKED) {
				SelectWidth();
				return TRUE;
			}
			break;

		case IDC_VID_4X3:
		case IDC_VID_16X10:
		case IDC_VID_16X9:
			if (HIWORD(wParam) == BN_CLICKED) {
				aspectIdx = LOWORD(wParam) - IDC_VID_4X3;
				SelectWidth();
				return TRUE;
			}
			break;

		case IDC_VID_INFO:
			DialogBoxParamA(hInst, MAKEINTRESOURCEA(IDD_VK_ADVANCED), hTab, AdvancedDlgProcWrp, (LPARAM)this);
			return TRUE;
		}
		break;
	}

	return FALSE;
}

void VideoTab::SelectAdapter(int index)
{
	if (index < 0 || index >= (int)adapters.size())
		return;
	selectedAdapterIdx = index;
	gclient->GetVideoData()->deviceidx = index;
}

void VideoTab::SelectMode(int index)
{
	if (index < 0 || index >= (int)displayModes.size())
		return;
	gclient->GetVideoData()->modeidx = index;
}

void VideoTab::SelectFullscreen(bool bFull)
{
	SetWindowTextA(GetDlgItem(hTab, IDC_VID_ENUM),     "(unused)");
	SetWindowTextA(GetDlgItem(hTab, IDC_VID_STENCIL),  "(unused)");
	SetWindowTextA(GetDlgItem(hTab, IDC_VID_PAGEFLIP), "Multiple displays");

	SendDlgItemMessageA(hTab, IDC_VID_FULL,   BM_SETCHECK, bFull ? BST_CHECKED : BST_UNCHECKED, 0);
	SendDlgItemMessageA(hTab, IDC_VID_WINDOW, BM_SETCHECK, bFull ? BST_UNCHECKED : BST_CHECKED, 0);

	EnableWindow(GetDlgItem(hTab, IDC_VID_ENUM),    FALSE);
	EnableWindow(GetDlgItem(hTab, IDC_VID_STENCIL), FALSE);
	EnableWindow(GetDlgItem(hTab, IDC_VID_VSYNC),   TRUE);

	EnableWindow(GetDlgItem(hTab, IDC_VID_ASPECT), !bFull);
	EnableWindow(GetDlgItem(hTab, IDC_VID_WIDTH),  !bFull);
	EnableWindow(GetDlgItem(hTab, IDC_VID_HEIGHT), !bFull);
	EnableWindow(GetDlgItem(hTab, IDC_VID_4X3),    !bFull);
	EnableWindow(GetDlgItem(hTab, IDC_VID_16X10),  !bFull);
	EnableWindow(GetDlgItem(hTab, IDC_VID_16X9),   !bFull);

	EnableWindow(GetDlgItem(hTab, IDC_VID_MODE),     bFull);
	EnableWindow(GetDlgItem(hTab, IDC_VID_BPP),      bFull);
	EnableWindow(GetDlgItem(hTab, IDC_VID_PAGEFLIP), bFull);
}

void VideoTab::SelectWidth()
{
	if (SendDlgItemMessageA(hTab, IDC_VID_ASPECT, BM_GETCHECK, 0, 0) != BST_CHECKED)
		return;

	char cbuf[32] = {};
	GetWindowTextA(GetDlgItem(hTab, IDC_VID_WIDTH), cbuf, sizeof(cbuf));
	int w = std::atoi(cbuf);
	GetWindowTextA(GetDlgItem(hTab, IDC_VID_HEIGHT), cbuf, sizeof(cbuf));
	int h = std::atoi(cbuf);

	int wfac = kAspectW[aspectIdx], hfac = kAspectH[aspectIdx];
	if (w > 0 && w != (wfac * h) / hfac) {
		h = (hfac * w) / wfac;
		updatingDimensions = true;
		SetWindowTextA(GetDlgItem(hTab, IDC_VID_HEIGHT), std::to_string(h).c_str());
		updatingDimensions = false;
	}
}

void VideoTab::SelectHeight()
{
	if (SendDlgItemMessageA(hTab, IDC_VID_ASPECT, BM_GETCHECK, 0, 0) != BST_CHECKED)
		return;

	char cbuf[32] = {};
	GetWindowTextA(GetDlgItem(hTab, IDC_VID_WIDTH), cbuf, sizeof(cbuf));
	int w = std::atoi(cbuf);
	GetWindowTextA(GetDlgItem(hTab, IDC_VID_HEIGHT), cbuf, sizeof(cbuf));
	int h = std::atoi(cbuf);

	int wfac = kAspectW[aspectIdx], hfac = kAspectH[aspectIdx];
	if (h > 0 && h != (hfac * w) / wfac) {
		w = (wfac * h) / hfac;
		updatingDimensions = true;
		SetWindowTextA(GetDlgItem(hTab, IDC_VID_WIDTH), std::to_string(w).c_str());
		updatingDimensions = false;
	}
}

void VideoTab::UpdateConfigData()
{
	if (!hTab)
		return;

	GraphicsClient::VIDEODATA *data = gclient->GetVideoData();

	int devSel  = (int)SendDlgItemMessageA(hTab, IDC_VID_DEVICE, CB_GETCURSEL, 0, 0);
	int modeSel = (int)SendDlgItemMessageA(hTab, IDC_VID_MODE,   CB_GETCURSEL, 0, 0);
	int bppSel  = (int)SendDlgItemMessageA(hTab, IDC_VID_BPP,    CB_GETCURSEL, 0, 0);

	if (devSel  != CB_ERR) data->deviceidx = devSel;
	if (modeSel != CB_ERR) data->modeidx   = modeSel;
	if (bppSel  != CB_ERR) data->style     = bppSel;

	data->fullscreen = (SendDlgItemMessageA(hTab, IDC_VID_FULL,     BM_GETCHECK, 0, 0) == BST_CHECKED);
	data->novsync    = (SendDlgItemMessageA(hTab, IDC_VID_VSYNC,    BM_GETCHECK, 0, 0) == BST_CHECKED);
	data->pageflip   = (SendDlgItemMessageA(hTab, IDC_VID_PAGEFLIP, BM_GETCHECK, 0, 0) == BST_CHECKED);
	data->trystencil = false;
	data->forceenum  = false;

	char cbuf[32] = {};
	GetWindowTextA(GetDlgItem(hTab, IDC_VID_WIDTH), cbuf, sizeof(cbuf));
	int w = std::atoi(cbuf);
	GetWindowTextA(GetDlgItem(hTab, IDC_VID_HEIGHT), cbuf, sizeof(cbuf));
	int h = std::atoi(cbuf);

	data->winw = (w >= 320) ? w : 1280;
	data->winh = (h >= 200) ? h : 720;

	if (g_vkConfig)
		g_vkConfig->WriteParams();
}

// ==============================================================
// "Advanced" Configuration Modal Dialog (IDD_VK_ADVANCED)
// ==============================================================

INT_PTR CALLBACK VideoTab::AdvancedDlgProcWrp(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (uMsg == WM_INITDIALOG)
		SetWindowLongPtrA(hWnd, GWLP_USERDATA, lParam);

	VideoTab *self = reinterpret_cast<VideoTab *>(GetWindowLongPtrA(hWnd, GWLP_USERDATA));
	return self ? self->AdvancedDlgProc(hWnd, uMsg, wParam, lParam) : FALSE;
}

INT_PTR VideoTab::AdvancedDlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	(void)lParam;
	switch (uMsg) {
	case WM_INITDIALOG: {
		if (!g_vkConfig)
			return TRUE;

		const char *anisoLabels[] = { "Off (1x)", "2x", "4x", "8x", "16x" };
		int anisoSel = 3;
		for (int i = 0; i < 5; ++i) {
			SendDlgItemMessageA(hWnd, IDC_VK_ANISO, CB_ADDSTRING, 0, (LPARAM)anisoLabels[i]);
			if (g_vkConfig->Anisotropy == kAnisoValues[i])
				anisoSel = i;
		}
		SendDlgItemMessageA(hWnd, IDC_VK_ANISO, CB_SETCURSEL, anisoSel, 0);

		const char *msaaLabels[] = { "None (1x)", "2x MSAA", "4x MSAA", "8x MSAA" };
		int msaaSel = 0;
		for (int i = 0; i < 4; ++i) {
			SendDlgItemMessageA(hWnd, IDC_VK_MSAA, CB_ADDSTRING, 0, (LPARAM)msaaLabels[i]);
			if (g_vkConfig->SceneMSAA == kMsaaValues[i])
				msaaSel = i;
		}
		SendDlgItemMessageA(hWnd, IDC_VK_MSAA, CB_SETCURSEL, msaaSel, 0);

		const char *dbgLabels[] = {
			"0 - Errors Only",
			"1 - Warnings",
			"2 - Info (Default)",
			"3 - Verbose / Orbiter.log",
			"4 - Full Debug Trace"
		};
		for (int i = 0; i < 5; ++i)
			SendDlgItemMessageA(hWnd, IDC_VK_DBGLVL, CB_ADDSTRING, 0, (LPARAM)dbgLabels[i]);
		SendDlgItemMessageA(hWnd, IDC_VK_DBGLVL, CB_SETCURSEL, std::clamp(g_vkConfig->DebugLvl, 0, 4), 0);

		SendDlgItemMessageA(hWnd, IDC_VK_FLATS,      BM_SETCHECK, g_vkConfig->bFlats ? BST_CHECKED : BST_UNCHECKED, 0);
		SendDlgItemMessageA(hWnd, IDC_VK_VALIDATION, BM_SETCHECK, g_vkConfig->ValidationLayers ? BST_CHECKED : BST_UNCHECKED, 0);
		SendDlgItemMessageA(hWnd, IDC_VK_FPSLIMIT,   BM_SETCHECK, g_vkConfig->EnableLimiter ? BST_CHECKED : BST_UNCHECKED, 0);

		char fpsBuf[32];
		std::snprintf(fpsBuf, sizeof(fpsBuf), "%.0f", g_vkConfig->FrameRate);
		SetWindowTextA(GetDlgItem(hWnd, IDC_VK_FPSVAL), fpsBuf);
		EnableWindow(GetDlgItem(hWnd, IDC_VK_FPSVAL), g_vkConfig->EnableLimiter ? TRUE : FALSE);

		if (selectedAdapterIdx >= 0 && selectedAdapterIdx < (int)adapters.size()) {
			const auto &ad = adapters[selectedAdapterIdx];
			char devBuf[256];
			std::snprintf(devBuf, sizeof(devBuf), "Selected GPU: %s (Vulkan %u.%u.%u) | Validation Layer Installed: %s",
				ad.name.c_str(),
				VK_API_VERSION_MAJOR(ad.apiVersion),
				VK_API_VERSION_MINOR(ad.apiVersion),
				VK_API_VERSION_PATCH(ad.apiVersion),
				validationLayerInstalled ? "Yes" : "No (x86/host)");
			SetWindowTextA(GetDlgItem(hWnd, IDC_VK_DEVINFO), devBuf);
		}
		return TRUE;
	}

	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDC_VK_FPSLIMIT:
			if (HIWORD(wParam) == BN_CLICKED) {
				BOOL en = (SendDlgItemMessageA(hWnd, IDC_VK_FPSLIMIT, BM_GETCHECK, 0, 0) == BST_CHECKED);
				EnableWindow(GetDlgItem(hWnd, IDC_VK_FPSVAL), en);
				return TRUE;
			}
			break;

		case IDOK:
			if (g_vkConfig) {
				int anisoIdx = (int)SendDlgItemMessageA(hWnd, IDC_VK_ANISO,  CB_GETCURSEL, 0, 0);
				int msaaIdx  = (int)SendDlgItemMessageA(hWnd, IDC_VK_MSAA,   CB_GETCURSEL, 0, 0);
				int dbgIdx   = (int)SendDlgItemMessageA(hWnd, IDC_VK_DBGLVL, CB_GETCURSEL, 0, 0);

				if (anisoIdx >= 0 && anisoIdx < 5) g_vkConfig->Anisotropy = kAnisoValues[anisoIdx];
				if (msaaIdx  >= 0 && msaaIdx  < 4) g_vkConfig->SceneMSAA  = kMsaaValues[msaaIdx];
				if (dbgIdx   >= 0 && dbgIdx   < 5) {
					g_vkConfig->DebugLvl = dbgIdx;
					VkLogSetLevel(dbgIdx);
				}

				g_vkConfig->bFlats           = (SendDlgItemMessageA(hWnd, IDC_VK_FLATS,      BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;
				g_vkConfig->ValidationLayers = (SendDlgItemMessageA(hWnd, IDC_VK_VALIDATION, BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;
				g_vkConfig->EnableLimiter    = (SendDlgItemMessageA(hWnd, IDC_VK_FPSLIMIT,   BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;

				char fpsBuf[32] = {};
				GetWindowTextA(GetDlgItem(hWnd, IDC_VK_FPSVAL), fpsBuf, sizeof(fpsBuf));
				double fps = std::atof(fpsBuf);
				if (fps >= 10.0 && fps <= 1000.0)
					g_vkConfig->FrameRate = fps;

				g_vkConfig->WriteParams();
			}
			EndDialog(hWnd, IDOK);
			return TRUE;

		case IDCANCEL:
			EndDialog(hWnd, IDCANCEL);
			return TRUE;
		}
		break;
	}
	return FALSE;
}
