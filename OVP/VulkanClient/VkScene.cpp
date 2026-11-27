// ==============================================================
// VkScene.cpp
// 3D scene coordinator, camera stack and visual management
// Part of the ORBITER VISUALISATION PROJECT (OVP) - VulkanClient
// Licensed under the MIT License
// ==============================================================

#include "VkScene.h"
#include "VkCelSphere.h"
#include "VulkanClient.h"
#include "VkSwapchain.h"
#include "VkLog.h"
#include <algorithm>
#include <cmath>
#include <limits>

VkScene::VkScene(oapi::VulkanClient *client, DWORD w, DWORD h)
	: gc(client)
	, viewW(w)
	, viewH(h)
{
	Camera.mView.Ident();
	Camera.mProj.Ident();
	Camera.mProjView.Ident();
	Camera.mProjViewInf.Ident();
	Camera.viewportW = w;
	Camera.viewportH = h;
	SetCameraAperture(0.5f, (h > 0) ? float(w) / float(h) : 1.0f);
	SetCameraFrustumLimits(1.0, 1e6);
}

VkScene::~VkScene()
{
	celSphere.reset();
	DeleteAllVisuals();
}

void VkScene::SetViewportSize(DWORD w, DWORD h)
{
	viewW = w;
	viewH = h;
	Camera.viewportW = w;
	Camera.viewportH = h;
	if (celSphere)
		celSphere->SetViewportSize(w, h);
}

void VkScene::Initialise()
{
	hSun = oapiGetGbodyByIndex(0);

	DWORD ambient = 0;
	if (gc) {
		if (const void *prm = gc->GetConfigParam(CFGPRM_AMBIENTLEVEL))
			ambient = *static_cast<const DWORD *>(prm);
	}

	sunLight.Color = oapi::FVECTOR4(1.0f, 1.0f, 1.0f, 1.0f);
	sunLight.Ambient = float(ambient) * 0.0039f;
	sunLight.Transmission = 1.0f;
	sunLight.Inscatter = 0.0f;

	if (hSun) {
		VECTOR3 rpos, cpos;
		oapiGetGlobalPos(hSun, &rpos);
		oapiCameraGlobalPos(&cpos);
		rpos -= cpos;
		if (length(rpos) > 1.0)
			sunLight.Dir = VkVec3(-unit(rpos));
	}

	if (gc) {
		celSphere = std::make_unique<VkCelSphere>(gc, this);
		if (!celSphere->Init())
			VkLogWarn("VkScene: VkCelSphere initialisation incomplete");
	}
}

void VkScene::OnOptionChanged(int cat, int item)
{
	if (celSphere)
		celSphere->OnOptionChanged(cat, item);
}

double VkScene::GetObjectAppRad(OBJHANDLE hObj) const
{
	VECTOR3 gpos;
	oapiGetGlobalPos(hObj, &gpos);
	const double dst = dist(gpos, Camera.pos);
	if (dst < 1e-3)
		return 1e6;
	const double rad = oapiGetSize(hObj);
	const double ap = std::max(0.01, double(Camera.aperture));
	return (rad * double(viewH)) / (dst * ap);
}

double VkScene::GetObjectAppRad2(OBJHANDLE hObj) const
{
	VECTOR3 gpos;
	oapiGetGlobalPos(hObj, &gpos);
	const double dst = dist(gpos, Camera.pos);
	if (dst < 1e-3)
		return 1e6;
	return oapiGetSize(hObj) / dst;
}

VkScene::VOBJREC *VkScene::FindVisual(OBJHANDLE hObj) const
{
	for (VOBJREC *pv = vobjFirst; pv; pv = pv->next) {
		if (pv->vobj && pv->vobj->Object() == hObj)
			return pv;
	}
	return nullptr;
}

VkVObject *VkScene::GetVisObject(OBJHANDLE hObj) const
{
	const VOBJREC *pv = FindVisual(hObj);
	return pv ? pv->vobj : nullptr;
}

