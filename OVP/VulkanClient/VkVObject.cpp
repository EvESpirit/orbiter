// ==============================================================
// VkVObject.cpp
// Visual representation of an Orbiter scene object
// Part of the ORBITER VISUALISATION PROJECT (OVP) - VulkanClient
// Licensed under the MIT License
// ==============================================================

#include "VkVObject.h"
#include "VkScene.h"
#include "VkLog.h"
#include <algorithm>
#include <cmath>
#include <cstring>

oapi::VulkanClient *VkVObject::gc = nullptr;

void VkVObject::GlobalInit(oapi::VulkanClient *gclient)
{
	gc = gclient;
}

void VkVObject::GlobalExit()
{
	gc = nullptr;
}

VkVObject *VkVObject::Create(OBJHANDLE _hObj, const VkScene *scene)
{
	// Units 2.4+ specialize OBJTP_STAR -> VkVStar and OBJTP_VESSEL -> VkVVessel;
	// planets/bases use VkVObject placeholders until Phase 3 adds VkVPlanet / VkVBase.
	return new VkVObject(_hObj, scene);
}

VkVObject::VkVObject(OBJHANDLE _hObj, const VkScene *scene)
	: hObj(_hObj)
	, scn(scene)
{
	VkZeroAABB(&BBox);
	mWorld.Ident();
	dmWorld = identity4();

	if (hObj) {
		objtp = oapiGetObjectType(hObj);
		size = oapiGetSize(hObj);
		oapiGetObjectName(hObj, name, sizeof(name));
		name[sizeof(name) - 1] = '\0';
		oapiGetGlobalPos(hObj, &gpos);
		oapiGetRotationMatrix(hObj, &grot);
	}
}

void VkVObject::Activate(bool isactive)
{
	active = isactive;
	if (!active)
		bBSRecompute = true;
}

void VkVObject::UpdateBoundingBox()
{
	VkInitAABB(&BBox);
	const float rad = float(size);
	const oapi::FVECTOR3 ext(rad, rad, rad);
	const oapi::FVECTOR3 negExt(-rad, -rad, -rad);
	VkAddPointAABB(&BBox, &negExt);
	VkAddPointAABB(&BBox, &ext);
	VkUpdateAABB(&BBox);
}

oapi::FVECTOR3 VkVObject::GetBoundingSpherePosF()
{
	return oapi::TransformCoord(VkVec3(BBox.bs), mWorld);
}

VECTOR3 VkVObject::GetBoundingSpherePos()
{
	const oapi::FVECTOR3 p = GetBoundingSpherePosF();
	return _V(p.x, p.y, p.z);
}

float VkVObject::GetBoundingSphereRadius()
{
	if (BBox.bs.w > 0.0f)
		return BBox.bs.w;
	return float(size);
}

bool VkVObject::IsVisible()
{
	if (!scn)
		return false;
	const oapi::FVECTOR3 fcpos = VkVec3(cpos);
	const float rad = GetBoundingSphereRadius();
	return const_cast<VkScene *>(scn)->IsVisibleInCamera(&fcpos, rad);
}

bool VkVObject::Update(bool bMainScene)
{
	(void)bMainScene;
	if (!active || !hObj || !scn)
		return false;

	oapiGetGlobalPos(hObj, &gpos);
	oapiGetRotationMatrix(hObj, &grot);

	cpos = gpos - scn->GetCameraGPos();
	cdist = length(cpos);

	const OBJHANDLE hTgt = oapiCameraTarget();
	if (hTgt == hObj) {
		ctgtdst = cdist;
	} else if (hTgt) {
		VECTOR3 tpos;
		oapiGetGlobalPos(hTgt, &tpos);
		ctgtdst = length(gpos - tpos);
	} else {
		ctgtdst = cdist;
	}

	mWorld.Ident();
	VkMatSetInvRotation(&mWorld, &grot);
	VkMatSetTranslation(&mWorld, &cpos);

	dmWorld = _M(
		grot.m11, grot.m21, grot.m31, 0.0,
		grot.m12, grot.m22, grot.m32, 0.0,
		grot.m13, grot.m23, grot.m33, 0.0,
		cpos.x,   cpos.y,   cpos.z,   1.0);

	OBJHANDLE hSun = scn->GetSun();
	if (hSun) {
		VECTOR3 sunGPos;
		oapiGetGlobalPos(hSun, &sunGPos);
		VECTOR3 toSun = sunGPos - gpos;
		sundst = length(toSun);
		if (sundst > 1.0) {
			sundir = toSun / sundst;
			sunapprad = oapiGetSize(hSun) / sundst;
		} else {
			sundir = _V(0.0, 0.0, 1.0);
			sunapprad = 0.0;
		}
	}

	return true;
}

void VkVObject::ReOrigin(VECTOR3 global_pos)
{
	cpos = gpos - global_pos;
	cdist = length(cpos);
	VkMatSetTranslation(&mWorld, &cpos);
	dmWorld.m41 = cpos.x;
	dmWorld.m42 = cpos.y;
	dmWorld.m43 = cpos.z;
}
