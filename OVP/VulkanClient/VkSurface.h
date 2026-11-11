// ==============================================================
// VkSurface.h
// Native Vulkan surface wrapper (VkSurfNative) and DDS/WIC texture loader
// Part of the ORBITER VISUALISATION PROJECT (OVP) - VulkanClient
// Licensed under the MIT License
// ==============================================================

#ifndef __VKCLIENT_VKSURFACE_H
#define __VKCLIENT_VKSURFACE_H

#include "Orbitersdk.h"
#include "GraphicsAPI.h"
#include "VkAPI.h"
#include <cstdint>
#include <string>
#include <vector>

class VkContext;
class VkSwapchain;

namespace oapi {
class VulkanClient;
}

enum VkTexMapSlot : uint32_t {
	MAP_NORMAL        = 0,
	MAP_SPECULAR      = 1,
	MAP_EMISSION      = 2,
	MAP_REFLECTION    = 3,
	MAP_TRANSLUCENCE  = 4,
	MAP_TRANSMITTANCE = 5,
	MAP_ROUGHNESS     = 6,
	MAP_METALNESS     = 7,
	MAP_HEAT          = 8,
	MAP_MAX_COUNT     = 9
};

constexpr DWORD OAPISURFACE_MAPS       = 0x80000000;
constexpr DWORD OAPISURFACE_BACKBUFFER = 0x40000000;
constexpr DWORD OAPISURFACE_ORIGIN     = 0x20000000;
constexpr DWORD OAPISURFACE_CAPTURE    = 0x10000000;

#ifndef OAPISURFACE_PF_MASK
#define OAPISURFACE_PF_MASK    0xFF0000
#define OAPISURFACE_PF_XRGB    0x010000
#define OAPISURFACE_PF_ARGB    0x020000
#define OAPISURFACE_PF_RGB565  0x030000
#define OAPISURFACE_PF_S16R    0x040000
#define OAPISURFACE_PF_F32R    0x050000
#define OAPISURFACE_PF_F32RG   0x060000
#define OAPISURFACE_PF_F32RGBA 0x070000
#define OAPISURFACE_PF_F16R    0x080000
#define OAPISURFACE_PF_F16RG   0x090000
#define OAPISURFACE_PF_F16RGBA 0x0A0000
#define OAPISURFACE_PF_DXT1    0x0B0000
#define OAPISURFACE_PF_DXT3    0x0C0000
#define OAPISURFACE_PF_DXT5    0x0D0000
#define OAPISURFACE_PF_ALPHA   0x0E0000
#define OAPISURFACE_PF_GRAY    0x0F0000
#endif

/**
 * \brief Backing Vulkan 1.3 implementation for every Orbiter `SURFHANDLE`.
 */
class VkSurfNative {
public:
	VkSurfNative(VkContext &context, VkSwapchain &swap);
	~VkSurfNative();

	VkSurfNative(const VkSurfNative &) = delete;
	VkSurfNative &operator=(const VkSurfNative &) = delete;

	static VkSurfNative *Create(
		VkContext &context,
		VkSwapchain &swap,
		uint32_t width,
		uint32_t height,
		DWORD flags);

	static VkSurfNative *LoadFromFile(
		oapi::VulkanClient &client,
		VkContext &context,
		VkSwapchain &swap,
		const char *fname,
		DWORD flags,
		bool bPath,
		bool loadAuxMaps = true);

	bool CopyFromHBITMAP(HBITMAP hbm, int srcX, int srcY, int srcW, int srcH);
	bool ReadbackBgra(std::vector<uint8_t> &outBgra, uint32_t &outWidth, uint32_t &outHeight);
	bool Decompress();

	void TransitionLayout(
		VkCommandBuffer cmd,
		VkImageLayout newLayout,
		VkPipelineStageFlags2 srcStage,
		VkAccessFlags2 srcAccess,
		VkPipelineStageFlags2 dstStage,
		VkAccessFlags2 dstAccess);

	void TransitionToShaderRead(VkCommandBuffer cmd);
	void TransitionToTransferDst(VkCommandBuffer cmd);
	void TransitionToTransferSrc(VkCommandBuffer cmd);
	void TransitionToColorAttachment(VkCommandBuffer cmd);

