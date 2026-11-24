// ==============================================================
// VkScene.h
// 3D scene coordinator, camera stack and visual management
// Part of the ORBITER VISUALISATION PROJECT (OVP) - VulkanClient
// Licensed under the MIT License
// ==============================================================

#ifndef __VKCLIENT_VKSCENE_H
#define __VKCLIENT_VKSCENE_H

#include "VkAPI.h"
#include "VkMath.h"
#include "VkVObject.h"
#include <list>
#include <stack>

namespace oapi {
class VulkanClient;
}
class VkSwapchain;

#define RENDERPASS_UNKNOWN      0x0000
#define RENDERPASS_MAINSCENE    0x0001
#define RENDERPASS_ENVCAM       0x0002
#define RENDERPASS_CUSTOMCAM    0x0003
#define RENDERPASS_SHADOWMAP    0x0004
#define RENDERPASS_PICKSCENE    0x0005
#define RENDERPASS_SKETCHPAD    0x0006
#define RENDERPASS_MAINOVERLAY  0x0007
#define RENDERPASS_NORMAL_DEPTH 0x0008

struct VkSun {
	oapi::FVECTOR4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };
	oapi::FVECTOR3 Dir{ 0.0f, 0.0f, 1.0f };
	float Ambient = 0.0f;
	float Transmission = 1.0f;
	float Inscatter = 0.0f;
};

/**
 * \brief Represents a 3D world view with camera frustum stack, visual lifecycle,
 * and per-frame scene rendering coordination.
 */
class VkScene {
public:
	struct VOBJREC {
		VkVObject *vobj = nullptr;
		int type = 0;
		float apprad = 0.0f;
		VOBJREC *prev = nullptr;
		VOBJREC *next = nullptr;
	};

	struct FRUSTUM {
		float znear = 1.0f;
		float zfar = 1e6f;
	};

	struct CAMERA {
		float aperture = 0.5f;
		float aspect = 1.333333f;
		float nearplane = 1.0f;
		float farplane = 1e6f;
		float apsq = 0.5f;
		float vh = 1.0f, vw = 1.0f, vhf = 1.414f, vwf = 1.414f;

		VECTOR3 pos{ 0.0, 0.0, 0.0 };
		VECTOR3 relpos{ 0.0, 0.0, 0.0 };
		VECTOR3 dir{ 0.0, 0.0, 1.0 };

		oapi::FVECTOR3 x{ 1.0f, 0.0f, 0.0f };
		oapi::FVECTOR3 y{ 0.0f, 1.0f, 0.0f };
		oapi::FVECTOR3 z{ 0.0f, 0.0f, 1.0f };
		oapi::FVECTOR3 upos{ 0.0f, 1.0f, 0.0f };

		oapi::FMATRIX4 mView = VkMatIdentity();
		oapi::FMATRIX4 mProj = VkMatIdentity();
		oapi::FMATRIX4 mProjView = VkMatIdentity();
		oapi::FMATRIX4 mProjViewInf = VkMatIdentity();

		OBJHANDLE hTarget = nullptr;
		OBJHANDLE hObj_proxy = nullptr;
		VkVObject *vProxy = nullptr;
		double alt_proxy = 0.0;

		OBJHANDLE hNear = nullptr;
		VkVObject *vNear = nullptr;
		double alt_near = 0.0;

		OBJHANDLE hGravRef = nullptr;
		VkVObject *vGravRef = nullptr;

		double elev = 0.0;
		double lng = 0.0;
		double lat = 0.0;

		DWORD viewportW = 0;
		DWORD viewportH = 0;
	};

	VkScene(oapi::VulkanClient *client, DWORD w, DWORD h);
	~VkScene();

	void Initialise();
	void OnOptionChanged(int cat, int item);
	void SetViewportSize(DWORD w, DWORD h);

	void Update();
	void RenderMainScene(VkSwapchain &swapchain);

	void NewVessel(OBJHANDLE hVessel);
	void DeleteVessel(OBJHANDLE hVessel);
	void CheckVisual(OBJHANDLE hObj);

	VkVObject *GetVisObject(OBJHANDLE hObj) const;
	const std::list<VkVObject *> &GetRenderList() const { return RenderList; }

