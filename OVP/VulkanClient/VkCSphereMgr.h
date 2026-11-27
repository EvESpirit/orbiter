// ==============================================================
// VkCSphereMgr.h
// Tiled celestial background & star-image sphere manager
// Part of the ORBITER VISUALISATION PROJECT (OVP) - VulkanClient
// Licensed under the MIT License
// ==============================================================

#ifndef __VKCLIENT_VKCSPHEREMGR_H
#define __VKCLIENT_VKCSPHEREMGR_H

#include "VkAPI.h"
#include "VkMath.h"
#include "VkSpherePatch.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

class VkContext;
class VkSurfNative;
class VkScene;
namespace oapi { class VulkanClient; }

/**
 * \brief Loads and renders a multi-resolution `.tex` skybox archive on the celestial sphere.
 */
class VkCSphereMgr {
public:
	VkCSphereMgr() = default;
	~VkCSphereMgr();

	VkCSphereMgr(const VkCSphereMgr &) = delete;
	VkCSphereMgr &operator=(const VkCSphereMgr &) = delete;

	bool Load(
		oapi::VulkanClient &client,
		const char *texRelPath,
		VkDescriptorSetLayout texSetLayout,
		VkSampler sampler,
		const oapi::FMATRIX4 &galToEcl);

	void Destroy(VkContext *ctx);

	bool IsLoaded() const { return !tiles.empty(); }
	const std::string &LoadedPath() const { return loadedPath; }

	void Render(
		VkCommandBuffer cmd,
		VkPipelineLayout pipeLayout,
		const VkScene *scene,
		const oapi::FVECTOR3 &colorScale,
		float alpha) const;

private:
	struct Tile {
		VkSurfNative *surf = nullptr;
		VkDescriptorSet descSet = VK_NULL_HANDLE;
		uint32_t bandIdx = 0;
		oapi::FMATRIX4 mWorld = VkMatIdentity();
		oapi::FVECTOR3 bndCenter{ 0.0f, 0.0f, 0.0f };
		float bndRadius = 1.0f;
	};

	bool ResolveTexFile(oapi::VulkanClient &client, const char *relPath, std::string &outFullPath) const;

	VkContext *ownerCtx = nullptr;
	VkDescriptorPool descPool = VK_NULL_HANDLE;
	std::vector<VBMESH> bands;
	std::vector<Tile> tiles;
	std::string loadedPath;
	int loadedLevel = 0;
};

#endif // !__VKCLIENT_VKCSPHEREMGR_H
