// ==============================================================
// VkAABBUtil.cpp
// Axis-aligned bounding box, frustum culling and near-plane utilities
// Part of the ORBITER VISUALISATION PROJECT (OVP) - VulkanClient
// Licensed under the MIT License
// ==============================================================

#include "VkAABBUtil.h"
#include <algorithm>
#include <cmath>
#include <limits>

void VkZeroAABB(VkBBox *box)
{
	if (!box)
		return;
	box->min = oapi::FVECTOR4(0.0f, 0.0f, 0.0f, 1.0f);
	box->max = oapi::FVECTOR4(0.0f, 0.0f, 0.0f, 1.0f);
	box->bs  = oapi::FVECTOR4(0.0f, 0.0f, 0.0f, 0.0f);
}

void VkInitAABB(VkBBox *box)
{
	if (!box)
		return;
	constexpr float kHuge = 1e30f;
	box->min = oapi::FVECTOR4( kHuge,  kHuge,  kHuge, 1.0f);
	box->max = oapi::FVECTOR4(-kHuge, -kHuge, -kHuge, 1.0f);
	box->bs  = oapi::FVECTOR4(0.0f, 0.0f, 0.0f, 0.0f);
}

void VkAddPointAABB(VkBBox *box, const oapi::FVECTOR3 *v)
{
	if (!box || !v)
		return;
	box->min.x = (std::min)(box->min.x, v->x);
	box->min.y = (std::min)(box->min.y, v->y);
	box->min.z = (std::min)(box->min.z, v->z);
	box->max.x = (std::max)(box->max.x, v->x);
	box->max.y = (std::max)(box->max.y, v->y);
	box->max.z = (std::max)(box->max.z, v->z);
}

void VkUpdateAABB(VkBBox *box, const oapi::FMATRIX4 *m1, const oapi::FMATRIX4 *m2)
{
	if (!box)
		return;

	if (box->min.x > box->max.x || box->min.y > box->max.y || box->min.z > box->max.z) {
		VkZeroAABB(box);
		return;
	}

	const oapi::FVECTOR3 center(
		0.5f * (box->min.x + box->max.x),
		0.5f * (box->min.y + box->max.y),
		0.5f * (box->min.z + box->max.z));
	const oapi::FVECTOR3 halfExt(
		0.5f * (box->max.x - box->min.x),
		0.5f * (box->max.y - box->min.y),
		0.5f * (box->max.z - box->min.z));
	float radius = oapi::length(halfExt);

	if (m1 && m2) {
		const oapi::FMATRIX4 combined = VkMatMul(*m2, *m1);
		const oapi::FVECTOR3 tCenter = oapi::TransformCoord(center, combined);
		radius *= VkMatBSScaleFactor(&combined);
		box->bs = oapi::FVECTOR4(tCenter, radius);
	} else if (m1) {
		const oapi::FVECTOR3 tCenter = oapi::TransformCoord(center, *m1);
		radius *= VkMatBSScaleFactor(m1);
		box->bs = oapi::FVECTOR4(tCenter, radius);
	} else {
		box->bs = oapi::FVECTOR4(center, radius);
	}
}

void VkAddAABB(const VkBBox *src, const oapi::FMATRIX4 *m, VkBBox *dst, bool reset)
{
	if (!src || !dst)
		return;
	if (reset)
		VkInitAABB(dst);

	if (src->min.x > src->max.x)
		return;

	for (int ix = 0; ix < 2; ++ix) {
		for (int iy = 0; iy < 2; ++iy) {
			for (int iz = 0; iz < 2; ++iz) {
				oapi::FVECTOR3 corner(
					ix ? src->max.x : src->min.x,
					iy ? src->max.y : src->min.y,
					iz ? src->max.z : src->min.z);
				if (m)
					corner = oapi::TransformCoord(corner, *m);
				VkAddPointAABB(dst, &corner);
			}
		}
	}
}

oapi::FVECTOR4 VkLinearFieldOfView(const oapi::FMATRIX4 *mProj)
{
	if (!mProj || std::fabsf(mProj->m11) < 1e-6f || std::fabsf(mProj->m22) < 1e-6f)
		return oapi::FVECTOR4(1.0f, 1.0f, 1.4142135f, 1.4142135f);

	const float vw = 1.0f / std::fabsf(mProj->m11);
	const float vh = 1.0f / std::fabsf(mProj->m22);
	const float vwf = std::sqrtf(1.0f + vw * vw);
	const float vhf = std::sqrtf(1.0f + vh * vh);
	return oapi::FVECTOR4(vw, vh, vwf, vhf);
}

