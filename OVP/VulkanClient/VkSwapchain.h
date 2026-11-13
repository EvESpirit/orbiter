// ==============================================================
// VkSwapchain.h
// Swapchain, persistent scene target and 2-frames-in-flight loop
// Part of the ORBITER VISUALISATION PROJECT (OVP) - VulkanClient
// Licensed under the MIT License
// ==============================================================

#ifndef __VKCLIENT_VKSWAPCHAIN_H
#define __VKCLIENT_VKSWAPCHAIN_H

#include "VkAPI.h"
#include <vector>

class VkContext;

/**
 * \brief Manages the presentation swapchain, persistent offscreen scene color
 * target (`VK_IMAGE_USAGE_TRANSFER_SRC_BIT`), and 2-frames-in-flight command streams.
 */
class VkSwapchain {
public:
	static constexpr uint32_t kMaxFramesInFlight = 2;

	VkSwapchain() = default;
	~VkSwapchain();

	VkSwapchain(const VkSwapchain &) = delete;
	VkSwapchain &operator=(const VkSwapchain &) = delete;

	bool Create(VkContext &context, uint32_t width, uint32_t height, bool disableVSync);
	void Destroy();

	bool Recreate(uint32_t width, uint32_t height);

	bool BeginFrame(HWND hWnd);
	void ClearScene(const VkClearColorValue &clearColor);
	bool FillScene(const RECT *rect, DWORD col);
	bool BeginSceneOverlayPass();
	void EndSceneOverlayPass();
	void EndFrame();
	bool Present(HWND hWnd);

	bool IsValid() const { return swapchain.swapchain != VK_NULL_HANDLE; }
	bool IsFrameRecording() const { return frameRecording; }

	VkFormat SwapchainFormat() const { return swapchain.image_format; }
	VkExtent2D Extent() const { return swapchain.extent; }
	uint32_t ImageCount() const { return swapchain.image_count; }
	VkPresentModeKHR PresentMode() const { return swapchain.present_mode; }

	VkImage SceneImage() const { return sceneImage; }
	VkImageView SceneImageView() const { return sceneView; }
	VkFormat SceneFormat() const { return sceneFormat; }
	VkExtent2D SceneExtent() const { return sceneExtent; }
	VkImageLayout SceneLayout() const { return sceneLayout; }

	VkCommandBuffer CurrentCommandBuffer() const { return frames[currentFrame].cmd; }
	uint32_t CurrentFrameIndex() const { return currentFrame; }

	VkCommandBuffer OffscreenCommandBuffer();
	VkCommandBuffer ActiveCommandBuffer() { return frameRecording ? frames[currentFrame].cmd : OffscreenCommandBuffer(); }
	uint32_t ActiveSlotIndex() const { return frameRecording ? currentFrame : kMaxFramesInFlight; }
	uint64_t SlotGeneration(uint32_t slot) const { return (slot < kMaxFramesInFlight) ? frames[slot].generation : offscreenGeneration; }
	void DeferStagingFree(VkBuffer buf, VmaAllocation alloc);
	bool FlushOffscreenCommands();

	bool ReadbackSceneTarget(std::vector<uint8_t> &outBgra, uint32_t &outWidth, uint32_t &outHeight);

	void TransitionSceneLayout(
		VkCommandBuffer cmd,
		VkImageLayout newLayout,
		VkPipelineStageFlags2 srcStage,
		VkAccessFlags2 srcAccess,
		VkPipelineStageFlags2 dstStage,
		VkAccessFlags2 dstAccess);

private:
	struct DeferredBuffer {
		VkBuffer buf = VK_NULL_HANDLE;
		VmaAllocation alloc = VK_NULL_HANDLE;
	};

	struct FrameSync {
		VkCommandPool cmdPool = VK_NULL_HANDLE;
		VkCommandBuffer cmd = VK_NULL_HANDLE;
		VkSemaphore imageAvailable = VK_NULL_HANDLE;
		VkFence inFlight = VK_NULL_HANDLE;
		uint64_t generation = 1;
		std::vector<DeferredBuffer> deferredStaging;
	};

	bool CreateSwapchainInternal(uint32_t width, uint32_t height);
	void DestroySwapchainInternal();

	bool CreateSceneTarget(uint32_t width, uint32_t height);
	void DestroySceneTarget();

	bool CreateFrameResources();
	void DestroyFrameResources();

	VkContext *ctx = nullptr;
	bool noVSync = false;

	vkb::Swapchain swapchain;
	std::vector<VkImage> images;
	std::vector<VkImageView> imageViews;
	std::vector<VkSemaphore> renderFinishedSemaphores;

	VkImage sceneImage = VK_NULL_HANDLE;
	VkImageView sceneView = VK_NULL_HANDLE;
	VmaAllocation sceneAlloc = VK_NULL_HANDLE;
	VkFormat sceneFormat = VK_FORMAT_B8G8R8A8_UNORM;
	VkExtent2D sceneExtent{ 0, 0 };
	VkImageLayout sceneLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	FrameSync frames[kMaxFramesInFlight]{};
	uint32_t currentFrame = 0;
	uint32_t currentImageIndex = 0;
	bool frameRecording = false;
	bool frameReadyToPresent = false;
	bool needsRecreate = false;

	VkCommandPool offscreenPool = VK_NULL_HANDLE;
	VkCommandBuffer cmdOffscreen = VK_NULL_HANDLE;
	VkFence offscreenFence = VK_NULL_HANDLE;
	uint64_t offscreenGeneration = 1;
	std::vector<DeferredBuffer> offscreenDeferredStaging;
	bool offscreenRecording = false;
	bool offscreenHasWork = false;
};

#endif // !__VKCLIENT_VKSWAPCHAIN_H