VkScene::VOBJREC *VkScene::AddVisualRec(OBJHANDLE hObj)
{
	if (!hObj)
		return nullptr;

	if (VOBJREC *existing = FindVisual(hObj))
		return existing;

	auto *pv = new VOBJREC();
	pv->vobj = VkVObject::Create(hObj, this);
	pv->type = oapiGetObjectType(hObj);
	pv->apprad = float(GetObjectAppRad(hObj));
	pv->prev = vobjLast;
	pv->next = nullptr;

	if (vobjLast)
		vobjLast->next = pv;
	else
		vobjFirst = pv;
	vobjLast = pv;

	if (gc)
		gc->RegisterVisObject(hObj, reinterpret_cast<VISHANDLE>(pv->vobj));

	return pv;
}

void VkScene::DelVisualRec(VOBJREC *pv)
{
	if (!pv)
		return;

	if (pv->prev)
		pv->prev->next = pv->next;
	else
		vobjFirst = pv->next;

	if (pv->next)
		pv->next->prev = pv->prev;
	else
		vobjLast = pv->prev;

	if (pv->vobj) {
		if (Camera.vProxy == pv->vobj) Camera.vProxy = nullptr;
		if (Camera.vNear == pv->vobj)  Camera.vNear = nullptr;
		if (Camera.vGravRef == pv->vobj) Camera.vGravRef = nullptr;

		RenderList.remove(pv->vobj);
		if (gc)
			gc->UnregisterVisObject(pv->vobj->Object());
		delete pv->vobj;
	}

	delete pv;
}

void VkScene::DeleteAllVisuals()
{
	while (vobjFirst)
		DelVisualRec(vobjFirst);
	vobjFirst = nullptr;
	vobjLast = nullptr;
	RenderList.clear();
}

void VkScene::CheckVisual(OBJHANDLE hObj)
{
	if (!hObj)
		return;

	VECTOR3 pos;
	oapiGetGlobalPos(hObj, &pos);
	const double rad = oapiGetSize(hObj);
	const double dst = dist(pos, Camera.pos);
	if (dst < 1e-3)
		return;

	double apprad = (rad * double(viewH)) / (dst * std::max(0.01, double(Camera.aperture)));
	const int objtp = oapiGetObjectType(hObj);

	VOBJREC *pv = FindVisual(hObj);

	// Keep celestial bodies / vessels active above ~0.5 px or always for nearby focus/star
	if (objtp == OBJTP_SURFBASE) {
		const OBJHANDLE hParent = oapiGetBasePlanet(hObj);
		if (hParent) {
			const double pdst = dist(pos, Camera.pos);
			apprad = (rad * double(viewH)) / (std::max(1.0, pdst) * std::max(0.01, double(Camera.aperture)));
		}
	}

	if (pv) {
		if (apprad < 0.25 && hObj != oapiCameraTarget() && hObj != Camera.hObj_proxy && hObj != Camera.hNear)
			DelVisualRec(pv);
		else
			pv->apprad = float(apprad);
	} else {
		if (apprad > 0.5 || hObj == oapiCameraTarget() || hObj == Camera.hObj_proxy || hObj == Camera.hNear || objtp == OBJTP_STAR) {
			pv = AddVisualRec(hObj);
			if (pv && pv->vobj)
				pv->vobj->Update(true);
		}
	}
}

void VkScene::NewVessel(OBJHANDLE hVessel)
{
	CheckVisual(hVessel);
}

void VkScene::DeleteVessel(OBJHANDLE hVessel)
{
	if (VOBJREC *pv = FindVisual(hVessel))
		DelVisualRec(pv);
}

