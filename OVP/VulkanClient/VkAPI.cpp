// ==============================================================
// VkAPI.cpp
// Part of the ORBITER VISUALISATION PROJECT (OVP) - VulkanClient
// Licensed under the MIT License
// ==============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR
#endif

#include <windows.h>
#include <mutex>

#pragma warning(push)
#pragma warning(disable : 4100 4127 4189 4324 4505 4701 4703)
#define VOLK_IMPLEMENTATION
#include <volk.h>

#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include <vk_mem_alloc.h>
#pragma warning(pop)

#include "VkAPI.h"
#include "VkLog.h"

namespace {
std::mutex g_volkMutex;
bool g_volkReady = false;
} // namespace

bool VkEnsureVolkInitialized()
{
	std::lock_guard<std::mutex> lock(g_volkMutex);
	if (g_volkReady)
		return true;

	VkResult res = volkInitialize();
	if (res != VK_SUCCESS) {
		VkLogErr("volkInitialize() failed (VkResult=%d)", (int)res);
		return false;
	}

	uint32_t loaderVer = volkGetInstanceVersion();
	VkLogInfo("volk initialized (Vulkan loader %u.%u.%u)",
		VK_API_VERSION_MAJOR(loaderVer),
		VK_API_VERSION_MINOR(loaderVer),
		VK_API_VERSION_PATCH(loaderVer));

	g_volkReady = true;
	return true;
}
