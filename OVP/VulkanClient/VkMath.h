// ==============================================================
// VkMath.h
// Pure C++ row-major DX9/Orbiter-compatible vector & matrix helpers
// Part of the ORBITER VISUALISATION PROJECT (OVP) - VulkanClient
// Licensed under the MIT License
// ==============================================================

#ifndef __VKCLIENT_VKMATH_H
#define __VKCLIENT_VKMATH_H

#include "VkAPI.h"
#include "OrbiterAPI.h"
#include "DrawAPI.h"
#include <algorithm>
#include <cmath>
#include <cstring>

inline oapi::FVECTOR3 VkVec3(const VECTOR3 &v)
{
	return oapi::FVECTOR3(float(v.x), float(v.y), float(v.z));
}

inline oapi::FVECTOR3 VkVec3(const oapi::FVECTOR4 &v)
{
	return oapi::FVECTOR3(v.x, v.y, v.z);
}

inline oapi::FVECTOR4 VkArgbToVec4(DWORD argb)
{
	const float inv = 1.0f / 255.0f;
	return oapi::FVECTOR4(
		float((argb >> 16) & 0xFF) * inv,
		float((argb >> 8)  & 0xFF) * inv,
		float( argb        & 0xFF) * inv,
		float((argb >> 24) & 0xFF) * inv);
}

inline oapi::FVECTOR4 VkAbgrToVec4(DWORD abgr)
{
	const float inv = 1.0f / 255.0f;
	return oapi::FVECTOR4(
		float( abgr        & 0xFF) * inv,
		float((abgr >> 8)  & 0xFF) * inv,
		float((abgr >> 16) & 0xFF) * inv,
		float((abgr >> 24) & 0xFF) * inv);
}

inline oapi::FMATRIX4 VkMatZero()
{
	oapi::FMATRIX4 m;
	m.Zero();
	return m;
}

inline oapi::FMATRIX4 VkMatIdentity()
{
	oapi::FMATRIX4 m;
	m.Ident();
	return m;
}

inline void VkMatZero(oapi::FMATRIX4 *mat)
{
	mat->Zero();
}

inline void VkMatIdentity(oapi::FMATRIX4 *mat)
{
	mat->Ident();
}

inline void VkMatCopy(oapi::FMATRIX4 *tgt, const oapi::FMATRIX4 *src)
{
	std::memcpy(tgt, src, sizeof(oapi::FMATRIX4));
}

inline oapi::FMATRIX4 VkMatMul(const oapi::FMATRIX4 &a, const oapi::FMATRIX4 &b)
{
	oapi::FMATRIX4 out;
	out.Zero();
	for (int r = 0; r < 4; ++r) {
		const float a0 = a.data[r * 4 + 0];
		const float a1 = a.data[r * 4 + 1];
		const float a2 = a.data[r * 4 + 2];
		const float a3 = a.data[r * 4 + 3];
		for (int c = 0; c < 4; ++c) {
			out.data[r * 4 + c] =
				a0 * b.data[0 * 4 + c] +
				a1 * b.data[1 * 4 + c] +
				a2 * b.data[2 * 4 + c] +
				a3 * b.data[3 * 4 + c];
		}
	}
	return out;
}

inline oapi::FMATRIX4 VkMatTranspose(const oapi::FMATRIX4 &m)
{
	oapi::FMATRIX4 out = m;
	out.Transpose();
	return out;
}

inline oapi::FMATRIX4 VkMatTranslation(float x, float y, float z)
{
	oapi::FMATRIX4 m = VkMatIdentity();
	m.m41 = x;
	m.m42 = y;
	m.m43 = z;
	return m;
}

inline oapi::FMATRIX4 VkMatTranslation(const oapi::FVECTOR3 &t)
{
	return VkMatTranslation(t.x, t.y, t.z);
}

inline oapi::FMATRIX4 VkMatScaling(float sx, float sy, float sz)
{
	oapi::FMATRIX4 m = VkMatZero();
	m.m11 = sx;
	m.m22 = sy;
	m.m33 = sz;
	m.m44 = 1.0f;
	return m;
}

inline void VkMatSetRotation(oapi::FMATRIX4 *mat, const MATRIX3 *rot)
{
	mat->m11 = float(rot->m11); mat->m12 = float(rot->m12); mat->m13 = float(rot->m13);
	mat->m21 = float(rot->m21); mat->m22 = float(rot->m22); mat->m23 = float(rot->m23);
	mat->m31 = float(rot->m31); mat->m32 = float(rot->m32); mat->m33 = float(rot->m33);
}