bool VkIsBSVisible(const VkBBox *box, const oapi::FMATRIX4 *mWV, const oapi::FVECTOR4 *fov)
{
	if (!box || !mWV || !fov)
		return true;

	const float r = box->bs.w * VkMatBSScaleFactor(mWV);
	const oapi::FVECTOR3 c = oapi::TransformCoord(VkVec3(box->bs), *mWV);

	if (c.z < -r)
		return false;
	const float az = (c.z < 0.0f) ? 0.0f : c.z;
	if (std::fabsf(c.y) - r * fov->w > fov->y * az)
		return false;
	if (std::fabsf(c.x) - r * fov->z > fov->x * az)
		return false;
	return true;
}

bool VkIsAABBVisible(const VkBBox *box, const oapi::FMATRIX4 *mWV, const oapi::FVECTOR4 *fov)
{
	if (!box || !mWV || !fov)
		return true;
	if (box->min.x > box->max.x)
		return false;

	if (!VkIsBSVisible(box, mWV, fov))
		return false;

	float minX =  std::numeric_limits<float>::max();
	float minY =  std::numeric_limits<float>::max();
	float minZ =  std::numeric_limits<float>::max();
	float maxX = -std::numeric_limits<float>::max();
	float maxY = -std::numeric_limits<float>::max();
	float maxZ = -std::numeric_limits<float>::max();

	for (int ix = 0; ix < 2; ++ix) {
		for (int iy = 0; iy < 2; ++iy) {
			for (int iz = 0; iz < 2; ++iz) {
				const oapi::FVECTOR3 corner(
					ix ? box->max.x : box->min.x,
					iy ? box->max.y : box->min.y,
					iz ? box->max.z : box->min.z);
				const oapi::FVECTOR3 tc = oapi::TransformCoord(corner, *mWV);
				minX = (std::min)(minX, tc.x);
				minY = (std::min)(minY, tc.y);
				minZ = (std::min)(minZ, tc.z);
				maxX = (std::max)(maxX, tc.x);
				maxY = (std::max)(maxY, tc.y);
				maxZ = (std::max)(maxZ, tc.z);
			}
		}
	}

	if (maxZ < 0.0f)
		return false;

	const float zCheck = (std::max)(0.0f, maxZ);
	if (minX > fov->x * zCheck || maxX < -fov->x * zCheck)
		return false;
	if (minY > fov->y * zCheck || maxY < -fov->y * zCheck)
		return false;
	return true;
}

void VkComputeMinMaxDistance(
	const VkBBox *box,
	const oapi::FMATRIX4 *mWV,
	const oapi::FVECTOR4 *fov,
	float *zmin,
	float *zmax,
	float *dmin)
{
	if (!box || !mWV || !fov || box->min.x > box->max.x)
		return;
	if (!VkIsAABBVisible(box, mWV, fov))
		return;

	float localZMin =  std::numeric_limits<float>::max();
	float localZMax = -std::numeric_limits<float>::max();
	float localDMin =  std::numeric_limits<float>::max();

	for (int ix = 0; ix < 2; ++ix) {
		for (int iy = 0; iy < 2; ++iy) {
			for (int iz = 0; iz < 2; ++iz) {
				const oapi::FVECTOR3 corner(
					ix ? box->max.x : box->min.x,
					iy ? box->max.y : box->min.y,
					iz ? box->max.z : box->min.z);
				const oapi::FVECTOR3 tc = oapi::TransformCoord(corner, *mWV);
				localZMin = (std::min)(localZMin, tc.z);
				localZMax = (std::max)(localZMax, tc.z);
				localDMin = (std::min)(localDMin, oapi::length(tc));
			}
		}
	}

	if (zmin) *zmin = (std::min)(*zmin, localZMin);
	if (zmax) *zmax = (std::max)(*zmax, localZMax);
	if (dmin) *dmin = (std::min)(*dmin, localDMin);
}

float VkNearPlane(
	float nearpt,
	float farpt,
	float neardist,
	const oapi::FMATRIX4 *mProj,
	bool bParticles)
{
	(void)mProj;
	float znear = 2.5f;
	if (nearpt < znear) {
		if (nearpt > 0.25f)
			znear = (std::max)(0.25f, nearpt * 0.85f);
		else if (neardist < 1e9f)
			znear = (std::max)(0.1f, (std::min)(neardist * 0.5f, 1.0f));
	}
	if (farpt > 0.0f) {
		const float minForPrecision = farpt * 1e-6f;
		znear = (std::max)(znear, (std::min)(minForPrecision, 2.5f));
	}
	if (bParticles)
		znear = (std::min)(znear, 1.0f);
	return (std::max)(0.1f, znear);
}
