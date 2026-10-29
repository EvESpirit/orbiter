// ==============================================================
// VkAPI.h
// Part of the ORBITER VISUALISATION PROJECT (OVP) - VulkanClient
// Licensed under the MIT License
// ==============================================================

#ifndef __VKCLIENT_VKAPI_H
#define __VKCLIENT_VKAPI_H

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
#include <volk.h>

#pragma warning(push)
#pragma warning(disable : 4100 4127 4189 4324 4505)
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include <vk_mem_alloc.h>
#include <VkBootstrap.h>
#pragma warning(pop)

// Load vulkan-1.dll via volk once per process
bool VkEnsureVolkInitialized();

#endif // !__VKCLIENT_VKAPI_H
