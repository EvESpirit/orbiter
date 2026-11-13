// ==============================================================
// VkBlit.h
// Shader-driven 2D textured, scaled & color-keyed blit pipeline
// Part of the ORBITER VISUALISATION PROJECT (OVP) - VulkanClient
// Licensed under the MIT License
// ==============================================================

#ifndef __VKCLIENT_VKBLIT_H
#define __VKCLIENT_VKBLIT_H

#include "VkAPI.h"
#include <cstdint>
#include <unordered_map>
#include <vector>

class VkContext;

/**
 * \brief Graphics blit pipeline for format-converting, scaled, and color-keyed 2D surface blits.
 */
class VkBlitPipeline {
public:
	static constexpr uint32_t kNumSlots = 3; // 0..1 = frames in flight, 2 = cmdOffscreen

	VkBlitPipeline() = default;
	~VkBlitPipeline();

	VkBlitPipeline(const VkBlitPipeline &) = delete;
	VkBlitPipeline &operator=(const VkBlitPipeline &) = delete;

	bool Create(VkContext &context);
	void Destroy();

	bool Blit(
		VkCommandBuffer cmd,
		uint32_t slot,
		uint64_t slotGen,
		VkImageView srcView,
		uint32_t srcW,
		uint32_t srcH,
		VkImageView dstView,
		VkFormat dstFormat,
		uint32_t dstW,
		uint32_t dstH,
		const RECT &srcRect,
		const RECT &dstRect,
		bool useColorKey,
		DWORD colorKey,
		bool linearFilter);

private:
	struct SlotPools {
		uint64_t lastGeneration = 0;
		std::vector<VkDescriptorPool> pools;
		size_t activePoolIdx = 0;
		uint32_t usedInActivePool = 0;
	};

	VkPipeline GetOrCreatePipeline(VkFormat dstFormat);
	VkDescriptorSet AllocateDescriptorSet(uint32_t slot, uint64_t slotGen);

	VkContext *ctx = nullptr;
	VkShaderModule vertModule = VK_NULL_HANDLE;
	VkShaderModule fragModule = VK_NULL_HANDLE;
	VkSampler nearestSampler = VK_NULL_HANDLE;
	VkSampler linearSampler = VK_NULL_HANDLE;
	VkDescriptorSetLayout descSetLayout = VK_NULL_HANDLE;
	VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
	std::unordered_map<uint32_t, VkPipeline> pipelinesByFormat;
	SlotPools slotPools[kNumSlots]{};
};

#endif // !__VKCLIENT_VKBLIT_H
