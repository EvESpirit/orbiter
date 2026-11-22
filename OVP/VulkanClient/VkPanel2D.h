// ==============================================================
// VkPanel2D.h
// Hardware 2D instrument panel, MFD & HUD mesh renderer
// Part of the ORBITER VISUALISATION PROJECT (OVP) - VulkanClient
// Licensed under the MIT License
// ==============================================================

#ifndef __VKCLIENT_VKPANEL2D_H
#define __VKCLIENT_VKPANEL2D_H

#include "VkAPI.h"
#include "GraphicsAPI.h"
#include <cstdint>
#include <unordered_map>

class VkContext;
class VkSwapchain;
class VkPadPipeline;

namespace oapi {
	class VulkanClient;
}

/**
 * \brief Renders CPU-updated 2D panel, glass cockpit, and HUD meshes into the
 * active scene target using transient ring-buffered vertex/index copies.
 */
class VkPanel2D {
public:
	VkPanel2D() = default;
	~VkPanel2D();

	VkPanel2D(const VkPanel2D &) = delete;
	VkPanel2D &operator=(const VkPanel2D &) = delete;

	bool Init(VkContext &context, VkSwapchain &swap, VkPadPipeline &padPipeline);
	void Destroy();

	bool IsInitialized() const { return ctx != nullptr && pipeLayout != VK_NULL_HANDLE; }

	void Render(
		oapi::VulkanClient &client,
		SURFHANDLE *hSurf,
		MESHHANDLE hMesh,
		MATRIX3 *T,
		float alpha,
		bool additive,
		SURFHANDLE defaultTex);

private:
	VkPipeline GetOrCreatePipeline(VkFormat colorFormat, bool additive);

	VkContext     *ctx = nullptr;
	VkSwapchain   *swapchain = nullptr;
	VkPadPipeline *padPipe = nullptr;

	VkShaderModule        vertShader = VK_NULL_HANDLE;
	VkShaderModule        fragShader = VK_NULL_HANDLE;
	VkDescriptorSetLayout setLayout  = VK_NULL_HANDLE;
	VkPipelineLayout      pipeLayout = VK_NULL_HANDLE;

	std::unordered_map<uint64_t, VkPipeline> pipelines;
};

#endif // !__VKCLIENT_VKPANEL2D_H