VECTOR3 VkScene::ComputeSkyColour() const
{
	VECTOR3 col{ 0.0, 0.0, 0.0 };
	const OBJHANDLE hProxy = Camera.hObj_proxy;
	if (!hProxy || !oapiPlanetHasAtmosphere(hProxy))
		return col;

	const ATMCONST *atmc = oapiGetPlanetAtmConstants(hProxy);
	if (!atmc || atmc->rho0 <= 0.0)
		return col;

	VECTOR3 ppos;
	oapiGetGlobalPos(hProxy, &ppos);
	const VECTOR3 dp = Camera.pos - ppos;
	const double r = length(dp);
	if (r <= 1.0)
		return col;

	ATMPARAM prm{};
	oapiGetPlanetAtmParams(hProxy, r, &prm);
	if (prm.rho <= 0.0)
		return col;

	const double rhoRatio = std::clamp(prm.rho / atmc->rho0, 0.0, 1.0);
	double sunFactor = 1.0;
	if (hSun) {
		VECTOR3 spos;
		oapiGetGlobalPos(hSun, &spos);
		const VECTOR3 sdir = unit(spos - ppos);
		const VECTOR3 up = dp / r;
		const double cosSun = dotp(sdir, up);
		sunFactor = std::clamp((cosSun + 0.15) * 1.4, 0.0, 1.0);
	}

	const double scale = std::pow(rhoRatio, 0.7) * sunFactor;
	col.x = std::clamp(atmc->color0.x * scale, 0.0, 1.0);
	col.y = std::clamp(atmc->color0.y * scale, 0.0, 1.0);
	col.z = std::clamp(atmc->color0.z * scale, 0.0, 1.0);
	return col;
}

void VkScene::Update()
{
	UpdateCameraFromOrbiter(RENDERPASS_MAINSCENE);

	const DWORD nobj = oapiGetObjectCount();
	if (nobj > 0) {
		if (iVCheck >= nobj)
			iVCheck = 0;
		const OBJHANDLE hObj = oapiGetObjectByIndex(int(iVCheck));
		CheckVisual(hObj);
		++iVCheck;
	}

	if (hSun) {
		VECTOR3 rpos;
		oapiGetGlobalPos(hSun, &rpos);
		rpos -= Camera.pos;
		if (length(rpos) > 1.0)
			sunLight.Dir = VkVec3(-unit(rpos));
	}

	for (VOBJREC *pv = vobjFirst; pv; pv = pv->next) {
		if (pv->vobj)
			pv->vobj->Update(true);
	}
}

float VkScene::ComputeNearClipPlane()
{
	float nearpt = 1e9f;
	float farpt = 0.0f;
	float neardist = 1e9f;

	for (VOBJREC *pv = vobjFirst; pv; pv = pv->next) {
		VkVObject *vo = pv->vobj;
		if (!vo || !vo->IsActive() || vo->bOmit)
			continue;

		float zmin = 1e9f, zmax = 0.0f, dmin = 1e9f;
		if (vo->GetMinMaxDistance(&zmin, &zmax, &dmin)) {
			nearpt = std::min(nearpt, zmin);
			farpt = std::max(farpt, zmax);
			neardist = std::min(neardist, dmin);
		} else if (vo->Type() == OBJTP_PLANET || vo->Type() == OBJTP_STAR) {
			const float cdist = float(vo->CamDist());
			const float rad = float(vo->GetSize());
			if (cdist > rad) {
				nearpt = std::min(nearpt, cdist - rad);
				farpt = std::max(farpt, cdist + rad);
				neardist = std::min(neardist, cdist - rad);
			}
		}
	}

	if (nearpt >= 1e8f)
		return 1.0f;

	return VkNearPlane(nearpt, farpt, neardist, &Camera.mProj, false);
}

bool VkScene::UpdateCamVis()
{
	sky_color = ComputeSkyColour();
	bglvl = (sky_color.x + sky_color.y + sky_color.z) / 3.0;
	bg_rgba =
		(0xFFu << 24) |
		(DWORD(std::clamp(sky_color.x * 255.0, 0.0, 255.0)) << 16) |
		(DWORD(std::clamp(sky_color.y * 255.0, 0.0, 255.0)) << 8) |
		DWORD(std::clamp(sky_color.z * 255.0, 0.0, 255.0));

	RenderList.clear();

	for (VOBJREC *pv = vobjFirst; pv; pv = pv->next) {
		VkVObject *vo = pv->vobj;
		if (!vo)
			continue;
		vo->ReOrigin(Camera.pos);
		const bool vis = vo->IsVisible();
		vo->Activate(vis || vo->Type() == OBJTP_PLANET || vo->Type() == OBJTP_STAR);
		if (vis && vo->Type() == OBJTP_VESSEL)
			RenderList.push_back(vo);
	}

	const float znear = ComputeNearClipPlane();
	SetCameraFrustumLimits(znear, std::max(1e6f, float(Camera.alt_proxy * 2.0 + 1e6)));
	return true;
}

