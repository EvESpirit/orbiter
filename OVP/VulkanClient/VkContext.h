// ==============================================================
// VkContext.h
// Vulkan 1.3 instance, surface, device, queues and VMA allocator
// Part of the ORBITER VISUALISATION PROJECT (OVP) - VulkanClient
// Licensed under the MIT License
// ==============================================================

#ifndef __VKCLIENT_VKCONTEXT_H
#define __VKCLIENT_VKCONTEXT_H

#include "VkAPI.h"

/**
 * \brief Owns the per-session Vulkan objects shared by the whole client.
 *
 * Created in clbkCreateRenderWindow() and destroyed in clbkDestroyRenderWindow(),
 * after Orbiter has released all session surfaces and meshes.
 */
class VkContext {
public:
	VkContext() = default;
	~VkContext();

	VkContext(const VkContext &) = delete;
	VkContext &operator=(const VkContext &) = delete;

	/**
	 * \brief Create instance, Win32 surface, device, queues and allocator.
	 * \param hWnd render window
	 * \param deviceIdx Launchpad adapter index (vkEnumeratePhysicalDevices order)
	 * \param enableValidation request VK_LAYER_KHRONOS_validation if installed
	 * \return true on success; on failure all partially created objects are released
	 */
	bool Create(HWND hWnd, int deviceIdx, bool enableValidation);
	void Destroy();

	bool IsValid() const { return device.device != VK_NULL_HANDLE; }

	VkInstance Instance() const { return instance.instance; }
	VkSurfaceKHR Surface() const { return surface; }
	VkPhysicalDevice PhysicalDevice() const { return physicalDevice.physical_device; }
	VkDevice Device() const { return device.device; }
	const vkb::Device &VkbDevice() const { return device; }
	VmaAllocator Allocator() const { return allocator; }
	VkQueue GraphicsQueue() const { return graphicsQueue; }
	VkQueue PresentQueue() const { return presentQueue; }
	uint32_t GraphicsQueueFamily() const { return graphicsFamily; }
	uint32_t PresentQueueFamily() const { return presentFamily; }
	const VkPhysicalDeviceProperties &Properties() const { return physicalDevice.properties; }
	bool ValidationEnabled() const { return validationEnabled; }

private:
	bool CreateInstance(bool enableValidation);
	bool CreateSurface(HWND hWnd);
	bool SelectPhysicalDevice(int deviceIdx);
	bool CreateDevice();
	bool CreateAllocator();

	vkb::Instance instance;
	vkb::PhysicalDevice physicalDevice;
	vkb::Device device;
	VkSurfaceKHR surface = VK_NULL_HANDLE;
	VmaAllocator allocator = VK_NULL_HANDLE;
	VkQueue graphicsQueue = VK_NULL_HANDLE;
	VkQueue presentQueue = VK_NULL_HANDLE;
	uint32_t graphicsFamily = 0;
	uint32_t presentFamily = 0;
	bool validationEnabled = false;
};

#endif // !__VKCLIENT_VKCONTEXT_H
