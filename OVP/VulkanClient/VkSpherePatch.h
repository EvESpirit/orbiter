// ==============================================================
// VkSpherePatch.h
// Tessellated spherical patch geometry & static GPU buffer helpers
// Part of the ORBITER VISUALISATION PROJECT (OVP) - VulkanClient
// Licensed under the MIT License
// ==============================================================

#ifndef __VKCLIENT_VKSPHEREPATCH_H
#define __VKCLIENT_VKSPHEREPATCH_H

#include "VkAPI.h"
#include "VkVertex.h"
#include "DrawAPI.h"
#include <cstdint>
#include <vector>

class VkContext;

/**
 * \brief Static device-local GPU vertex/index buffer helper.
 */
struct VkGpuBuffer {
	VkBuffer buffer = VK_NULL_HANDLE;
	VmaAllocation alloc = VK_NULL_HANDLE;
	VkDeviceSize size = 0;

	bool UploadStatic(VkContext &ctx, const void *data, VkDeviceSize byteSize, VkBufferUsageFlags usage);
	void Destroy(VkContext &ctx);
};

/**
 * \brief Create a VkShaderModule from precompiled SPIR-V bytecode words.
 */
VkShaderModule VkCreateShaderModuleFromBytes(VkDevice device, const void *spvBytes, size_t byteSize);

/**
 * \brief Vertex + index mesh patch on the unit (or scaled) sphere.
 */
struct VBMESH {
	std::vector<VkVertex2Tex> vtx;
	std::vector<uint16_t> idx;
	uint32_t nv = 0;
	uint32_t ni = 0;
	VkGpuBuffer vb;
	VkGpuBuffer ib;
	oapi::FVECTOR4 bnd{ 0.0f, 0.0f, 0.0f, 1.0f }; ///< Bounding sphere (center xyz, radius w)

	bool Upload(VkContext &ctx);
	void Destroy(VkContext &ctx);
	void ClearCpu();
};

/**
 * \brief Create a full latitude-longitude sphere covering [-Pi, +Pi] x [-Pi/2, +Pi/2].
 * \param inwardWinding true when viewed from inside (skybox/celestial sphere)
 */
VBMESH VkCreateSphere(
	VkContext &ctx,
	uint32_t segLon,
	uint32_t segLat,
	double radius = 1.0,
	bool inwardWinding = true);

/**
 * \brief Create a single latitude-longitude sphere patch centered around phi = 0 ([-dPhi/2, +dPhi/2]).
 * \param latMin southern latitude bound [rad]
 * \param latMax northern latitude bound [rad]
 * \param dPhi longitudinal width [rad]
 * \param segLon longitudinal subdivisions
 * \param segLat latitudinal subdivisions
 * \param radius sphere radius
 * \param inwardWinding true when viewed from inside (skybox/celestial sphere)
 */
VBMESH VkCreateSpherePatch(
	VkContext &ctx,
	double latMin,
	double latMax,
	double dPhi,
	uint32_t segLon,
	uint32_t segLat,
	double radius = 1.0,
	bool inwardWinding = true);

#endif // !__VKCLIENT_VKSPHEREPATCH_H