inline void VkMatSetInvRotation(oapi::FMATRIX4 *mat, const MATRIX3 *rot)
{
	mat->m11 = float(rot->m11); mat->m12 = float(rot->m21); mat->m13 = float(rot->m31);
	mat->m21 = float(rot->m12); mat->m22 = float(rot->m22); mat->m23 = float(rot->m32);
	mat->m31 = float(rot->m13); mat->m32 = float(rot->m23); mat->m33 = float(rot->m33);
}

inline void VkMatSetTranslation(oapi::FMATRIX4 *mat, const VECTOR3 *trans)
{
	mat->m41 = float(trans->x);
	mat->m42 = float(trans->y);
	mat->m43 = float(trans->z);
}

inline void VkMatSetTranslation(oapi::FMATRIX4 *mat, const oapi::FVECTOR3 *trans)
{
	mat->m41 = trans->x;
	mat->m42 = trans->y;
	mat->m43 = trans->z;
}

inline void VkMatFromAxis(oapi::FMATRIX4 *mat, const oapi::FVECTOR3 *x, const oapi::FVECTOR3 *y, const oapi::FVECTOR3 *z)
{
	mat->m11 = x->x; mat->m21 = x->y; mat->m31 = x->z;
	mat->m12 = y->x; mat->m22 = y->y; mat->m32 = y->z;
	mat->m13 = z->x; mat->m23 = z->y; mat->m33 = z->z;
}

inline void VkMatFromAxis(oapi::FMATRIX4 *mat, const VECTOR3 *x, const VECTOR3 *y, const VECTOR3 *z)
{
	mat->m11 = float(x->x); mat->m21 = float(x->y); mat->m31 = float(x->z);
	mat->m12 = float(y->x); mat->m22 = float(y->y); mat->m32 = float(y->z);
	mat->m13 = float(z->x); mat->m23 = float(z->y); mat->m33 = float(z->z);
}

inline void VkMatFromAxisT(oapi::FMATRIX4 *mat, const oapi::FVECTOR3 *x, const oapi::FVECTOR3 *y, const oapi::FVECTOR3 *z)
{
	mat->m11 = x->x; mat->m12 = x->y; mat->m13 = x->z;
	mat->m21 = y->x; mat->m22 = y->y; mat->m23 = y->z;
	mat->m31 = z->x; mat->m32 = z->y; mat->m33 = z->z;
}

inline void VkMatRotationFromAxis(const oapi::FVECTOR3 &axis, float angle, oapi::FMATRIX4 *rot)
{
	const float half = angle * 0.5f;
	const float w = std::cosf(half);
	const float sina = std::sinf(half);
	const float x = sina * axis.x;
	const float y = sina * axis.y;
	const float z = sina * axis.z;

	const float xx = x * x, yy = y * y, zz = z * z;
	const float xy = x * y, xz = x * z, yz = y * z;
	const float wx = w * x, wy = w * y, wz = w * z;

	rot->m11 = 1.0f - 2.0f * (yy + zz);
	rot->m12 =        2.0f * (xy + wz);
	rot->m13 =        2.0f * (xz - wy);
	rot->m21 =        2.0f * (xy - wz);
	rot->m22 = 1.0f - 2.0f * (xx + zz);
	rot->m23 =        2.0f * (yz + wx);
	rot->m31 =        2.0f * (xz + wy);
	rot->m32 =        2.0f * (yz - wx);
	rot->m33 = 1.0f - 2.0f * (xx + yy);

	rot->m14 = rot->m24 = rot->m34 = rot->m41 = rot->m42 = rot->m43 = 0.0f;
	rot->m44 = 1.0f;
}

inline void VkMatRotX(oapi::FMATRIX4 *mat, double r)
{
	const double sinr = std::sin(r), cosr = std::cos(r);
	mat->Zero();
	mat->m22 = mat->m33 = float(cosr);
	mat->m23 = -(mat->m32 = float(sinr));
	mat->m11 = mat->m44 = 1.0f;
}

inline void VkMatRotY(oapi::FMATRIX4 *mat, double r)
{
	const double sinr = std::sin(r), cosr = std::cos(r);
	mat->Zero();
	mat->m11 = mat->m33 = float(cosr);
	mat->m31 = -(mat->m13 = float(sinr));
	mat->m22 = mat->m44 = 1.0f;
}

inline void VkMatRotZ(oapi::FMATRIX4 *mat, double r)
{
	const double sinr = std::sin(r), cosr = std::cos(r);
	mat->Zero();
	mat->m11 = mat->m22 = float(cosr);
	mat->m12 = float(sinr);
	mat->m21 = float(-sinr);
	mat->m33 = mat->m44 = 1.0f;
}

