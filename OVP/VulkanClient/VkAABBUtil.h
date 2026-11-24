// ==============================================================
// VkAABBUtil.h
// Axis-aligned bounding box, frustum culling and near-plane utilities
// Part of the ORBITER VISUALISATION PROJECT (OVP) - VulkanClient
// Licensed under the MIT License
// ==============================================================

#ifndef __VKCLIENT_VKAABBUTIL_H
#define __VKCLIENT_VKAABBUTIL_H

#include "VkMath.h"

struct VkBBox {
	oapi::FVECTOR4 min; // Local AABB min corner (w = 1)
	oapi::FVECTOR4 max; // Local AABB max corner (w = 1)
	oapi::FVECTOR4 bs;  // Bounding sphere: xyz = center, w = radius
};

void VkZeroAABB(VkBBox *box);
void VkInitAABB(VkBBox *box);
void VkAddPointAABB(VkBBox *box, const oapi::FVECTOR3 *v);
void VkUpdateAABB(VkBBox *box, const oapi::FMATRIX4 *m1 = nullptr, const oapi::FMATRIX4 *m2 = nullptr);
void VkAddAABB(const VkBBox *src, const oapi::FMATRIX4 *m, VkBBox *dst, bool reset = false);

oapi::FVECTOR4 VkLinearFieldOfView(const oapi::FMATRIX4 *mProj);
bool VkIsAABBVisible(const VkBBox *box, const oapi::FMATRIX4 *mWV, const oapi::FVECTOR4 *fov);
bool VkIsBSVisible(const VkBBox *box, const oapi::FMATRIX4 *mWV, const oapi::FVECTOR4 *fov);
void VkComputeMinMaxDistance(
	const VkBBox *box,
	const oapi::FMATRIX4 *mWV,
	const oapi::FVECTOR4 *fov,
	float *zmin,
	float *zmax,
	float *dmin);

float VkNearPlane(
	float nearpt,
	float farpt,
	float neardist,
	const oapi::FMATRIX4 *mProj,
	bool bParticles);

#endif // !__VKCLIENT_VKAABBUTIL_H