	bool UpdateCameraFromOrbiter(DWORD dwPass);
	bool SetupInternalCamera(oapi::FMATRIX4 *mView, VECTOR3 *pos, double apr, double asp);
	void SetCameraAperture(float ap, float as);
	void SetCameraFrustumLimits(double nearlimit, double farlimit);
	void PushCamera();
	void PopCamera();
	oapi::FMATRIX4 PushCameraFrustumLimits(float nearlimit, float farlimit);
	oapi::FMATRIX4 PopCameraFrustumLimits();

	void BeginPass(DWORD dwPass);
	void PopPass();
	DWORD GetRenderPass() const;

	bool IsVisibleInCamera(const oapi::FVECTOR3 *pCnt, float radius) const;
	bool WorldToScreenSpace(const VECTOR3 &wpos, oapi::IVECTOR2 *pt, const oapi::FMATRIX4 *pVP = nullptr, float clip = 1.0f) const;
	bool WorldToScreenSpace2(const VECTOR3 &wpos, oapi::FVECTOR2 *pt, const oapi::FMATRIX4 *pVP = nullptr, float clip = 1.0f) const;
	bool CameraDirection2Viewport(const VECTOR3 &dir, int &x, int &y) const;

	double GetTanAp() const { return std::tan(Camera.aperture); }
	float GetCameraAspect() const { return Camera.aspect; }
	float GetCameraFarPlane() const { return Camera.farplane; }
	float GetCameraNearPlane() const { return Camera.nearplane; }
	float GetCameraAperture() const { return Camera.aperture; }
	VECTOR3 GetCameraGPos() const { return Camera.pos; }
	VECTOR3 GetCameraGDir() const { return Camera.dir; }
	OBJHANDLE GetCameraProxyBody() const { return Camera.hObj_proxy; }
	VkVObject *GetCameraProxyVisual() const { return Camera.vProxy; }
	double GetCameraAltitude() const { return Camera.alt_proxy; }
	OBJHANDLE GetCameraNearBody() const { return Camera.hNear; }
	VkVObject *GetCameraNearVisual() const { return Camera.vNear; }
	double GetCameraNearAltitude() const { return Camera.alt_near; }
	double GetCameraElevation() const { return Camera.elev; }
	void GetCameraLngLat(double *lng, double *lat) const;

	const oapi::FMATRIX4 *GetViewMatrix() const { return &Camera.mView; }
	const oapi::FMATRIX4 *GetProjectionMatrix() const { return &Camera.mProj; }
	const oapi::FMATRIX4 *GetProjectionViewMatrix() const { return &Camera.mProjView; }
	const oapi::FMATRIX4 *GetProjectionViewInfMatrix() const { return &Camera.mProjViewInf; }
	const oapi::FVECTOR3 *GetCameraX() const { return &Camera.x; }
	const oapi::FVECTOR3 *GetCameraY() const { return &Camera.y; }
	const oapi::FVECTOR3 *GetCameraZ() const { return &Camera.z; }
	const CAMERA *GetCamera() const { return &Camera; }

	OBJHANDLE GetSun() const { return hSun; }
	const VkSun *GetSunLight() const { return &sunLight; }
	VECTOR3 GetSkyColour() const { return sky_color; }
	DWORD GetViewW() const { return viewW; }
	DWORD GetViewH() const { return viewH; }
	DWORD GetFrameId() const { return dwFrameId; }

private:
	double GetObjectAppRad(OBJHANDLE hObj) const;
	double GetObjectAppRad2(OBJHANDLE hObj) const;
	VOBJREC *FindVisual(OBJHANDLE hObj) const;
	VOBJREC *AddVisualRec(OBJHANDLE hObj);
	void DelVisualRec(VOBJREC *pv);
	void DeleteAllVisuals();

	VECTOR3 ComputeSkyColour() const;
	float ComputeNearClipPlane();
	bool UpdateCamVis();

	oapi::VulkanClient *gc = nullptr;
	DWORD viewW = 0;
	DWORD viewH = 0;
	DWORD iVCheck = 0;
	DWORD dwFrameId = 0;
	OBJHANDLE hSun = nullptr;

	VOBJREC *vobjFirst = nullptr;
	VOBJREC *vobjLast = nullptr;

	std::list<VkVObject *> RenderList;
	std::stack<CAMERA> CameraStack;
	std::stack<FRUSTUM> FrustumStack;
	std::stack<DWORD> PassStack;

	CAMERA Camera{};
	VkSun sunLight{};
	VECTOR3 sky_color{ 0.0, 0.0, 0.0 };
	double bglvl = 0.0;
	DWORD bg_rgba = 0xFF000000;
};

#endif // !__VKCLIENT_VKSCENE_H
