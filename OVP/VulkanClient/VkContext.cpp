// ==============================================================
// VkContext.cpp
// Vulkan 1.3 instance, surface, device, queues and VMA allocator
// Part of the ORBITER VISUALISATION PROJECT (OVP) - VulkanClient
// Licensed under the MIT License
// ==============================================================

#include "VkContext.h"
#include "VkLog.h"
#include <vector>

namespace {

constexpr const char *kValidationLayer = "VK_LAYER_KHRONOS_validation";

VKAPI_ATTR VkBool32 VKAPI_CALL DebugMessengerCallback(
	VkDebugUtilsMessageSeverityFlagBitsEXT severity,
	VkDebugUtilsMessageTypeFlagsEXT type,
	const VkDebugUtilsMessengerCallbackDataEXT *data,
	void *userData)
{
	(void)userData;
	const char *typeStr = vkb::to_string_message_type(type);
	const char *msg = (data && data->pMessage) ? data->pMessage : "(null)";
	if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
		VkLogErr("[%s] %s", typeStr, msg);
	else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
		VkLogWarn("[%s] %s", typeStr, msg);
	else
		VkLogDbg("[%s] %s", typeStr, msg);
	return VK_FALSE;
}

const char *DeviceTypeName(VkPhysicalDeviceType t)
{
	switch (t) {
	case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "integrated";
	case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   return "discrete";
	case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    return "virtual";
	case VK_PHYSICAL_DEVICE_TYPE_CPU:            return "cpu";
	default:                                     return "other";
	}
}

} // namespace

VkContext::~VkContext()
{
	Destroy();
}

bool VkContext::Create(HWND hWnd, int deviceIdx, bool enableValidation)
{
	Destroy();

	if (!VkEnsureVolkInitialized())
		return false;

	if (CreateInstance(enableValidation) &&
		CreateSurface(hWnd) &&
		SelectPhysicalDevice(deviceIdx) &&
		CreateDevice() &&
		CreateAllocator())
	{
		return true;
	}

	Destroy();
	return false;
}

void VkContext::Destroy()
{
	if (device.device != VK_NULL_HANDLE)
		vkDeviceWaitIdle(device.device);

	if (allocator != VK_NULL_HANDLE) {
		vmaDestroyAllocator(allocator);
		allocator = VK_NULL_HANDLE;
	}
	if (device.device != VK_NULL_HANDLE) {
		vkb::destroy_device(device);
		device = vkb::Device{};
	}
	graphicsQueue = presentQueue = VK_NULL_HANDLE;
	graphicsFamily = presentFamily = 0;
	physicalDevice = vkb::PhysicalDevice{};

	if (surface != VK_NULL_HANDLE) {
		vkb::destroy_surface(instance, surface);
		surface = VK_NULL_HANDLE;
	}
	if (instance.instance != VK_NULL_HANDLE) {
		vkb::destroy_instance(instance); // also destroys the debug messenger
		instance = vkb::Instance{};
		VkLogInfo("Vulkan context destroyed");
	}
	validationEnabled = false;
}