inline void VkMatCreateXBillboard(const oapi::FVECTOR3 *toCam, const oapi::FVECTOR3 *pos, float size, oapi::FMATRIX4 *pOut)
{
	const float xzLenSq = toCam->x * toCam->x + toCam->z * toCam->z;
	const float hz = (xzLenSq > 1e-12f) ? (1.0f / std::sqrtf(xzLenSq)) : 1.0f;

	pOut->m11 = toCam->x;
	pOut->m12 = toCam->y;
	pOut->m13 = toCam->z;
	pOut->m31 = (xzLenSq > 1e-12f) ? (-toCam->z * hz) : 0.0f;
	pOut->m32 = 0.0f;
	pOut->m33 = (xzLenSq > 1e-12f) ? (toCam->x * hz) : 1.0f;
	pOut->m21 = -pOut->m12 * pOut->m33;
	pOut->m22 =  pOut->m33 * pOut->m11 - pOut->m13 * pOut->m31;
	pOut->m23 =  pOut->m31 * pOut->m12;
	pOut->m41 = pos->x;
	pOut->m42 = pos->y;
	pOut->m43 = pos->z;
	pOut->m14 = pOut->m24 = pOut->m34 = 0.0f;
	pOut->m44 = 1.0f;

	pOut->m11 *= size; pOut->m12 *= size; pOut->m13 *= size;
	pOut->m21 *= size; pOut->m22 *= size; pOut->m23 *= size;
	pOut->m31 *= size;                    pOut->m33 *= size;
}

inline void VkMatCreateXBillboard(
	const oapi::FVECTOR3 *toCam,
	const oapi::FVECTOR3 *pos,
	const oapi::FVECTOR3 *dir,
	float size,
	float stretch,
	oapi::FMATRIX4 *pOut)
{
	oapi::FVECTOR3 q = oapi::unit(oapi::cross(*dir, *toCam));
	oapi::FVECTOR3 w = oapi::unit(oapi::cross(q, *dir));

	pOut->m11 = w.x * size; pOut->m12 = w.y * size; pOut->m13 = w.z * size;
	pOut->m21 = q.x * size; pOut->m22 = q.y * size; pOut->m23 = q.z * size;
	pOut->m31 = dir->x * stretch; pOut->m32 = dir->y * stretch; pOut->m33 = dir->z * stretch;
	pOut->m41 = pos->x; pOut->m42 = pos->y; pOut->m43 = pos->z;
	pOut->m14 = pOut->m24 = pOut->m34 = 0.0f;
	pOut->m44 = 1.0f;
}

inline float VkMatBSScaleFactor(const oapi::FMATRIX4 *mat)
{
	const float lx = mat->m11 * mat->m11 + mat->m12 * mat->m12 + mat->m13 * mat->m13;
	const float ly = mat->m21 * mat->m21 + mat->m22 * mat->m22 + mat->m23 * mat->m23;
	const float lz = mat->m31 * mat->m31 + mat->m32 * mat->m32 + mat->m33 * mat->m33;
	return std::sqrtf((std::max)((std::max)(lx, ly), lz));
}

inline bool VkMatVectorMatrixMultiply(oapi::FVECTOR3 *res, const oapi::FVECTOR3 *v, const oapi::FMATRIX4 *mat)
{
	const float x = v->x * mat->m11 + v->y * mat->m21 + v->z * mat->m31 + mat->m41;
	const float y = v->x * mat->m12 + v->y * mat->m22 + v->z * mat->m32 + mat->m42;
	const float z = v->x * mat->m13 + v->y * mat->m23 + v->z * mat->m33 + mat->m43;
	const float w = v->x * mat->m14 + v->y * mat->m24 + v->z * mat->m34 + mat->m44;
	if (std::fabsf(w) < 1e-5f)
		return false;
	res->x = x / w;
	res->y = y / w;
	res->z = z / w;
	return true;
}

