// ==============================================================
// VkVObject.h
// Visual representation of an Orbiter scene object
// Part of the ORBITER VISUALISATION PROJECT (OVP) - VulkanClient
// Licensed under the MIT License
// ==============================================================

#ifndef __VKCLIENT_VKVOBJECT_H
#define __VKCLIENT_VKVOBJECT_H

#include "VkAABBUtil.h"
#include "VkMath.h"
#include <string>

namespace oapi {
class VulkanClient;
}
class VkScene;
class VkMesh;

/**
 * \brief Base class for visual objects managed by VkScene (vessels, celestial bodies,
 * surface bases, stars).
 */
class VkVObject {
public:
	explicit VkVObject(OBJHANDLE _hObj, const VkScene *scene);
	virtual ~VkVObject() = default;

	static VkVObject *Create(OBJHANDLE _hObj, const VkScene *scene);
	static void GlobalInit(oapi::VulkanClient *gclient);
	static void GlobalExit();

	OBJHANDLE Object() const { return hObj; }
	int Type() const { return objtp; }
	const VkScene *GetScene() const { return scn; }
	static oapi::VulkanClient *GetClient() { return gc; }

	bool IsActive() const { return active; }
	virtual void Activate(bool isactive);

	const oapi::FMATRIX4 *MWorld() const { return &mWorld; }
	const MATRIX4 &DMWorld() const { return dmWorld; }

	virtual MESHHANDLE GetMesh(UINT idx) { (void)idx; return nullptr; }
	virtual DWORD GetMeshCount() { return 0; }
	virtual bool GetMinMaxDistance(float *zmin, float *zmax, float *dmin) { (void)zmin; (void)zmax; (void)dmin; return false; }

	virtual void UpdateBoundingBox();
	virtual bool IsVisible();

	oapi::FVECTOR3 GetBoundingSpherePosF();
	VECTOR3 GetBoundingSpherePos();
	float GetBoundingSphereRadius();
	const char *GetName() const { return name; }

	double CamDist() const { return cdist; }
	double GetSize() const { return size; }
	double SunApparentRad() const { return sunapprad; }
	double CameraTgtDist() const { return ctgtdst; }
	const VECTOR3 &PosFromCamera() const { return cpos; }
	const VECTOR3 &GlobalPos() const { return gpos; }
	const VECTOR3 &SunDirection() const { return sundir; }
	double SunDistance() const { return sundst; }
	bool Is(const std::string &x) const { return x == name; }

	virtual bool Update(bool bMainScene);
	virtual void ReOrigin(VECTOR3 global_pos);
	virtual void CheckResolution() {}
	virtual void UpdateAnimations(bool bMainScene = true) { (void)bMainScene; }

	virtual bool Render(VkCommandBuffer cmd) { (void)cmd; return false; }
	virtual bool RenderVC(VkCommandBuffer cmd) { (void)cmd; return false; }
	virtual void RenderBeacons(VkCommandBuffer cmd) { (void)cmd; }
	virtual void RenderGrapplePoints(VkCommandBuffer cmd) { (void)cmd; }
	virtual void RenderDot(VkCommandBuffer cmd) { (void)cmd; }

	virtual void clbkEvent(DWORD event, DWORD_PTR context) { (void)event; (void)context; }

	bool bStencilShadow = false;
	bool bOmit = false;
	VkBBox BBox{};

protected:
	static oapi::VulkanClient *gc;

	OBJHANDLE hObj = nullptr;
	bool bBSRecompute = false;
	bool active = true;
	int objtp = 0;
	const VkScene *scn = nullptr;
	VECTOR3 axis{ 0.0, 1.0, 0.0 };
	VECTOR3 cpos{ 0.0, 0.0, 0.0 };
	VECTOR3 sundir{ 0.0, 0.0, 1.0 };
	VECTOR3 albedo{ 1.0, 1.0, 1.0 };
	VECTOR3 gpos{ 0.0, 0.0, 0.0 };
	MATRIX3 grot{};
	MATRIX4 dmWorld = identity4();
	oapi::FMATRIX4 mWorld = VkMatIdentity();
	double size = 1.0;
	double cdist = 0.0;
	double sunapprad = 0.0;
	double sundst = 0.0;
	double ctgtdst = 0.0;
	double lng = 0.0;
	double lat = 0.0;
	OBJHANDLE hPlanet = nullptr;
	char name[64]{};
};

#endif // !__VKCLIENT_VKVOBJECT_H