bool VkContext::CreateInstance(bool enableValidation)
{
	auto sysRet = vkb::SystemInfo::get_system_info(vkGetInstanceProcAddr);
	if (!sysRet) {
		VkLogErr("vkb::SystemInfo failed: %s", sysRet.error().message().c_str());
		return false;
	}
	const vkb::SystemInfo &sys = sysRet.value();

	// The debug messenger is only valid together with the layer that provides
	// VK_EXT_debug_utils. Note that LunarG SDKs >= 1.4.304.1 ship no 32-bit
	// validation layer, so on Win32 builds this usually stays disabled.
	bool useValidation = false;
	if (enableValidation) {
		if (sys.is_layer_available(kValidationLayer) &&
			sys.is_extension_available(VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
		{
			useValidation = true;
		} else {
			VkLogWarn("Validation requested but %s is not installed for this architecture (%d-bit)",
				kValidationLayer, (int)(sizeof(void *) * 8));
		}
	}

	vkb::InstanceBuilder builder(vkGetInstanceProcAddr);
	builder.set_app_name("OpenOrbiter")
		.set_engine_name("OVP-VulkanClient")
		.require_api_version(1, 3, 0);

	if (useValidation) {
		builder.enable_validation_layers(true)
			.set_debug_callback(DebugMessengerCallback)
			.set_debug_messenger_severity(
				VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT);
	}

	auto instRet = builder.build();
	if (!instRet) {
		VkLogErr("Vulkan instance creation failed: %s (VkResult=%d)",
			instRet.error().message().c_str(), (int)instRet.vk_result());
		return false;
	}
	instance = instRet.value();
	validationEnabled = useValidation;
	volkLoadInstanceOnly(instance.instance);

	const uint32_t loaderVer = volkGetInstanceVersion();
	VkLogInfo("Vulkan instance created (loader %u.%u, requested API 1.3, validation %s)",
		VK_API_VERSION_MAJOR(loaderVer),
		VK_API_VERSION_MINOR(loaderVer),
		validationEnabled ? "ON" : "OFF");
	return true;
}

bool VkContext::CreateSurface(HWND hWnd)
{
	VkWin32SurfaceCreateInfoKHR sci{};
	sci.sType     = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
	sci.hinstance = (HINSTANCE)GetWindowLongPtr(hWnd, GWLP_HINSTANCE);
	sci.hwnd      = hWnd;

	VkResult res = vkCreateWin32SurfaceKHR(instance.instance, &sci, nullptr, &surface);
	if (res != VK_SUCCESS) {
		VkLogErr("vkCreateWin32SurfaceKHR failed (VkResult=%d)", (int)res);
		surface = VK_NULL_HANDLE;
		return false;
	}
	return true;
}

bool VkContext::SelectPhysicalDevice(int deviceIdx)
{
	// Map the Launchpad index (raw enumeration order, see VideoTab) to a handle
	VkPhysicalDevice wanted = VK_NULL_HANDLE;
	uint32_t count = 0;
	vkEnumeratePhysicalDevices(instance.instance, &count, nullptr);
	if (count > 0) {
		std::vector<VkPhysicalDevice> gpus(count);
		if (vkEnumeratePhysicalDevices(instance.instance, &count, gpus.data()) == VK_SUCCESS &&
			deviceIdx >= 0 && deviceIdx < (int)count)
		{
			wanted = gpus[deviceIdx];
		}
	}

	VkPhysicalDeviceVulkan13Features f13{};
	f13.sType            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
	f13.dynamicRendering = VK_TRUE;
	f13.synchronization2 = VK_TRUE;
	// VK_EXT_extended_dynamic_state is core in 1.3 and has no feature bit

	VkPhysicalDeviceFeatures f10{};
	f10.samplerAnisotropy = VK_TRUE;

	vkb::PhysicalDeviceSelector selector(instance);
	selector.set_surface(surface)
		.set_minimum_version(1, 3)
		.set_required_features_13(f13)
		.set_required_features(f10);

	auto listRet = selector.select_devices();
	if (!listRet || listRet.value().empty()) {
		VkLogErr("No suitable Vulkan 1.3 device: %s",
			listRet ? "empty list" : listRet.error().message().c_str());
		return false;
	}
	const std::vector<vkb::PhysicalDevice> &candidates = listRet.value();

	const vkb::PhysicalDevice *chosen = nullptr;
	for (const auto &pd : candidates) {
		if (pd.physical_device == wanted) {
			chosen = &pd;
			break;
		}
	}
	if (!chosen) {
		chosen = &candidates.front(); // vk-bootstrap orders by preference (discrete first)
		VkLogWarn("Launchpad adapter %d is unavailable or unsuitable; falling back to '%s'",
			deviceIdx, chosen->name.c_str());
	}
	physicalDevice = *chosen;

	const VkPhysicalDeviceProperties &p = physicalDevice.properties;
	VkLogAlw("Using adapter '%s' (%s, VK %u.%u.%u, driver 0x%08X)",
		p.deviceName, DeviceTypeName(p.deviceType),
		VK_API_VERSION_MAJOR(p.apiVersion), VK_API_VERSION_MINOR(p.apiVersion),
		VK_API_VERSION_PATCH(p.apiVersion), p.driverVersion);
	return true;
}

bool VkContext::CreateDevice()
{
	vkb::DeviceBuilder builder(physicalDevice);
	auto devRet = builder.build();
	if (!devRet) {
		VkLogErr("vkCreateDevice failed: %s (VkResult=%d)",
			devRet.error().message().c_str(), (int)devRet.vk_result());
		return false;
	}
	device = devRet.value();
	volkLoadDevice(device.device);

	auto gq  = device.get_queue(vkb::QueueType::graphics);
	auto gqi = device.get_queue_index(vkb::QueueType::graphics);
	auto pq  = device.get_queue(vkb::QueueType::present);
	auto pqi = device.get_queue_index(vkb::QueueType::present);
	if (!gq || !gqi || !pq || !pqi) {
		VkLogErr("Failed to retrieve graphics/present queues");
		return false;
	}
	graphicsQueue  = gq.value();
	graphicsFamily = gqi.value();
	presentQueue   = pq.value();
	presentFamily  = pqi.value();

	VkLogInfo("Logical device created (graphics family %u, present family %u)",
		graphicsFamily, presentFamily);
	return true;
}

bool VkContext::CreateAllocator()
{
	VmaVulkanFunctions funcs{};
	funcs.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
	funcs.vkGetDeviceProcAddr   = vkGetDeviceProcAddr;

	VmaAllocatorCreateInfo aci{};
	aci.physicalDevice   = physicalDevice.physical_device;
	aci.device           = device.device;
	aci.instance         = instance.instance;
	aci.vulkanApiVersion = VK_API_VERSION_1_3;
	aci.pVulkanFunctions = &funcs;

	VkResult res = vmaCreateAllocator(&aci, &allocator);
	if (res != VK_SUCCESS) {
		VkLogErr("vmaCreateAllocator failed (VkResult=%d)", (int)res);
		allocator = VK_NULL_HANDLE;
		return false;
	}
	return true;
}