inline bool VkMatInvertAffine(oapi::FMATRIX4 *res, const oapi::FMATRIX4 &a)
{
	if (std::fabsf(a.m44 - 1.0f) > 0.001f)
		return false;
	if (std::fabsf(a.m14) > 0.001f || std::fabsf(a.m24) > 0.001f || std::fabsf(a.m34) > 0.001f)
		return false;

	const float det =
		a.m11 * (a.m22 * a.m33 - a.m23 * a.m32) -
		a.m12 * (a.m21 * a.m33 - a.m23 * a.m31) +
		a.m13 * (a.m21 * a.m32 - a.m22 * a.m31);
	if (std::fabsf(det) < 1e-20f)
		return false;
	const float fDetInv = 1.0f / det;

	res->m11 =  fDetInv * (a.m22 * a.m33 - a.m23 * a.m32);
	res->m12 = -fDetInv * (a.m12 * a.m33 - a.m13 * a.m32);
	res->m13 =  fDetInv * (a.m12 * a.m23 - a.m13 * a.m22);
	res->m14 = 0.0f;

	res->m21 = -fDetInv * (a.m21 * a.m33 - a.m23 * a.m31);
	res->m22 =  fDetInv * (a.m11 * a.m33 - a.m13 * a.m31);
	res->m23 = -fDetInv * (a.m11 * a.m23 - a.m13 * a.m21);
	res->m24 = 0.0f;

	res->m31 =  fDetInv * (a.m21 * a.m32 - a.m22 * a.m31);
	res->m32 = -fDetInv * (a.m11 * a.m32 - a.m12 * a.m31);
	res->m33 =  fDetInv * (a.m11 * a.m22 - a.m12 * a.m21);
	res->m34 = 0.0f;

	res->m41 = -(a.m41 * res->m11 + a.m42 * res->m21 + a.m43 * res->m31);
	res->m42 = -(a.m41 * res->m12 + a.m42 * res->m22 + a.m43 * res->m32);
	res->m43 = -(a.m41 * res->m13 + a.m42 * res->m23 + a.m43 * res->m33);
	res->m44 = 1.0f;
	return true;
}

inline bool VkMatInvert(oapi::FMATRIX4 *res, const oapi::FMATRIX4 &m)
{
	const float *a = m.data;
	float inv[16];

	inv[0] =   a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15]
	         + a[9] * a[7]  * a[14] + a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
	inv[4] =  -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15]
	         - a[8] * a[7]  * a[14] - a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
	inv[8] =   a[4] * a[9]  * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15]
	         + a[8] * a[7]  * a[13] + a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
	inv[12] = -a[4] * a[9]  * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14]
	         - a[8] * a[6]  * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9];

	inv[1] =  -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15]
	         - a[9] * a[3]  * a[14] - a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
	inv[5] =   a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15]
	         + a[8] * a[3]  * a[14] + a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
	inv[9] =  -a[0] * a[9]  * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15]
	         - a[8] * a[3]  * a[13] - a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
	inv[13] =  a[0] * a[9]  * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14]
	         + a[8] * a[2]  * a[13] + a[12] * a[1] * a[10] - a[12] * a[2] * a[9];

	inv[2] =   a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15]
	         + a[5] * a[3] * a[14] + a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
	inv[6] =  -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15]
	         - a[4] * a[3] * a[14] - a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
	inv[10] =  a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15]
	         + a[4] * a[3] * a[13] + a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
	inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14]
	         - a[4] * a[2] * a[13] - a[12] * a[1] * a[6] + a[12] * a[2] * a[5];

	inv[3] =  -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11]
	         - a[5] * a[3] * a[10] - a[9] * a[2] * a[7]  + a[9] * a[3] * a[6];
	inv[7] =   a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11]
	         + a[4] * a[3] * a[10] + a[8] * a[2] * a[7]  - a[8] * a[3] * a[6];
	inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9]  + a[4] * a[1] * a[11]
	         - a[4] * a[3] * a[9]  - a[8] * a[1] * a[7]  + a[8] * a[3] * a[5];
	inv[15] =  a[0] * a[5] * a[10] - a[0] * a[6] * a[9]  - a[4] * a[1] * a[10]
	         + a[4] * a[2] * a[9]  + a[8] * a[1] * a[6]  - a[8] * a[2] * a[5];

	const float det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
	if (std::fabsf(det) < 1e-24f)
		return false;

	const float invDet = 1.0f / det;
	for (int i = 0; i < 16; ++i)
		res->data[i] = inv[i] * invDet;
	return true;
}

inline oapi::FMATRIX4 VkMatPerspectiveFovLH(float fovy, float aspect, float zn, float zf)
{
	const float yScale = 1.0f / std::tanf(fovy * 0.5f);
	const float xScale = yScale / aspect;
	oapi::FMATRIX4 m;
	m.Zero();
	m.m11 = xScale;
	m.m22 = yScale;
	m.m33 = zf / (zf - zn);
	m.m34 = 1.0f;
	m.m43 = (-zn * zf) / (zf - zn);
	return m;
}

inline oapi::FMATRIX4 VkMatOrthoOffCenterLH(float l, float r, float b, float t, float zn, float zf)
{
	oapi::FMATRIX4 m;
	m.Zero();
	m.m11 = 2.0f / (r - l);
	m.m22 = 2.0f / (t - b);
	m.m33 = 1.0f / (zf - zn);
	m.m41 = (l + r) / (l - r);
	m.m42 = (t + b) / (b - t);
	m.m43 = zn / (zn - zf);
	m.m44 = 1.0f;
	return m;
}

#endif // !__VKCLIENT_VKMATH_H