void VkScene::RenderMainScene(VkSwapchain &swapchain)
{
	++dwFrameId;

	UpdateCameraFromOrbiter(RENDERPASS_MAINSCENE);
	UpdateCamVis();

	const VkClearColorValue clearCol = {
		{ float(sky_color.x), float(sky_color.y), float(sky_color.z), 1.0f }
	};

	if (swapchain.BeginScenePass(true, clearCol, true, 1.0f, true)) {
		if (celSphere)
			celSphere->Render(swapchain.CurrentCommandBuffer());
		// Unit 2.4: render active stars & vessels (opaque -> alpha -> VC pass)
		swapchain.EndScenePass();
	}
}

void VkScene::RenderOverlay()
{
	if (celSphere)
		celSphere->RenderOverlay();
}

void VkScene::SetCameraAperture(float ap, float as)
{
	Camera.aperture = std::max(0.01f, ap);
	Camera.aspect = (as > 0.0f) ? as : 1.0f;

	const float tanap = std::tanf(Camera.aperture);
	Camera.vh = tanap;
	Camera.vw = tanap * Camera.aspect;
	Camera.vhf = std::sqrtf(1.0f + Camera.vh * Camera.vh);
	Camera.vwf = std::sqrtf(1.0f + Camera.vw * Camera.vw);
	Camera.apsq = tanap * std::sqrtf(1.0f + Camera.aspect * Camera.aspect);

	Camera.mProj.Zero();
	Camera.mProj.m11 = 1.0f / (Camera.aspect * tanap);
	Camera.mProj.m22 = 1.0f / tanap;
	const float nfDiff = std::max(0.01f, Camera.farplane - Camera.nearplane);
	Camera.mProj.m33 = Camera.farplane / nfDiff;
	Camera.mProj.m43 = Camera.mProj.m33 * (-Camera.nearplane);
	Camera.mProj.m34 = 1.0f;

	Camera.mProjView = VkMatMul(Camera.mView, Camera.mProj);

	oapi::FMATRIX4 mProjInf = Camera.mProj;
	mProjInf.m33 = 1.0f - 1e-6f;
	mProjInf.m43 = -Camera.nearplane;
	Camera.mProjViewInf = VkMatMul(Camera.mView, mProjInf);
}

void VkScene::SetCameraFrustumLimits(double nearlimit, double farlimit)
{
	Camera.nearplane = float(std::max(0.05, nearlimit));
	Camera.farplane = float(std::max(double(Camera.nearplane) + 10.0, farlimit));
	SetCameraAperture(Camera.aperture, Camera.aspect);
}

