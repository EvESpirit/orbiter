// ==============================================================
// VkVertex.h
// Shared 3D vertex formats for VulkanClient
// Part of the ORBITER VISUALISATION PROJECT (OVP) - VulkanClient
// Licensed under the MIT License
// ==============================================================

#ifndef __VKCLIENT_VKVERTEX_H
#define __VKCLIENT_VKVERTEX_H

#include <cstdint>

struct VkVertexXYZ {
	float x, y, z;
};

struct VkVertexXYZC {
	float x, y, z;
	uint32_t col; // 0xAARRGGBB -> little-endian [B,G,R,A] (VK_FORMAT_B8G8R8A8_UNORM)
};

struct VkVertexXYZTex {
	float x, y, z;
	float tu, tv;
};

struct VkVertex2Tex {
	float x, y, z;
	float nx, ny, nz;
	float tu0, tv0;
	float tu1, tv1;
};

// Normal-mapped mesh vertex matching D3D9 NMVERTEX (44 bytes)
struct VkNMVERTEX {
	float x, y, z;
	float nx, ny, nz;
	float tx, ty, tz;
	float tu, tv;
};

// Shadow volume mesh vertex matching D3D9 SMVERTEX (24 bytes)
struct VkSMVERTEX {
	float x, y, z;
	float nx, ny, nz;
};

#endif // !__VKCLIENT_VKVERTEX_H