	VkDescriptorSet GetImGuiDescriptorSet(VkSampler sampler);
	void ReleaseImGuiDescriptorSet();
	void ClearImGuiDescriptorSet() { imguiDescSet = VK_NULL_HANDLE; }

	void IncRef() { ++refCount; }
	bool DecRef() { return --refCount <= 0; }
	int RefCount() const { return refCount; }

	void SetManaged(bool managed) { isManaged = managed; }
	bool IsManaged() const { return isManaged; }

	void SetName(const char *surfName);
	const char *GetName() const { return name; }

	void SetColorKey(DWORD ck) { colorKey = ck; }
	DWORD GetColorKey() const { return colorKey; }
	bool IsColorKeyEnabled() const { return colorKey != SURF_NO_CK; }

	uint32_t GetWidth() const { return width; }
	uint32_t GetHeight() const { return height; }
	uint32_t GetMipMaps() const { return mipLevels; }
	DWORD GetOAPIFlags() const { return flags; }
	DWORD *GetClientFlags() { return &clientFlags; }

	VkFormat GetFormat() const { return format; }
	VkImage GetImage() const { return image; }
	VkImageView GetImageView() const { return imageView; }
	VkImageLayout GetLayout() const { return currentLayout; }

	VkImage GetDepthImage() const { return depthImage; }
	VkImageView GetDepthView() const { return depthView; }
	VkFormat GetDepthFormat() const { return depthFormat; }

	bool IsCompressed() const;
	bool IsRenderTarget() const { return (flags & (OAPISURFACE_RENDERTARGET | OAPISURFACE_SKETCHPAD | OAPISURFACE_RENDER3D | OAPISURFACE_BACKBUFFER)) != 0; }
	bool Is3DRenderTarget() const { return depthImage != VK_NULL_HANDLE && (flags & OAPISURFACE_RENDER3D) != 0; }
	bool IsGDISurface() const { return (flags & (OAPISURFACE_GDI | OAPISURFACE_SYSMEM)) != 0; }
	bool HasAlpha() const { return hasAlpha; }

	void SetMap(uint32_t slot, VkSurfNative *mapSurf);
	VkSurfNative *GetMap(uint32_t slot) const { return (slot < MAP_MAX_COUNT) ? pMap[slot] : nullptr; }
	VkSurfNative *GetMap(uint32_t slot1, uint32_t slot2) const {
		VkSurfNative *m = GetMap(slot1);
		return m ? m : GetMap(slot2);
	}

private:
	bool AllocateGpuImage(
		uint32_t w,
		uint32_t h,
		uint32_t mips,
		VkFormat fmt,
		VkImageUsageFlags usage,
		bool alpha,
		bool createDepth);

	bool UploadMipChain(
		const uint8_t *data,
		size_t dataSize,
		const std::vector<size_t> &mipOffsets,
		const std::vector<size_t> &mipSizes);

	void DestroyGpuResources();

	VkContext *ctx = nullptr;
	VkSwapchain *swapchain = nullptr;

	char name[128]{};
	std::string sourcePath;

	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t mipLevels = 1;
	DWORD flags = 0;
	DWORD colorKey = SURF_NO_CK;
	DWORD clientFlags = 0;
	int refCount = 1;
	bool isManaged = false;
	bool hasAlpha = false;

	VkFormat format = VK_FORMAT_B8G8R8A8_UNORM;
	VkImageUsageFlags imageUsage = 0;
	VkImage image = VK_NULL_HANDLE;
	VmaAllocation allocation = VK_NULL_HANDLE;
	VkImageView imageView = VK_NULL_HANDLE;
	VkImageLayout currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	VkImage depthImage = VK_NULL_HANDLE;
	VmaAllocation depthAlloc = VK_NULL_HANDLE;
	VkImageView depthView = VK_NULL_HANDLE;
	VkFormat depthFormat = VK_FORMAT_D24_UNORM_S8_UINT;

	VkDescriptorSet imguiDescSet = VK_NULL_HANDLE;
	VkSurfNative *pMap[MAP_MAX_COUNT]{};

	// CPU copy of compressed mip0 blocks (kept only for compressed surfaces to support CPU readback/Decompress)
	std::vector<uint8_t> compressedMip0;
};

#endif // !__VKCLIENT_VKSURFACE_H