bool VkScene::UpdateCameraFromOrbiter(DWORD dwPass)
{
	(void)dwPass;
	oapiCameraGlobalPos(&Camera.pos);
	oapiCameraGlobalDir(&Camera.dir);
	Camera.hTarget = oapiCameraTarget();

	MATRIX3 grot{};
	oapiCameraRotationMatrix(&grot);

	Camera.mView.Ident();
	VkMatSetRotation(&Camera.mView, &grot);

	Camera.x = oapi::FVECTOR3(float(grot.m11), float(grot.m21), float(grot.m31));
	Camera.y = oapi::FVECTOR3(float(grot.m12), float(grot.m22), float(grot.m32));
	Camera.z = oapi::FVECTOR3(float(grot.m13), float(grot.m23), float(grot.m33));

	Camera.hObj_proxy = oapiCameraProxyGbody();
	Camera.hNear = Camera.hObj_proxy;
	Camera.hGravRef = Camera.hObj_proxy;

	if (Camera.hObj_proxy) {
		if (!GetVisObject(Camera.hObj_proxy))
			AddVisualRec(Camera.hObj_proxy);

		Camera.vProxy = GetVisObject(Camera.hObj_proxy);
		Camera.vNear = Camera.vProxy;
		Camera.vGravRef = Camera.vProxy;

		VECTOR3 ppos;
		oapiGetGlobalPos(Camera.hObj_proxy, &ppos);
		const VECTOR3 dp = Camera.pos - ppos;
		const double distProxy = length(dp);
		const double radProxy = oapiGetSize(Camera.hObj_proxy);
		Camera.alt_proxy = distProxy - radProxy;
		Camera.alt_near = Camera.alt_proxy;
		Camera.elev = Camera.alt_proxy;
		if (distProxy > 1.0)
			Camera.upos = VkVec3(dp / distProxy);
		oapiGlobalToEqu(Camera.hObj_proxy, Camera.pos, &Camera.lng, &Camera.lat, &Camera.elev);
		Camera.elev -= radProxy;
	} else {
		Camera.vProxy = nullptr;
		Camera.vNear = nullptr;
		Camera.vGravRef = nullptr;
		Camera.alt_proxy = 0.0;
		Camera.alt_near = 0.0;
		Camera.elev = 0.0;
	}

	const float aspect = (viewH > 0) ? (float(viewW) / float(viewH)) : 1.0f;
	SetCameraAperture(float(oapiCameraAperture()), aspect);
	return true;
}

bool VkScene::SetupInternalCamera(oapi::FMATRIX4 *mView, VECTOR3 *pos, double apr, double asp)
{
	if (!mView || !pos)
		return false;

	Camera.pos = *pos;
	Camera.mView = *mView;
	Camera.x = oapi::FVECTOR3(mView->m11, mView->m21, mView->m31);
	Camera.y = oapi::FVECTOR3(mView->m12, mView->m22, mView->m32);
	Camera.z = oapi::FVECTOR3(mView->m13, mView->m23, mView->m33);
	Camera.dir = _V(double(Camera.z.x), double(Camera.z.y), double(Camera.z.z));
	SetCameraAperture(float(apr), float(asp));
	return true;
}

void VkScene::PushCamera()
{
	CameraStack.push(Camera);
}

void VkScene::PopCamera()
{
	if (!CameraStack.empty()) {
		Camera = CameraStack.top();
		CameraStack.pop();
	}
}

oapi::FMATRIX4 VkScene::PushCameraFrustumLimits(float nearlimit, float farlimit)
{
	FRUSTUM fr{ Camera.nearplane, Camera.farplane };
	FrustumStack.push(fr);
	SetCameraFrustumLimits(nearlimit, farlimit);
	return Camera.mProjView;
}

oapi::FMATRIX4 VkScene::PopCameraFrustumLimits()
{
	if (!FrustumStack.empty()) {
		const FRUSTUM fr = FrustumStack.top();
		FrustumStack.pop();
		SetCameraFrustumLimits(fr.znear, fr.zfar);
	}
	return Camera.mProjView;
}

void VkScene::BeginPass(DWORD dwPass)
{
	PassStack.push(dwPass);
}

void VkScene::PopPass()
{
	if (!PassStack.empty())
		PassStack.pop();
}

DWORD VkScene::GetRenderPass() const
{
	if (PassStack.empty())
		return RENDERPASS_MAINSCENE;
	return PassStack.top();
}

bool VkScene::IsVisibleInCamera(const oapi::FVECTOR3 *pCnt, float radius) const
{
	if (!pCnt)
		return false;

	float z = Camera.z.x * pCnt->x + Camera.z.y * pCnt->y + Camera.z.z * pCnt->z;
	if (z < -radius)
		return false;
	if (z < 0.0f)
		z = -z;

	float y = Camera.y.x * pCnt->x + Camera.y.y * pCnt->y + Camera.y.z * pCnt->z;
	if (y < 0.0f)
		y = -y;
	if (y - (radius * Camera.vhf) > (Camera.vh * z))
		return false;

	float x = Camera.x.x * pCnt->x + Camera.x.y * pCnt->y + Camera.x.z * pCnt->z;
	if (x < 0.0f)
		x = -x;
	if (x - (radius * Camera.vwf) > (Camera.vw * z))
		return false;

	return true;
}

