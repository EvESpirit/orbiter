// ==============================================================
// VkCelSphere.h
// Celestial sphere renderer (skybox, stars, constellations, grids & markers)
// Part of the ORBITER VISUALISATION PROJECT (OVP) - VulkanClient
// Licensed under the MIT License
// ==============================================================

#ifndef __VKCLIENT_VKCELSPHERE_H
#define __VKCLIENT_VKCELSPHERE_H

#include "CelSphereAPI.h"
#include "VkAPI.h"
#include "VkMath.h"
#include "VkSpherePatch.h"
#include "VkCSphereMgr.h"
#include <array>
#include <cstdint>
#include <memory>

class VkScene;
class VkSurfNative;

namespace oapi { class VulkanClient; }

/**
 * \brief Vulkan implementation of Orbiter's oapi::CelestialSphere.
 */
class VkCelSphere : public oapi::CelestialSphere {
public:
	VkCelSphere(oapi::VulkanClient *client, VkScene *scene);
	~VkCelSphere() override;

	VkCelSphere(const VkCelSphere &) = delete;
	VkCelSphere &operator=(const VkCelSphere &) = delete;

	bool Init();
	void Destroy();
	void SetViewportSize(DWORD w, DWORD h);
	void OnOptionChanged(int cat, int item);

	/**
	 * \brief Render 3D skybox tiles, star points, constellations, and grid overlays.
	 * Must be called inside an active scene dynamic rendering pass before opaque geometry.
	 */
	void Render(VkCommandBuffer cmd);

	/**
	 * \brief Render 2D Sketchpad constellation labels and celestial markers after the 3D scene pass.
	 */
	void RenderOverlay();

	bool EclDir2WindowPos(const VECTOR3 &dir, int &x, int &y) const override;

private:
	bool CreatePipelines();
	void DestroyPipelines();
	bool InitStars();
	bool InitConstellationLines();
	bool InitConstellationBoundaries();
	bool AllocGrids();
	bool AllocGridLabels();

	void RenderLineSet(
		VkCommandBuffer cmd,
		const VkGpuBuffer &vb,
		uint32_t firstVertex,
		uint32_t vertexCount,
		const oapi::FMATRIX4 &mModel,
		const oapi::FVECTOR4 &color) const;

	void RenderGrid(
		VkCommandBuffer cmd,
		const oapi::FMATRIX4 &mModel,
		const oapi::FVECTOR4 &baseCol,
		double elevRotAngle) const;

	void LoadBackgroundImageLayer();
	void LoadStarImageLayer();

	oapi::VulkanClient *gc = nullptr;
	VkScene *scene = nullptr;

	// Vulkan pipelines & descriptor layouts
	VkDescriptorSetLayout texSetLayout = VK_NULL_HANDLE;
	VkPipelineLayout pipeLayoutLine = VK_NULL_HANDLE;
	VkPipelineLayout pipeLayoutStar = VK_NULL_HANDLE;
	VkPipelineLayout pipeLayoutTex = VK_NULL_HANDLE;
	VkPipeline pipeLine = VK_NULL_HANDLE;
	VkPipeline pipeStar = VK_NULL_HANDLE;
	VkPipeline pipeLabel = VK_NULL_HANDLE;
	VkPipeline pipeCSphere = VK_NULL_HANDLE;

	// Stars
	VkGpuBuffer starVB;
	uint32_t starCount = 0;
	std::array<int, 256> starCutoffIdx{};

	// Constellation geometry
	VkGpuBuffer constLineVB;
	uint32_t constLineVtxCount = 0;
	VkGpuBuffer constBndVB;
	uint32_t constBndVtxCount = 0;

	// Celestial/ecliptic/galactic/local-horizon grid geometry
	VkGpuBuffer gridVB;
	uint32_t equatorOffset = 0;
	uint32_t equatorCount = 0;
	uint32_t minorGridOffset = 0;
	uint32_t minorGridCount = 0;

	// Grid tick label geometry + gridlabel.dds texture
	VkGpuBuffer gridLabelVB;
	VkGpuBuffer gridLabelIB;
	uint32_t azLabelFirstIdx = 0;
	uint32_t azLabelIdxCount = 0;
	uint32_t elLabelFirstIdx = 0;
	uint32_t elLabelIdxCount = 0;
	VkSurfNative *gridLabelTex = nullptr;
	VkDescriptorPool labelDescPool = VK_NULL_HANDLE;
	VkDescriptorSet labelDescSet = VK_NULL_HANDLE;

	// Cached Galactic -> Ecliptic matrix (row-vector convention)
	oapi::FMATRIX4 mGal2Ecl = VkMatIdentity();

	// Skybox layers
	std::unique_ptr<VkCSphereMgr> mgrBkgImage;
	std::unique_ptr<VkCSphereMgr> mgrStarImage;

	// Cached Orbiter celestial sphere options
	DWORD viewW = 0;
	DWORD viewH = 0;
	bool useStarDots = true;
	bool useStarImage = false;
	bool useBgImage = false;
	float bgIntens = 0.3f;
};

#endif // !__VKCLIENT_VKCELSPHERE_H