bool VkScene::WorldToScreenSpace(const VECTOR3 &wpos, oapi::IVECTOR2 *pt, const oapi::FMATRIX4 *pVP, float clip) const
{
	if (!pt || viewW == 0 || viewH == 0)
		return false;

	oapi::FVECTOR3 homog;
	const oapi::FVECTOR3 pos = VkVec3(wpos);
	const oapi::FMATRIX4 *vp = pVP ? pVP : &Camera.mProjView;
	if (!VkMatVectorMatrixMultiply(&homog, &pos, vp))
		return false;

	// Check unprojected clip w directly
	const float w = pos.x * vp->m14 + pos.y * vp->m24 + pos.z * vp->m34 + vp->m44;
	if (w < 0.0f)
		return false;

	const bool clipped = (homog.x < -clip || homog.x > clip || homog.y < -clip || homog.y > clip);
	if (std::hypot(homog.x, homog.y) < 1e-6) {
		pt->x = long(viewW / 2);
		pt->y = long(viewH / 2);
	} else {
		pt->x = long((float(viewW) * 0.5f * (1.0f + homog.x)) + 0.5f);
		pt->y = long((float(viewH) * 0.5f * (1.0f - homog.y)) + 0.5f);
	}
	return !clipped;
}

bool VkScene::WorldToScreenSpace2(const VECTOR3 &wpos, oapi::FVECTOR2 *pt, const oapi::FMATRIX4 *pVP, float clip) const
{
	if (!pt || viewW == 0 || viewH == 0)
		return false;

	oapi::FVECTOR3 homog;
	const oapi::FVECTOR3 pos = VkVec3(wpos);
	const oapi::FMATRIX4 *vp = pVP ? pVP : &Camera.mProjView;
	if (!VkMatVectorMatrixMultiply(&homog, &pos, vp))
		return false;

	const float w = pos.x * vp->m14 + pos.y * vp->m24 + pos.z * vp->m34 + vp->m44;
	bool clipped = (w < 0.0f) || (homog.x < -clip || homog.x > clip || homog.y < -clip || homog.y > clip);

	if (std::hypot(homog.x, homog.y) < 1e-6) {
		pt->x = float(viewW) * 0.5f;
		pt->y = float(viewH) * 0.5f;
	} else {
		pt->x = (float(viewW) * 0.5f * (1.0f + homog.x)) + 0.5f;
		pt->y = (float(viewH) * 0.5f * (1.0f - homog.y)) + 0.5f;
	}
	return !clipped;
}

bool VkScene::CameraDirection2Viewport(const VECTOR3 &dir, int &x, int &y) const
{
	const DWORD width = Camera.viewportW ? Camera.viewportW : viewW;
	const DWORD height = Camera.viewportH ? Camera.viewportH : viewH;
	if (width == 0 || height == 0)
		return false;

	const oapi::FVECTOR3 idir(-float(dir.x), -float(dir.y), -float(dir.z));
	oapi::FVECTOR3 homog;
	if (!VkMatVectorMatrixMultiply(&homog, &idir, &Camera.mProjView))
		return false;

	if (homog.x >= -1.0f && homog.y <= 1.0f && homog.z >= 0.0f) {
		if (std::hypot(homog.x, homog.y) < 1e-6) {
			x = int(width / 2);
			y = int(height / 2);
		} else {
			x = int(float(width) * 0.5f * (1.0f + homog.x));
			y = int(float(height) * 0.5f * (1.0f - homog.y));
		}
		return true;
	}
	return false;
}

void VkScene::GetCameraLngLat(double *lng, double *lat) const
{
	if (lng) *lng = Camera.lng;
	if (lat) *lat = Camera.lat;
}
