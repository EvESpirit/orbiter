// ==============================================================
// VkPad.cpp
// 2D Sketchpad implementation for the Vulkan Graphics Client
// Part of the ORBITER VISUALISATION PROJECT (OVP) - VulkanClient
// Licensed under the MIT License
// ==============================================================

#include "VkPad.h"

#include "VulkanClient.h"
#include "VkContext.h"
#include "VkSwapchain.h"
#include "VkSurface.h"
#include "VkTextMgr.h"
#include "VkPadTools.h"
#include "VkLog.h"

#include "Shaders/SketchpadVertSpv.h"
#include "Shaders/SketchpadFragSpv.h"

#include <algorithm>
#include <cmath>
#include <cstring>

using oapi::FMATRIX4;
using oapi::FVECTOR2;
using oapi::FVECTOR3;
using oapi::FVECTOR4;
using oapi::IVECTOR2;

namespace {

constexpr float kPi = 3.14159265358979323846f;

inline uint32_t ForceOpaqueIfZeroAlpha(uint32_t col)
{
	return (col & 0xFF000000u) == 0u ? (col | 0xFF000000u) : col;
}

inline void UnpackAbgrToFloat4(uint32_t abgr, float out[4])
{
	out[0] = float((abgr >> 0) & 0xFFu) * (1.0f / 255.0f);
	out[1] = float((abgr >> 8) & 0xFFu) * (1.0f / 255.0f);
	out[2] = float((abgr >> 16) & 0xFFu) * (1.0f / 255.0f);
	out[3] = float((abgr >> 24) & 0xFFu) * (1.0f / 255.0f);
}

inline FMATRIX4 MatIdentity()
{
	FMATRIX4 m;
	m.Ident();
	return m;
}

inline FMATRIX4 MatMul(const FMATRIX4 &a, const FMATRIX4 &b)
{
	FMATRIX4 r;
	r.Zero();
	for (int row = 0; row < 4; ++row) {
		for (int col = 0; col < 4; ++col) {
			float s = 0.0f;
			for (int k = 0; k < 4; ++k)
				s += a.data[row * 4 + k] * b.data[k * 4 + col];
			r.data[row * 4 + col] = s;
		}
	}
	return r;
}

inline FMATRIX4 MatOrthoOffCenterLH(float l, float r, float b, float t, float zn, float zf)
{
	FMATRIX4 m;
	m.Zero();
	const float invW = (r != l) ? (1.0f / (r - l)) : 1.0f;
	const float invH = (t != b) ? (1.0f / (t - b)) : 1.0f;
	const float invZ = (zf != zn) ? (1.0f / (zf - zn)) : 1.0f;
	m.m11 = 2.0f * invW;
	m.m22 = 2.0f * invH;
	m.m33 = invZ;
	m.m41 = -(l + r) * invW;
	m.m42 = -(t + b) * invH;
	m.m43 = -zn * invZ;
	m.m44 = 1.0f;
	return m;
}

inline FMATRIX4 MatTranslation(float x, float y, float z)
{
	FMATRIX4 m = MatIdentity();
	m.m41 = x;
	m.m42 = y;
	m.m43 = z;
	return m;
}

inline FMATRIX4 MatScaling(float sx, float sy, float sz)
{
	FMATRIX4 m;
	m.Zero();
	m.m11 = sx;
	m.m22 = sy;
	m.m33 = sz;
	m.m44 = 1.0f;
	return m;
}

inline FMATRIX4 MatRotationZ(float angleRad)
{
	FMATRIX4 m = MatIdentity();
	const float c = std::cos(angleRad);
	const float s = std::sin(angleRad);
	m.m11 = c;  m.m12 = s;
	m.m21 = -s; m.m22 = c;
	return m;
}

inline float Cross2D(const FVECTOR2 &a, const FVECTOR2 &b, const FVECTOR2 &c)
{
	return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

bool PointInTriangle2D(const FVECTOR2 &p, const FVECTOR2 &a, const FVECTOR2 &b, const FVECTOR2 &c)
{
	const float c1 = Cross2D(a, b, p);
	const float c2 = Cross2D(b, c, p);
	const float c3 = Cross2D(c, a, p);
	const bool hasNeg = (c1 < 0.0f) || (c2 < 0.0f) || (c3 < 0.0f);
	const bool hasPos = (c1 > 0.0f) || (c2 > 0.0f) || (c3 > 0.0f);
	return !(hasNeg && hasPos);
}

// Ear-clipping triangulation for arbitrary simple 2D polygons (CW or CCW)
void TriangulatePolygon2D(const std::vector<FVECTOR2> &pts, std::vector<uint32_t> &outLocalIndices)
{
	outLocalIndices.clear();
	size_t n = pts.size();
	if (n < 3)
		return;

	if (pts.back().x == pts.front().x && pts.back().y == pts.front().y)
		--n;
	if (n < 3)
		return;

	if (n == 3) {
		outLocalIndices = { 0, 1, 2 };
		return;
	}

	double signedArea2 = 0.0;
	for (size_t i = 0; i < n; ++i) {
		const FVECTOR2 &p0 = pts[i];
		const FVECTOR2 &p1 = pts[(i + 1) % n];
		signedArea2 += double(p0.x) * double(p1.y) - double(p1.x) * double(p0.y);
	}
	const float orient = (signedArea2 >= 0.0) ? 1.0f : -1.0f;

	std::vector<uint32_t> ring(n);
	for (size_t i = 0; i < n; ++i)
		ring[i] = uint32_t(i);

	outLocalIndices.reserve((n - 2) * 3);
	size_t guard = n * n;
	size_t idx = 0;

	while (ring.size() > 3 && guard-- > 0) {
		const size_t m = ring.size();
		bool clipped = false;

		for (size_t k = 0; k < m; ++k) {
			const size_t iPrev = (idx + k + m - 1) % m;
			const size_t iCurr = (idx + k) % m;
			const size_t iNext = (idx + k + 1) % m;

			const FVECTOR2 &a = pts[ring[iPrev]];
			const FVECTOR2 &b = pts[ring[iCurr]];
			const FVECTOR2 &c = pts[ring[iNext]];

			if (Cross2D(a, b, c) * orient < -1e-5f)
				continue;

			bool containsOther = false;
			for (size_t j = 0; j < m; ++j) {
				if (j == iPrev || j == iCurr || j == iNext)
					continue;
				if (PointInTriangle2D(pts[ring[j]], a, b, c)) {
					containsOther = true;
					break;
				}
			}
			if (containsOther)
				continue;

			outLocalIndices.push_back(ring[iPrev]);
			outLocalIndices.push_back(ring[iCurr]);
			outLocalIndices.push_back(ring[iNext]);
			ring.erase(ring.begin() + ptrdiff_t(iCurr));
			idx = iCurr % ring.size();
			clipped = true;
			break;
		}

		if (!clipped)
			break;
	}

	for (size_t k = 1; k + 1 < ring.size(); ++k) {
		outLocalIndices.push_back(ring[0]);
		outLocalIndices.push_back(ring[k]);
		outLocalIndices.push_back(ring[k + 1]);
	}
}

VkShaderModule CreateSpvModule(VkDevice device, const uint32_t *words, size_t byteSize)
{
	VkShaderModuleCreateInfo ci{};
	ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	ci.codeSize = byteSize;
	ci.pCode    = words;
	VkShaderModule mod = VK_NULL_HANDLE;
	if (vkCreateShaderModule(device, &ci, nullptr, &mod) != VK_SUCCESS)
		return VK_NULL_HANDLE;
	return mod;
}

inline VkDeviceSize AlignUp(VkDeviceSize val, VkDeviceSize align)
{
	return (val + align - 1) & ~(align - 1);
}

} // namespace

// ============================================================================
// VkPadPipeline
// ============================================================================

VkPadPipeline::~VkPadPipeline()
{
	Destroy();
}

bool VkPadPipeline::Init(VkContext &context, VkSwapchain &swap)
{
	Destroy();

	ctx = &context;
	swapchain = &swap;

	const VkPhysicalDeviceProperties &props = ctx->Properties();
	uboAlignment = std::max<VkDeviceSize>(256, props.limits.minUniformBufferOffsetAlignment);
	hasAnisotropy = (props.limits.maxSamplerAnisotropy >= 2.0f);

	vertShader = CreateSpvModule(ctx->Device(), g_SketchpadVertSpv, sizeof(g_SketchpadVertSpv));
	fragShader = CreateSpvModule(ctx->Device(), g_SketchpadFragSpv, sizeof(g_SketchpadFragSpv));
	if (vertShader == VK_NULL_HANDLE || fragShader == VK_NULL_HANDLE) {
		VkLogErr("VkPadPipeline: failed to create Sketchpad SPIR-V shader modules");
		Destroy();
		return false;
	}

	if (!CreateDescriptorAndPipelineLayouts() || !CreateSamplers() || !CreateHelperTextures()) {
		Destroy();
		return false;
	}

	for (uint32_t s = 0; s < kNumSlots; ++s) {
		slotPools[s].lastGen = UINT64_MAX;
		slotPools[s].vtxArena.defaultChunkBytes = 512u * 1024u;
		slotPools[s].vtxArena.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
		slotPools[s].vtxArena.align = 16;

		slotPools[s].idxArena.defaultChunkBytes = 256u * 1024u;
		slotPools[s].idxArena.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
		slotPools[s].idxArena.align = 16;

		slotPools[s].uboArena.defaultChunkBytes = 128u * 1024u;
		slotPools[s].uboArena.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
		slotPools[s].uboArena.align = uboAlignment;
	}

	VkLogInfo("VkPadPipeline initialized (uboAlign=%llu)", (unsigned long long)uboAlignment);
	return true;
}

void VkPadPipeline::Destroy()
{
	activePads.clear();
	freePads.clear();

	if (ctx && ctx->Device() != VK_NULL_HANDLE) {
		VkDevice dev = ctx->Device();
		vkDeviceWaitIdle(dev);

		for (auto &kv : pipelines) {
			if (kv.second != VK_NULL_HANDLE)
				vkDestroyPipeline(dev, kv.second, nullptr);
		}
		pipelines.clear();

		for (uint32_t s = 0; s < kNumSlots; ++s) {
			DestroyArena(slotPools[s].vtxArena);
			DestroyArena(slotPools[s].idxArena);
			DestroyArena(slotPools[s].uboArena);
			for (VkDescriptorPool p : slotPools[s].descPools) {
				if (p != VK_NULL_HANDLE)
					vkDestroyDescriptorPool(dev, p, nullptr);
			}
			slotPools[s].descPools.clear();
			slotPools[s].activeDescPool = 0;
			slotPools[s].lastGen = UINT64_MAX;
		}

		auto destroyImage = [&](VkImage &img, VmaAllocation &alloc, VkImageView &view) {
			if (view != VK_NULL_HANDLE) {
				vkDestroyImageView(dev, view, nullptr);
				view = VK_NULL_HANDLE;
			}
			if (img != VK_NULL_HANDLE) {
				vmaDestroyImage(ctx->Allocator(), img, alloc);
				img = VK_NULL_HANDLE;
				alloc = VK_NULL_HANDLE;
			}
		};
		destroyImage(whiteImage, whiteAlloc, whiteView);
		destroyImage(blackImage, blackAlloc, blackView);
		destroyImage(noiseImage, noiseAlloc, noiseView);

		auto destroySampler = [&](VkSampler &samp) {
			if (samp != VK_NULL_HANDLE) {
				vkDestroySampler(dev, samp, nullptr);
				samp = VK_NULL_HANDLE;
			}
		};
		destroySampler(samplerPoint);
		destroySampler(samplerLinear);
		destroySampler(samplerAniso);
		destroySampler(samplerNoise);

		if (pipeLayout != VK_NULL_HANDLE) {
			vkDestroyPipelineLayout(dev, pipeLayout, nullptr);
			pipeLayout = VK_NULL_HANDLE;
		}
		if (setLayout != VK_NULL_HANDLE) {
			vkDestroyDescriptorSetLayout(dev, setLayout, nullptr);
			setLayout = VK_NULL_HANDLE;
		}
		if (vertShader != VK_NULL_HANDLE) {
			vkDestroyShaderModule(dev, vertShader, nullptr);
			vertShader = VK_NULL_HANDLE;
		}
		if (fragShader != VK_NULL_HANDLE) {
			vkDestroyShaderModule(dev, fragShader, nullptr);
			fragShader = VK_NULL_HANDLE;
		}
	}

	ctx = nullptr;
	swapchain = nullptr;
}

bool VkPadPipeline::CreateDescriptorAndPipelineLayouts()
{
	VkDescriptorSetLayoutBinding bindings[4]{};
	bindings[0].binding         = 0;
	bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	bindings[0].descriptorCount = 1;
	bindings[0].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

	for (uint32_t i = 1; i <= 3; ++i) {
		bindings[i].binding         = i;
		bindings[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		bindings[i].descriptorCount = 1;
		bindings[i].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
	}

	VkDescriptorSetLayoutCreateInfo dslInfo{};
	dslInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	dslInfo.bindingCount = 4;
	dslInfo.pBindings    = bindings;

	if (vkCreateDescriptorSetLayout(ctx->Device(), &dslInfo, nullptr, &setLayout) != VK_SUCCESS) {
		VkLogErr("VkPadPipeline: vkCreateDescriptorSetLayout failed");
		return false;
	}

	VkPipelineLayoutCreateInfo plInfo{};
	plInfo.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	plInfo.setLayoutCount = 1;
	plInfo.pSetLayouts    = &setLayout;

	if (vkCreatePipelineLayout(ctx->Device(), &plInfo, nullptr, &pipeLayout) != VK_SUCCESS) {
		VkLogErr("VkPadPipeline: vkCreatePipelineLayout failed");
		return false;
	}

	return true;
}

bool VkPadPipeline::CreateSamplers()
{
	auto makeSampler = [&](VkFilter filter, VkSamplerMipmapMode mip, VkSamplerAddressMode addr, float aniso, VkSampler &out) -> bool {
		VkSamplerCreateInfo sci{};
		sci.sType            = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		sci.magFilter        = filter;
		sci.minFilter        = filter;
		sci.mipmapMode       = mip;
		sci.addressModeU     = addr;
		sci.addressModeV     = addr;
		sci.addressModeW     = addr;
		sci.anisotropyEnable = (aniso > 1.0f) ? VK_TRUE : VK_FALSE;
		sci.maxAnisotropy    = aniso;
		sci.minLod           = 0.0f;
		sci.maxLod           = VK_LOD_CLAMP_NONE;
		sci.borderColor      = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
		return vkCreateSampler(ctx->Device(), &sci, nullptr, &out) == VK_SUCCESS;
	};

	const float maxAniso = hasAnisotropy ? std::min(8.0f, ctx->Properties().limits.maxSamplerAnisotropy) : 1.0f;
	if (!makeSampler(VK_FILTER_NEAREST, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 1.0f, samplerPoint) ||
	    !makeSampler(VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 1.0f, samplerLinear) ||
	    !makeSampler(VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, maxAniso, samplerAniso) ||
	    !makeSampler(VK_FILTER_NEAREST, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_ADDRESS_MODE_REPEAT, 1.0f, samplerNoise))
	{
		VkLogErr("VkPadPipeline: failed to create samplers");
		return false;
	}

	return true;
}

bool VkPadPipeline::CreateHelperTextures()
{
	const uint8_t white[4] = { 255, 255, 255, 255 };
	if (!CreateStaticTexture(1, 1, white, whiteImage, whiteAlloc, whiteView))
		return false;

	const uint8_t black[4] = { 0, 0, 0, 255 };
	if (!CreateStaticTexture(1, 1, black, blackImage, blackAlloc, blackView))
		return false;

	constexpr uint32_t kNoiseDim = 128;
	std::vector<uint8_t> noise(kNoiseDim * kNoiseDim * 4);
	uint32_t rng = 0x13579BDFu;
	for (size_t i = 0; i < noise.size(); i += 4) {
		rng = rng * 1664525u + 1013904223u;
		const uint8_t v = uint8_t((rng >> 24) & 0xFFu);
		noise[i + 0] = v;
		noise[i + 1] = v;
		noise[i + 2] = v;
		noise[i + 3] = 255;
	}
	return CreateStaticTexture(kNoiseDim, kNoiseDim, noise.data(), noiseImage, noiseAlloc, noiseView);
}

bool VkPadPipeline::CreateStaticTexture(
	uint32_t width,
	uint32_t height,
	const uint8_t *bgraPixels,
	VkImage &outImage,
	VmaAllocation &outAlloc,
	VkImageView &outView)
{
	const VkDeviceSize byteSize = VkDeviceSize(width) * height * 4u;

	VkImageCreateInfo ici{};
	ici.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	ici.imageType     = VK_IMAGE_TYPE_2D;
	ici.format        = VK_FORMAT_B8G8R8A8_UNORM;
	ici.extent        = { width, height, 1 };
	ici.mipLevels     = 1;
	ici.arrayLayers   = 1;
	ici.samples       = VK_SAMPLE_COUNT_1_BIT;
	ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
	ici.usage         = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
	ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	VmaAllocationCreateInfo imgAllocInfo{};
	imgAllocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

	if (vmaCreateImage(ctx->Allocator(), &ici, &imgAllocInfo, &outImage, &outAlloc, nullptr) != VK_SUCCESS)
		return false;

	VkImageViewCreateInfo ivci{};
	ivci.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	ivci.image                           = outImage;
	ivci.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
	ivci.format                          = VK_FORMAT_B8G8R8A8_UNORM;
	ivci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
	ivci.subresourceRange.baseMipLevel   = 0;
	ivci.subresourceRange.levelCount     = 1;
	ivci.subresourceRange.baseArrayLayer = 0;
	ivci.subresourceRange.layerCount     = 1;

	if (vkCreateImageView(ctx->Device(), &ivci, nullptr, &outView) != VK_SUCCESS)
		return false;

	VkBufferCreateInfo bci{};
	bci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bci.size        = byteSize;
	bci.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VmaAllocationCreateInfo stgAllocInfo{};
	stgAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
	stgAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

	VkBuffer stgBuf = VK_NULL_HANDLE;
	VmaAllocation stgAlloc = VK_NULL_HANDLE;
	VmaAllocationInfo mapInfo{};
	if (vmaCreateBuffer(ctx->Allocator(), &bci, &stgAllocInfo, &stgBuf, &stgAlloc, &mapInfo) != VK_SUCCESS || !mapInfo.pMappedData) {
		if (stgBuf != VK_NULL_HANDLE)
			vmaDestroyBuffer(ctx->Allocator(), stgBuf, stgAlloc);
		return false;
	}

	std::memcpy(mapInfo.pMappedData, bgraPixels, size_t(byteSize));
	vmaFlushAllocation(ctx->Allocator(), stgAlloc, 0, byteSize);

	VkCommandBuffer cmd = swapchain->OffscreenCommandBuffer();
	if (cmd == VK_NULL_HANDLE) {
		vmaDestroyBuffer(ctx->Allocator(), stgBuf, stgAlloc);
		return false;
	}

	VkImageMemoryBarrier2 toDst{};
	toDst.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	toDst.srcStageMask        = VK_PIPELINE_STAGE_2_NONE;
	toDst.srcAccessMask       = VK_ACCESS_2_NONE;
	toDst.dstStageMask        = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
	toDst.dstAccessMask       = VK_ACCESS_2_TRANSFER_WRITE_BIT;
	toDst.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
	toDst.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toDst.image               = outImage;
	toDst.subresourceRange    = ivci.subresourceRange;

	VkDependencyInfo dep1{};
	dep1.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	dep1.imageMemoryBarrierCount = 1;
	dep1.pImageMemoryBarriers    = &toDst;
	vkCmdPipelineBarrier2(cmd, &dep1);

	VkBufferImageCopy2 copy{};
	copy.sType                           = VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2;
	copy.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
	copy.imageSubresource.mipLevel       = 0;
	copy.imageSubresource.baseArrayLayer = 0;
	copy.imageSubresource.layerCount     = 1;
	copy.imageExtent                     = { width, height, 1 };

	VkCopyBufferToImageInfo2 cpyInfo{};
	cpyInfo.sType          = VK_STRUCTURE_TYPE_COPY_BUFFER_TO_IMAGE_INFO_2;
	cpyInfo.srcBuffer      = stgBuf;
	cpyInfo.dstImage       = outImage;
	cpyInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	cpyInfo.regionCount    = 1;
	cpyInfo.pRegions       = &copy;
	vkCmdCopyBufferToImage2(cmd, &cpyInfo);

	VkImageMemoryBarrier2 toRead{};
	toRead.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	toRead.srcStageMask        = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
	toRead.srcAccessMask       = VK_ACCESS_2_TRANSFER_WRITE_BIT;
	toRead.dstStageMask        = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
	toRead.dstAccessMask       = VK_ACCESS_2_SHADER_READ_BIT;
	toRead.oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	toRead.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	toRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toRead.image               = outImage;
	toRead.subresourceRange    = ivci.subresourceRange;

	VkDependencyInfo dep2{};
	dep2.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	dep2.imageMemoryBarrierCount = 1;
	dep2.pImageMemoryBarriers    = &toRead;
	vkCmdPipelineBarrier2(cmd, &dep2);

	const bool ok = swapchain->FlushOffscreenCommands();
	vmaDestroyBuffer(ctx->Allocator(), stgBuf, stgAlloc);
	return ok;
}

VkSampler VkPadPipeline::SamplerForFilter(FilterMode mode) const
{
	switch (mode) {
	case FilterMode::Point: return samplerPoint;
	case FilterMode::Aniso: return samplerAniso;
	case FilterMode::Linear:
	default:                return samplerLinear;
	}
}

VkPipeline VkPadPipeline::GetOrCreatePipeline(
	VkFormat colorFormat,
	VkFormat depthFormat,
	ColorBlendOp blend,
	VkPrimitiveTopology topology,
	bool depthEnable)
{
	if (!IsInitialized() || colorFormat == VK_FORMAT_UNDEFINED)
		return VK_NULL_HANDLE;

	const bool useDepth = depthEnable && (depthFormat != VK_FORMAT_UNDEFINED);
	const bool isLine = (topology == VK_PRIMITIVE_TOPOLOGY_LINE_LIST);

	PipelineKey key{};
	key.colorFormat = uint32_t(colorFormat);
	key.depthFormat = useDepth ? uint32_t(depthFormat) : uint32_t(VK_FORMAT_UNDEFINED);
	key.blend       = uint8_t(blend);
	key.isLineList  = isLine ? 1 : 0;
	key.depthEnable = useDepth ? 1 : 0;

	if (auto it = pipelines.find(key); it != pipelines.end())
		return it->second;

	VkPipelineShaderStageCreateInfo stages[2]{};
	stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = vertShader;
	stages[0].pName  = "main";

	stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = fragShader;
	stages[1].pName  = "main";

	VkVertexInputBindingDescription vtxBind{};
	vtxBind.binding   = 0;
	vtxBind.stride    = sizeof(VkSkpVtx);
	vtxBind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	VkVertexInputAttributeDescription vtxAttrs[4]{};
	vtxAttrs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT,    offsetof(VkSkpVtx, x) };
	vtxAttrs[1] = { 1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(VkSkpVtx, nx) };
	vtxAttrs[2] = { 2, 0, VK_FORMAT_B8G8R8A8_UNORM,      offsetof(VkSkpVtx, clr) };
	vtxAttrs[3] = { 3, 0, VK_FORMAT_B8G8R8A8_UNORM,      offsetof(VkSkpVtx, fnc) };

	VkPipelineVertexInputStateCreateInfo vi{};
	vi.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vi.vertexBindingDescriptionCount   = 1;
	vi.pVertexBindingDescriptions      = &vtxBind;
	vi.vertexAttributeDescriptionCount = 4;
	vi.pVertexAttributeDescriptions    = vtxAttrs;

	VkPipelineInputAssemblyStateCreateInfo ia{};
	ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	ia.topology = isLine ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkPipelineViewportStateCreateInfo vp{};
	vp.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	vp.viewportCount = 1;
	vp.scissorCount  = 1;

	VkPipelineRasterizationStateCreateInfo rs{};
	rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rs.polygonMode = VK_POLYGON_MODE_FILL;
	rs.cullMode    = VK_CULL_MODE_NONE;
	rs.frontFace   = VK_FRONT_FACE_CLOCKWISE;
	rs.lineWidth   = 1.0f;

	VkPipelineMultisampleStateCreateInfo ms{};
	ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineDepthStencilStateCreateInfo ds{};
	ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	ds.depthTestEnable  = useDepth ? VK_TRUE : VK_FALSE;
	ds.depthWriteEnable = useDepth ? VK_TRUE : VK_FALSE;
	ds.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;

	VkPipelineColorBlendAttachmentState cbAtt{};
	switch (blend) {
	case ColorBlendOp::Copy:
		cbAtt.blendEnable    = VK_FALSE;
		cbAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		break;
	case ColorBlendOp::CopyAlpha:
		cbAtt.blendEnable    = VK_FALSE;
		cbAtt.colorWriteMask = VK_COLOR_COMPONENT_A_BIT;
		break;
	case ColorBlendOp::CopyColor:
		cbAtt.blendEnable    = VK_FALSE;
		cbAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT;
		break;
	case ColorBlendOp::AlphaBlend:
	default:
		cbAtt.blendEnable         = VK_TRUE;
		cbAtt.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		cbAtt.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		cbAtt.colorBlendOp        = VK_BLEND_OP_ADD;
		cbAtt.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		cbAtt.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		cbAtt.alphaBlendOp        = VK_BLEND_OP_ADD;
		cbAtt.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		break;
	}

	VkPipelineColorBlendStateCreateInfo cb{};
	cb.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	cb.attachmentCount = 1;
	cb.pAttachments    = &cbAtt;

	const VkDynamicState dynStates[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dyn{};
	dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dyn.dynamicStateCount = 2;
	dyn.pDynamicStates    = dynStates;

	VkPipelineRenderingCreateInfo rend{};
	rend.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	rend.colorAttachmentCount    = 1;
	rend.pColorAttachmentFormats = &colorFormat;
	rend.depthAttachmentFormat   = useDepth ? depthFormat : VK_FORMAT_UNDEFINED;

	VkGraphicsPipelineCreateInfo pci{};
	pci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pci.pNext               = &rend;
	pci.stageCount          = 2;
	pci.pStages             = stages;
	pci.pVertexInputState   = &vi;
	pci.pInputAssemblyState = &ia;
	pci.pViewportState      = &vp;
	pci.pRasterizationState = &rs;
	pci.pMultisampleState   = &ms;
	pci.pDepthStencilState  = &ds;
	pci.pColorBlendState    = &cb;
	pci.pDynamicState       = &dyn;
	pci.layout              = pipeLayout;

	VkPipeline pipe = VK_NULL_HANDLE;
	if (vkCreateGraphicsPipelines(ctx->Device(), VK_NULL_HANDLE, 1, &pci, nullptr, &pipe) != VK_SUCCESS) {
		VkLogErr("VkPadPipeline: vkCreateGraphicsPipelines failed (fmt=%d, blend=%d)", (int)colorFormat, (int)blend);
		return VK_NULL_HANDLE;
	}

	pipelines[key] = pipe;
	return pipe;
}

void VkPadPipeline::BeginSlotFrame(uint32_t slot, uint64_t slotGen)
{
	SlotPools &sp = slotPools[slot % kNumSlots];
	if (sp.lastGen == slotGen)
		return;

	sp.lastGen = slotGen;
	ResetArena(sp.vtxArena);
	ResetArena(sp.idxArena);
	ResetArena(sp.uboArena);

	for (VkDescriptorPool p : sp.descPools)
		vkResetDescriptorPool(ctx->Device(), p, 0);
	sp.activeDescPool = 0;
}

void VkPadPipeline::ResetArena(BufferArena &arena)
{
	for (BufferChunk &c : arena.chunks)
		c.used = 0;
}

void VkPadPipeline::DestroyArena(BufferArena &arena)
{
	for (BufferChunk &c : arena.chunks) {
		if (c.buffer != VK_NULL_HANDLE)
			vmaDestroyBuffer(ctx->Allocator(), c.buffer, c.alloc);
	}
	arena.chunks.clear();
}

VkPadPipeline::BufferSlice VkPadPipeline::AllocFromArena(BufferArena &arena, VkDeviceSize byteSize)
{
	if (byteSize == 0)
		return {};

	const VkDeviceSize alignedSize = AlignUp(byteSize, arena.align);

	for (BufferChunk &c : arena.chunks) {
		const VkDeviceSize off = AlignUp(c.used, arena.align);
		if (off + alignedSize <= c.size) {
			c.used = off + alignedSize;
			return { c.buffer, off, static_cast<uint8_t *>(c.mapped) + off };
		}
	}

	const VkDeviceSize newCap = std::max(arena.defaultChunkBytes, alignedSize * 2u);
	VkBufferCreateInfo bci{};
	bci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bci.size        = newCap;
	bci.usage       = arena.usage;
	bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VmaAllocationCreateInfo aci{};
	aci.usage = VMA_MEMORY_USAGE_AUTO;
	aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

	BufferChunk chunk{};
	VmaAllocationInfo mapInfo{};
	if (vmaCreateBuffer(ctx->Allocator(), &bci, &aci, &chunk.buffer, &chunk.alloc, &mapInfo) != VK_SUCCESS || !mapInfo.pMappedData) {
		if (chunk.buffer != VK_NULL_HANDLE)
			vmaDestroyBuffer(ctx->Allocator(), chunk.buffer, chunk.alloc);
		VkLogErr("VkPadPipeline: vmaCreateBuffer (%llu bytes) failed", (unsigned long long)newCap);
		return {};
	}

	chunk.size   = newCap;
	chunk.used   = alignedSize;
	chunk.mapped = mapInfo.pMappedData;
	arena.chunks.push_back(chunk);
	return { chunk.buffer, 0, chunk.mapped };
}

VkPadPipeline::BufferSlice VkPadPipeline::AllocVertices(uint32_t slot, VkDeviceSize byteSize)
{
	return AllocFromArena(slotPools[slot % kNumSlots].vtxArena, byteSize);
}

VkPadPipeline::BufferSlice VkPadPipeline::AllocIndices(uint32_t slot, VkDeviceSize byteSize)
{
	return AllocFromArena(slotPools[slot % kNumSlots].idxArena, byteSize);
}

VkPadPipeline::BufferSlice VkPadPipeline::AllocUniform(uint32_t slot, VkDeviceSize byteSize)
{
	return AllocFromArena(slotPools[slot % kNumSlots].uboArena, byteSize);
}

VkDescriptorPool VkPadPipeline::CreateDescriptorPool()
{
	constexpr uint32_t kMaxSets = 256;
	VkDescriptorPoolSize sizes[2]{};
	sizes[0] = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kMaxSets };
	sizes[1] = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxSets * 3 };

	VkDescriptorPoolCreateInfo pci{};
	pci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pci.maxSets       = kMaxSets;
	pci.poolSizeCount = 2;
	pci.pPoolSizes    = sizes;

	VkDescriptorPool pool = VK_NULL_HANDLE;
	if (vkCreateDescriptorPool(ctx->Device(), &pci, nullptr, &pool) != VK_SUCCESS)
		return VK_NULL_HANDLE;
	return pool;
}

VkDescriptorSet VkPadPipeline::AllocateDescriptorSet(uint32_t slot, VkDescriptorSetLayout customSetLayout)
{
	const VkDescriptorSetLayout layoutToUse = (customSetLayout != VK_NULL_HANDLE) ? customSetLayout : setLayout;
	SlotPools &sp = slotPools[slot % kNumSlots];
	while (true) {
		if (sp.activeDescPool >= sp.descPools.size()) {
			VkDescriptorPool np = CreateDescriptorPool();
			if (np == VK_NULL_HANDLE)
				return VK_NULL_HANDLE;
			sp.descPools.push_back(np);
		}

		VkDescriptorSetAllocateInfo ai{};
		ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		ai.descriptorPool     = sp.descPools[sp.activeDescPool];
		ai.descriptorSetCount = 1;
		ai.pSetLayouts        = &layoutToUse;

		VkDescriptorSet set = VK_NULL_HANDLE;
		const VkResult vr = vkAllocateDescriptorSets(ctx->Device(), &ai, &set);
		if (vr == VK_SUCCESS)
			return set;
		if (vr == VK_ERROR_OUT_OF_POOL_MEMORY || vr == VK_ERROR_FRAGMENTED_POOL) {
			++sp.activeDescPool;
			continue;
		}
		VkLogErr("VkPadPipeline: vkAllocateDescriptorSets failed (%d)", (int)vr);
		return VK_NULL_HANDLE;
	}
}

void VkPadPipeline::UpdateBatchDescriptorSet(
	VkDescriptorSet dstSet,
	const BufferSlice &uboSlice,
	VkImageView texView,
	VkSampler texSampler,
	VkImageView fntView)
{
	VkDescriptorBufferInfo bufInfo{};
	bufInfo.buffer = uboSlice.buffer;
	bufInfo.offset = uboSlice.offset;
	bufInfo.range  = sizeof(VkSkpUniforms);

	VkDescriptorImageInfo texInfo{};
	texInfo.sampler     = texSampler;
	texInfo.imageView   = (texView != VK_NULL_HANDLE) ? texView : whiteView;
	texInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkDescriptorImageInfo fntInfo{};
	fntInfo.sampler     = samplerLinear;
	fntInfo.imageView   = (fntView != VK_NULL_HANDLE) ? fntView : whiteView;
	fntInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkDescriptorImageInfo noiseInfo{};
	noiseInfo.sampler     = samplerNoise;
	noiseInfo.imageView   = noiseView;
	noiseInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkWriteDescriptorSet writes[4]{};
	writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[0].dstSet          = dstSet;
	writes[0].dstBinding      = 0;
	writes[0].descriptorCount = 1;
	writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	writes[0].pBufferInfo     = &bufInfo;

	writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[1].dstSet          = dstSet;
	writes[1].dstBinding      = 1;
	writes[1].descriptorCount = 1;
	writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	writes[1].pImageInfo      = &texInfo;

	writes[2].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[2].dstSet          = dstSet;
	writes[2].dstBinding      = 2;
	writes[2].descriptorCount = 1;
	writes[2].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	writes[2].pImageInfo      = &fntInfo;

	writes[3].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[3].dstSet          = dstSet;
	writes[3].dstBinding      = 3;
	writes[3].descriptorCount = 1;
	writes[3].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	writes[3].pImageInfo      = &noiseInfo;

	vkUpdateDescriptorSets(ctx->Device(), 4, writes, 0, nullptr);
}

oapi::Sketchpad *VkPadPipeline::AcquirePad(oapi::VulkanClient &client, oapi::Font *defaultFont, SURFHANDLE surf)
{
	if (!IsInitialized())
		return nullptr;

	if (surf != nullptr && !client.IsValidSurface(static_cast<const VkSurfNative *>(surf))) {
		VkLogWarn("VkPadPipeline::AcquirePad: invalid surface handle %p", surf);
		return nullptr;
	}

	if (!activePads.empty())
		activePads.back()->EndDrawing();

	std::unique_ptr<VkPad> pad;
	for (auto it = freePads.begin(); it != freePads.end(); ++it) {
		if ((*it)->GetSurface() == surf) {
			pad = std::move(*it);
			freePads.erase(it);
			break;
		}
	}

	if (!pad)
		pad = std::make_unique<VkPad>(client, *this, surf);
	else
		pad->ResetForSurface(surf, defaultFont);

	VkPad *raw = pad.release();
	raw->LoadDefaults(defaultFont);
	raw->BeginDrawing();
	activePads.push_back(raw);
	return raw;
}

void VkPadPipeline::ReleasePad(oapi::Sketchpad *sp)
{
	if (!sp)
		return;

	auto *pad = static_cast<VkPad *>(sp);
	pad->EndDrawing();

	if (!activePads.empty() && activePads.back() == pad) {
		activePads.pop_back();
		if (!activePads.empty())
			activePads.back()->BeginDrawing();
	} else {
		auto it = std::find(activePads.begin(), activePads.end(), pad);
		if (it != activePads.end())
			activePads.erase(it);
	}

	if (freePads.size() < 32)
		freePads.emplace_back(pad);
	else
		delete pad;
}

void VkPadPipeline::OnSurfaceDestroyed(SURFHANDLE surf)
{
	if (!surf)
		return;
	freePads.erase(
		std::remove_if(freePads.begin(), freePads.end(),
			[surf](const std::unique_ptr<VkPad> &p) { return p->GetSurface() == surf; }),
		freePads.end());
}

void VkPadPipeline::FlushActivePads()
{
	for (VkPad *pad : activePads) {
		if (pad)
			pad->Flush();
	}
}

// ============================================================================
// VkPad
// ============================================================================

VkPad::VkPad(oapi::VulkanClient &cli, VkPadPipeline &pipe, SURFHANDLE surf)
	: oapi::Sketchpad(surf), client(cli), pipeline(pipe), targetSurf(surf)
{
	nativeTarget = (surf != nullptr) ? static_cast<VkSurfNative *>(surf) : nullptr;
	LoadDefaults(nullptr);
}

void VkPad::ResetForSurface(SURFHANDLE surf, oapi::Font *defaultFont)
{
	targetSurf = surf;
	nativeTarget = (surf != nullptr) ? static_cast<VkSurfNative *>(surf) : nullptr;
	LoadDefaults(defaultFont);
}

void VkPad::LoadDefaults(oapi::Font *defaultFont)
{
	defFont = defaultFont;
	cfont = static_cast<VkPadFont *>(defaultFont);
	cpen = nullptr;
	cbrsh = nullptr;

	quickPenActive = false;
	quickPenColor = 0xFFFFFFFFu;
	quickPenWidth = 1.0f;
	quickPenDashed = false;
	quickBrushActive = false;
	quickBrushColor = 0xFFFFFFFFu;
	colorCompat = false;

	textColor = 0xFFFFFFFFu;
	bkColor = 0xFF000000u;
	bkMode = BK_TRANSPARENT;
	hAlign = LEFT;
	vAlign = TOP;
	origin = { 0, 0 };
	curPos = { 0, 0 };

	penWidthScale = 1.0f;
	patternScale = 1.0f;

	blendOp = VkPadPipeline::ColorBlendOp::AlphaBlend;
	texFilter = VkPadPipeline::FilterMode::Linear;
	depthEnable = false;
	viewMode = ORTHO;
	clipEnabled = false;
	clipRect = {};
	clipNear = -1000.0f;
	clipFar = 1000.0f;

	mWorld = MatIdentity();
	mView = MatIdentity();
	mProj = MatIdentity();
	worldStack.clear();

	mColorMatrix = MatIdentity();
	gammaVal = { 1.0f, 1.0f, 1.0f, 1.0f };
	noiseColor = { 0.0f, 0.0f, 0.0f, 1.0f };
	hasEffects = false;

	hasPendingClearColor = false;
	hasPendingClearDepth = false;
	pendingClearAbgr = 0;

	vertices.clear();
	indices.clear();
	batches.clear();
	didSubmitOnTarget = false;

	UpdateWVP();
}

void VkPad::BeginDrawing()
{
	UpdateWVP();
}

void VkPad::EndDrawing()
{
	Flush();

	if (nativeTarget && didSubmitOnTarget && client.IsValidSurface(nativeTarget)) {
		VkSwapchain *swap = pipeline.Swapchain();
		if (swap) {
			VkCommandBuffer cmd = swap->ActiveCommandBuffer();
			if (cmd != VK_NULL_HANDLE) {
				if (nativeTarget->GetMipMaps() > 1)
					nativeTarget->RegenerateMipmaps(cmd);
				else
					nativeTarget->TransitionToShaderRead(cmd);
				if (!swap->IsFrameRecording())
					swap->FlushOffscreenCommands();
			}
		}
	}
	didSubmitOnTarget = false;
}

bool VkPad::ResolveTargetInfo(
	VkImageView &outColorView,
	VkFormat &outColorFormat,
	VkExtent2D &outExtent,
	VkImageView &outDepthView,
	VkFormat &outDepthFormat) const
{
	VkSwapchain *swap = pipeline.Swapchain();
	if (!swap)
		return false;

	outDepthView = VK_NULL_HANDLE;
	outDepthFormat = VK_FORMAT_UNDEFINED;

	if (!targetSurf) {
		outColorView = swap->SceneImageView();
		outColorFormat = swap->SceneFormat();
		outExtent = swap->SceneExtent();
		return outColorView != VK_NULL_HANDLE && outExtent.width > 0 && outExtent.height > 0;
	}

	auto *surf = static_cast<VkSurfNative *>(targetSurf);
	if (!client.IsValidSurface(surf))
		return false;
	if (surf->IsCompressed() && !surf->Decompress())
		return false;

	outColorView = surf->GetRenderTargetView();
	outColorFormat = surf->GetFormat();
	outExtent = { surf->GetWidth(), surf->GetHeight() };
	return outColorView != VK_NULL_HANDLE && outExtent.width > 0 && outExtent.height > 0;
}

void VkPad::UpdateWVP()
{
	VkImageView rv = VK_NULL_HANDLE;
	VkFormat rf = VK_FORMAT_UNDEFINED;
	VkExtent2D ext{ 1, 1 };
	VkImageView dv = VK_NULL_HANDLE;
	VkFormat df = VK_FORMAT_UNDEFINED;
	if (!ResolveTargetInfo(rv, rf, ext, dv, df))
		ext = { 1024, 768 };

	if (viewMode == ORTHO) {
		mProj = MatOrthoOffCenterLH(0.0f, float(ext.width), float(ext.height), 0.0f, clipNear, clipFar);
		mView = MatIdentity();
		mVP = mProj;
	} else {
		mVP = MatMul(mView, mProj);
	}
	mWVP = MatMul(mWorld, mVP);
}

void VkPad::RefreshEffectsFlag()
{
	const FMATRIX4 id = MatIdentity();
	hasEffects =
		(std::memcmp(&mColorMatrix, &id, sizeof(FMATRIX4)) != 0) ||
		(gammaVal.x != 1.0f || gammaVal.y != 1.0f || gammaVal.z != 1.0f) ||
		(noiseColor.x != 0.0f || noiseColor.y != 0.0f || noiseColor.z != 0.0f);
}

uint32_t VkPad::ResolveCompatColor(DWORD color) const
{
	return colorCompat ? ForceOpaqueIfZeroAlpha(color) : color;
}

bool VkPad::HasActivePen() const
{
	if (quickPenActive)
		return quickPenWidth > 0.0f && quickPenColor != 0;
	return cpen != nullptr && !cpen->IsNull();
}

uint32_t VkPad::ActivePenColor() const
{
	if (quickPenActive)
		return quickPenColor;
	return cpen ? ForceOpaqueIfZeroAlpha(cpen->Color().dclr) : 0xFFFFFFFFu;
}

float VkPad::ActivePenWidth() const
{
	const float raw = quickPenActive ? quickPenWidth : (cpen ? float(cpen->Width()) : 0.0f);
	return std::max(1.0f, raw) * penWidthScale;
}

bool VkPad::ActivePenDashed() const
{
	return quickPenActive ? quickPenDashed : (cpen && cpen->IsDashed());
}

bool VkPad::HasActiveBrush() const
{
	if (quickBrushActive)
		return quickBrushColor != 0;
	return cbrsh != nullptr;
}

uint32_t VkPad::ActiveBrushColor() const
{
	if (quickBrushActive)
		return quickBrushColor;
	return cbrsh ? ForceOpaqueIfZeroAlpha(cbrsh->Color().dclr) : 0xFFFFFFFFu;
}

VkSkpUniforms VkPad::BuildUniforms(
	int32_t featureFlags,
	VkSurfNative *tex,
	uint32_t colorKeyAbgr,
	float effectivePenWidth,
	const FMATRIX4 *overrideWorld) const
{
	VkImageView rv = VK_NULL_HANDLE;
	VkFormat rf = VK_FORMAT_UNDEFINED;
	VkExtent2D ext{ 1, 1 };
	VkImageView dv = VK_NULL_HANDLE;
	VkFormat df = VK_FORMAT_UNDEFINED;
	if (!ResolveTargetInfo(rv, rf, ext, dv, df))
		ext = { 1, 1 };

	const FMATRIX4 wvp = overrideWorld ? MatMul(*overrideWorld, mVP) : mWVP;

	VkSkpUniforms u{};
	// Row-major FMATRIX4 copied linearly matches GLSL column-major matrix multiplication M * vec4
	std::memcpy(u.gWVP, &wvp, 64);
	std::memcpy(u.gColorMatrix, &mColorMatrix, 64);

	UnpackAbgrToFloat4(ActivePenColor(), u.gPen);
	UnpackAbgrToFloat4(colorKeyAbgr, u.gKey);

	u.gSize[0] = (tex && tex->GetWidth() > 0) ? (1.0f / float(tex->GetWidth())) : 1.0f;
	u.gSize[1] = (tex && tex->GetHeight() > 0) ? (1.0f / float(tex->GetHeight())) : 1.0f;
	u.gSize[2] = 1.0f;
	u.gSize[3] = 1.0f;

	u.gTarget[0] = 2.0f / float(std::max(1u, ext.width));
	u.gTarget[1] = 2.0f / float(std::max(1u, ext.height));
	u.gTarget[2] = float(std::max(1u, ext.width));
	u.gTarget[3] = float(std::max(1u, ext.height));

	u.gWidth[0] = effectivePenWidth;
	u.gWidth[1] = 0.08f * patternScale;
	u.gWidth[2] = 0.0f;
	u.gWidth[3] = 0.0f;

	u.gGamma[0] = gammaVal.x;
	u.gGamma[1] = gammaVal.y;
	u.gGamma[2] = gammaVal.z;
	u.gGamma[3] = gammaVal.w;

	u.gNoiseColor[0] = noiseColor.x;
	u.gNoiseColor[1] = noiseColor.y;
	u.gNoiseColor[2] = noiseColor.z;
	u.gNoiseColor[3] = noiseColor.w;

	u.gMisc[0] = 0.0f;
	u.gFlags[0] = featureFlags | (hasEffects ? VK_SKPF_EFFECTS : 0);
	return u;
}

VkPad::DrawBatch &VkPad::BeginBatch(
	bool isLineList,
	int32_t featureFlags,
	VkSurfNative *tex,
	VkSurfNative *fntTex,
	uint32_t colorKeyAbgr,
	float effectivePenWidth,
	const FMATRIX4 *overrideWorld)
{
	VkImageView rv = VK_NULL_HANDLE;
	VkFormat rf = VK_FORMAT_UNDEFINED;
	VkExtent2D ext{ 1, 1 };
	VkImageView dv = VK_NULL_HANDLE;
	VkFormat df = VK_FORMAT_UNDEFINED;
	if (!ResolveTargetInfo(rv, rf, ext, dv, df))
		ext = { 1, 1 };

	VkRect2D scissor{ { 0, 0 }, ext };
	if (clipEnabled) {
		const int32_t l = std::clamp<int32_t>(clipRect.left, 0, int32_t(ext.width));
		const int32_t t = std::clamp<int32_t>(clipRect.top, 0, int32_t(ext.height));
		const int32_t r = std::clamp<int32_t>(clipRect.right, l, int32_t(ext.width));
		const int32_t b = std::clamp<int32_t>(clipRect.bottom, t, int32_t(ext.height));
		scissor = { { l, t }, { uint32_t(r - l), uint32_t(b - t) } };
	}

	const VkSkpUniforms params = BuildUniforms(featureFlags, tex, colorKeyAbgr, effectivePenWidth, overrideWorld);

	if (!batches.empty()) {
		DrawBatch &back = batches.back();
		if (back.tex == tex &&
		    back.fntTex == fntTex &&
		    back.filter == texFilter &&
		    back.blend == blendOp &&
		    back.isLineList == isLineList &&
		    back.depthEnable == depthEnable &&
		    back.scissor.offset.x == scissor.offset.x &&
		    back.scissor.offset.y == scissor.offset.y &&
		    back.scissor.extent.width == scissor.extent.width &&
		    back.scissor.extent.height == scissor.extent.height &&
		    std::memcmp(&back.params, &params, sizeof(VkSkpUniforms)) == 0)
		{
			return back;
		}
	}

	DrawBatch nb{};
	nb.firstIndex  = uint32_t(indices.size());
	nb.indexCount  = 0;
	nb.params      = params;
	nb.tex         = tex;
	nb.fntTex      = fntTex;
	nb.filter      = texFilter;
	nb.blend       = blendOp;
	nb.isLineList  = isLineList;
	nb.depthEnable = depthEnable;
	nb.scissor     = scissor;
	batches.push_back(nb);
	return batches.back();
}

void VkPad::AppendSolidQuad(float x0, float y0, float x1, float y1, uint32_t abgr, const FMATRIX4 *overrideWorld)
{
	AppendGradientQuad(x0, y0, x1, y1, abgr, abgr, abgr, abgr, overrideWorld);
}

void VkPad::AppendGradientQuad(
	float x0,
	float y0,
	float x1,
	float y1,
	uint32_t cTL,
	uint32_t cTR,
	uint32_t cBR,
	uint32_t cBL,
	const FMATRIX4 *overrideWorld)
{
	DrawBatch &batch = BeginBatch(false, 0, nullptr, nullptr, 0, 1.0f, overrideWorld);
	const uint32_t base = uint32_t(vertices.size());
	const uint32_t sw = VK_SKPSW_CENTER | VK_SKPSW_FRAGMENT;

	vertices.push_back({ x0, y0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, cTL, sw });
	vertices.push_back({ x1, y0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, cTR, sw });
	vertices.push_back({ x1, y1, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, cBR, sw });
	vertices.push_back({ x0, y1, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, cBL, sw });

	indices.push_back(base + 0);
	indices.push_back(base + 1);
	indices.push_back(base + 2);
	indices.push_back(base + 0);
	indices.push_back(base + 2);
	indices.push_back(base + 3);
	batch.indexCount += 6;
}

void VkPad::AppendTexturedQuad(
	VkSurfNative *src,
	const FVECTOR2 pos[4],
	const FVECTOR2 uv[4],
	bool useColorKey,
	const FMATRIX4 *overrideWorld)
{
	if (!src || !client.IsValidSurface(src))
		return;
	if (src->IsCompressed() && !src->Decompress())
		return;

	const bool applyKey = useColorKey && src->IsColorKeyEnabled();
	const int32_t flags = VK_SKPF_TEX | (applyKey ? VK_SKPF_KEY : 0);
	const uint32_t sw = VK_SKPSW_CENTER | VK_SKPSW_TEXTURE | (applyKey ? VK_SKPSW_COLORKEY : 0u);
	const uint32_t keyAbgr = applyKey ? ForceOpaqueIfZeroAlpha(src->GetColorKey()) : 0u;

	DrawBatch &batch = BeginBatch(false, flags, src, nullptr, keyAbgr, 1.0f, overrideWorld);
	const uint32_t base = uint32_t(vertices.size());

	for (int i = 0; i < 4; ++i)
		vertices.push_back({ pos[i].x, pos[i].y, 0.0f, uv[i].x, uv[i].y, 0.0f, 0.0f, 0xFFFFFFFFu, sw });

	indices.push_back(base + 0);
	indices.push_back(base + 1);
	indices.push_back(base + 2);
	indices.push_back(base + 0);
	indices.push_back(base + 2);
	indices.push_back(base + 3);
	batch.indexCount += 6;
}

void VkPad::AppendStrokePolyline(const FVECTOR2 *pts, int npt, bool closed)
{
	if (!pts || npt < 2 || !HasActivePen())
		return;

	const float width = ActivePenWidth();
	if (width <= 1.05f)
		AppendHairlineSegments(pts, npt, closed, false);
	else
		AppendWidePolyline(pts, npt, closed);
}

void VkPad::AppendHairlineSegments(const FVECTOR2 *pts, int npt, bool closed, bool separatePairs)
{
	if (!pts || npt < 2 || !HasActivePen())
		return;

	const bool dashed = ActivePenDashed();
	const int32_t flags = dashed ? VK_SKPF_DASH : 0;
	DrawBatch &batch = BeginBatch(true, flags, nullptr, nullptr, 0, 1.0f);

	const uint32_t clr = ActivePenColor();
	const uint32_t sw0 = VK_SKPSW_CENTER | VK_SKPSW_PENCOLOR;
	const uint32_t sw1 = sw0 | (dashed ? VK_SKPSW_LENGTH : 0u);

	const int step = separatePairs ? 2 : 1;
	const int segCount = separatePairs ? (npt / 2) : (closed ? npt : (npt - 1));

	for (int s = 0; s < segCount; ++s) {
		const FVECTOR2 &a = pts[s * step];
		const FVECTOR2 &b = separatePairs ? pts[s * 2 + 1] : pts[(s + 1) % npt];
		float ax = a.x + 0.5f;
		float ay = a.y + 0.5f;
		float bx = b.x + 0.5f;
		float by = b.y + 0.5f;

		// Vulkan LINE_LIST drops max-coordinate endpoints on axis-aligned segments
		// regardless of draw direction. Orient non-dashed axis-aligned segments
		// along +X/+Y and extend past the far pixel boundary for [min..max] parity.
		if (!dashed) {
			const float dx = bx - ax;
			const float dy = by - ay;
			if (std::fabs(dx) < 1e-4f && std::fabs(dy) >= 1e-4f) {
				if (ay > by)
					std::swap(ay, by);
				by += 0.55f;
			} else if (std::fabs(dy) < 1e-4f && std::fabs(dx) >= 1e-4f) {
				if (ax > bx)
					std::swap(ax, bx);
				bx += 0.55f;
			}
		}

		const uint32_t base = uint32_t(vertices.size());
		vertices.push_back({ ax, ay, 0.0f, 0.0f, 0.0f, ax, ay, clr, sw0 });
		vertices.push_back({ bx, by, 0.0f, 0.0f, 0.0f, ax, ay, clr, sw1 });
		indices.push_back(base + 0);
		indices.push_back(base + 1);
		batch.indexCount += 2;
	}
}

void VkPad::AppendWidePolyline(const FVECTOR2 *pts, int npt, bool closed)
{
	if (!pts || npt < 2 || !HasActivePen())
		return;

	const float width = ActivePenWidth();
	const bool dashed = ActivePenDashed();
	const int32_t flags = VK_SKPF_WIDE | (dashed ? VK_SKPF_DASH : 0);
	DrawBatch &batch = BeginBatch(false, flags, nullptr, nullptr, 0, width);

	const uint32_t clr = ActivePenColor();
	const int roundedW = int(std::round(width));
	const float centerShift = (roundedW & 1) ? 0.5f : 0.0f;

	const uint32_t base = uint32_t(vertices.size());
	float cumLen = 0.0f;

	FVECTOR2 prevPt = closed
		? FVECTOR2{ pts[npt - 1].x + centerShift, pts[npt - 1].y + centerShift }
		: FVECTOR2{ (pts[0].x * 2.0f - pts[1].x) + centerShift, (pts[0].y * 2.0f - pts[1].y) + centerShift };

	for (int i = 0; i < npt; ++i) {
		const FVECTOR2 cur{ pts[i].x + centerShift, pts[i].y + centerShift };
		FVECTOR2 nextPt{};
		if (i + 1 < npt) {
			nextPt = { pts[i + 1].x + centerShift, pts[i + 1].y + centerShift };
		} else if (closed) {
			nextPt = { pts[0].x + centerShift, pts[0].y + centerShift };
		} else {
			nextPt = { (pts[i].x * 2.0f - pts[i - 1].x) + centerShift, (pts[i].y * 2.0f - pts[i - 1].y) + centerShift };
		}

		vertices.push_back({ cur.x, cur.y, cumLen, nextPt.x, nextPt.y, prevPt.x, prevPt.y, clr, VK_SKPSW_WIDEPEN_L | VK_SKPSW_PENCOLOR });
		vertices.push_back({ cur.x, cur.y, cumLen, nextPt.x, nextPt.y, prevPt.x, prevPt.y, clr, VK_SKPSW_WIDEPEN_R | VK_SKPSW_PENCOLOR });

		const float dx = nextPt.x - cur.x;
		const float dy = nextPt.y - cur.y;
		cumLen += std::sqrt(dx * dx + dy * dy);
		prevPt = cur;
	}

	if (closed) {
		VkSkpVtx vL = vertices[base + 0];
		VkSkpVtx vR = vertices[base + 1];
		vL.l = cumLen;
		vR.l = cumLen;
		vertices.push_back(vL);
		vertices.push_back(vR);
	}

	const int totalVerts = int(vertices.size() - base);
	for (int i = 0; i + 3 < totalVerts; i += 2) {
		const uint32_t v0 = base + uint32_t(i);
		indices.push_back(v0 + 0);
		indices.push_back(v0 + 1);
		indices.push_back(v0 + 2);
		indices.push_back(v0 + 1);
		indices.push_back(v0 + 3);
		indices.push_back(v0 + 2);
		batch.indexCount += 6;
	}
}

// --- Selection & State ---

oapi::Font *VkPad::SetFont(oapi::Font *font)
{
	oapi::Font *prev = cfont;
	if (font && client.IsValidFont(font))
		cfont = static_cast<VkPadFont *>(font);
	else
		cfont = static_cast<VkPadFont *>(defFont);
	return prev;
}

oapi::Pen *VkPad::SetPen(oapi::Pen *pen)
{
	oapi::Pen *prev = cpen;
	cpen = static_cast<VkPadPen *>(pen);
	quickPenActive = false;
	return prev;
}

oapi::Brush *VkPad::SetBrush(oapi::Brush *brush)
{
	oapi::Brush *prev = cbrsh;
	cbrsh = static_cast<VkPadBrush *>(brush);
	quickBrushActive = false;
	return prev;
}

void VkPad::SetTextAlign(TAlign_horizontal tah, TAlign_vertical tav)
{
	hAlign = tah;
	vAlign = tav;
}

DWORD VkPad::SetTextColor(DWORD col)
{
	const DWORD prev = textColor;
	textColor = ForceOpaqueIfZeroAlpha(col);
	return prev;
}

DWORD VkPad::SetBackgroundColor(DWORD col)
{
	const DWORD prev = bkColor;
	bkColor = ForceOpaqueIfZeroAlpha(col);
	return prev;
}

void VkPad::SetBackgroundMode(BkgMode mode)
{
	bkMode = mode;
}

DWORD VkPad::GetCharSize()
{
	return cfont ? cfont->GetCharSize() : 0;
}

DWORD VkPad::GetTextWidth(const char *str, int len)
{
	return cfont ? cfont->GetTextLength(str, len) : 0;
}

void VkPad::SetOrigin(int x, int y)
{
	origin.x = x;
	origin.y = y;
}

void VkPad::GetOrigin(int *x, int *y) const
{
	if (x) *x = origin.x;
	if (y) *y = origin.y;
}

// --- Text Rendering ---

bool VkPad::PrintCp1252Run(float x, float y, const char *cp1252, int len, float scale, float angle)
{
	if (!cfont || !cfont->IsValid() || !cp1252 || len <= 0)
		return false;

	const VkTextAtlasPtr &atlas = cfont->Atlas();
	if (!atlas)
		return false;

	VkContext *ctx = pipeline.Context();
	VkSwapchain *swap = pipeline.Swapchain();
	if (!ctx || !swap)
		return false;

	VkSurfNative *fntSurf = atlas->GetSurface(*ctx, *swap);
	if (!fntSurf)
		return false;

	const TEXTMETRICA &tm = atlas->Metrics();
	const float totalW = atlas->Length(cp1252, len) * scale;
	const float totalH = float(tm.tmHeight) * scale;

	float dx = 0.0f;
	switch (hAlign) {
	case CENTER: dx = -0.5f * totalW; break;
	case RIGHT:  dx = -totalW; break;
	case LEFT:
	default:     dx = 0.0f; break;
	}

	float dy = 0.0f;
	switch (vAlign) {
	case BASELINE: dy = -float(tm.tmAscent) * scale; break;
	case BOTTOM:   dy = -totalH; break;
	case TOP:
	default:       dy = 0.0f; break;
	}

	float totalRot = angle;
	if (cfont->Rotation() != 0.0f)
		totalRot += cfont->Rotation() * (kPi / 180.0f);

	const bool rotated = (std::fabs(totalRot) > 1e-5f);
	FMATRIX4 rotWorld = MatIdentity();
	const FMATRIX4 *overrideW = nullptr;
	if (rotated) {
		const FMATRIX4 local = MatMul(MatRotationZ(-totalRot), MatTranslation(x, y, 0.0f));
		rotWorld = MatMul(local, mWorld);
		overrideW = &rotWorld;
	}

	float penX = rotated ? dx : std::round(x + dx);
	const float topY = rotated ? dy : std::round(y + dy);

	if (bkMode == BK_OPAQUE)
		AppendSolidQuad(penX - 1.0f, topY, penX + totalW + 1.0f, topY + totalH, bkColor, overrideW);

	DrawBatch &batch = BeginBatch(false, VK_SKPF_FONT, nullptr, fntSurf, 0, 1.0f, overrideW);
	const uint32_t sw = VK_SKPSW_CENTER | VK_SKPSW_FRAGMENT | VK_SKPSW_FONT;
	const float padX = 1.0f * scale;
	const float padY = 1.0f * scale;

	for (int i = 0; i < len; ++i) {
		const uint8_t ch = static_cast<uint8_t>(cp1252[i]);
		const VkGlyph &g = atlas->Glyph(ch);

		if (g.w > 0.0f && g.h > 0.0f) {
			const float gx0 = penX - padX;
			const float gy0 = topY - padY;
			const float gx1 = gx0 + g.w * scale;
			const float gy1 = gy0 + g.h * scale;

			const uint32_t base = uint32_t(vertices.size());
			vertices.push_back({ gx0, gy0, 0.0f, g.tx0, g.ty0, 0.0f, 0.0f, textColor, sw });
			vertices.push_back({ gx1, gy0, 0.0f, g.tx1, g.ty0, 0.0f, 0.0f, textColor, sw });
			vertices.push_back({ gx1, gy1, 0.0f, g.tx1, g.ty1, 0.0f, 0.0f, textColor, sw });
			vertices.push_back({ gx0, gy1, 0.0f, g.tx0, g.ty1, 0.0f, 0.0f, textColor, sw });

			indices.push_back(base + 0);
			indices.push_back(base + 1);
			indices.push_back(base + 2);
			indices.push_back(base + 0);
			indices.push_back(base + 2);
			indices.push_back(base + 3);
			batch.indexCount += 6;
		}

		penX += float(g.sp) * scale;
	}

	return true;
}

bool VkPad::Text(int x, int y, const char *str, int len)
{
	if (!str || !cfont || !cfont->IsValid())
		return false;

	const std::string cp1252 = VkUtf8ToCp1252(str, len);
	if (cp1252.empty())
		return true;

	return PrintCp1252Run(float(x + origin.x), float(y + origin.y), cp1252.data(), int(cp1252.size()));
}

bool VkPad::TextW(int x, int y, const LPWSTR str, int len)
{
	if (!str || !cfont || !cfont->IsValid())
		return false;

	const int wlen = (len < 0) ? int(std::wcslen(str)) : len;
	if (wlen <= 0)
		return true;

	const int req = WideCharToMultiByte(1252, 0, str, wlen, nullptr, 0, "?", nullptr);
	if (req <= 0)
		return false;

	std::string cp1252(size_t(req), '\0');
	WideCharToMultiByte(1252, 0, str, wlen, cp1252.data(), req, "?", nullptr);
	return PrintCp1252Run(float(x + origin.x), float(y + origin.y), cp1252.data(), int(cp1252.size()));
}

void VkPad::TextEx(float x, float y, const char *str, float scale, float angle)
{
	if (!str || !cfont || !cfont->IsValid())
		return;

	const std::string cp1252 = VkUtf8ToCp1252(str, -1);
	if (cp1252.empty())
		return;

	PrintCp1252Run(x + float(origin.x), y + float(origin.y), cp1252.data(), int(cp1252.size()), scale * 0.01f, angle);
}

// --- Primitives ---

void VkPad::Pixel(int x, int y, DWORD col)
{
	const float fx = float(x + origin.x);
	const float fy = float(y + origin.y);
	AppendSolidQuad(fx, fy, fx + 1.0f, fy + 1.0f, ForceOpaqueIfZeroAlpha(col));
}

void VkPad::MoveTo(int x, int y)
{
	curPos.x = x;
	curPos.y = y;
}

void VkPad::LineTo(int x, int y)
{
	const FVECTOR2 pts[2] = {
		{ float(curPos.x + origin.x), float(curPos.y + origin.y) },
		{ float(x + origin.x),        float(y + origin.y) }
	};
	AppendStrokePolyline(pts, 2, false);
	curPos.x = x;
	curPos.y = y;
}

void VkPad::Line(int x0, int y0, int x1, int y1)
{
	const FVECTOR2 pts[2] = {
		{ float(x0 + origin.x), float(y0 + origin.y) },
		{ float(x1 + origin.x), float(y1 + origin.y) }
	};
	AppendStrokePolyline(pts, 2, false);
}

void VkPad::Rectangle(int x0, int y0, int x1, int y1)
{
	if (x1 == x0 || y1 == y0)
		return;

	const float l = float(std::min(x0, x1) + origin.x);
	const float t = float(std::min(y0, y1) + origin.y);
	const float r = float(std::max(x0, x1) + origin.x);
	const float b = float(std::max(y0, y1) + origin.y);

	if (HasActiveBrush())
		AppendSolidQuad(l, t, r, b, ActiveBrushColor());

	if (HasActivePen()) {
		// Keep 1px outline inside the half-open [l, r) x [t, b) box (matching D3D9/GDI).
		const float sr = std::max(l, r - 1.0f);
		const float sb = std::max(t, b - 1.0f);
		const FVECTOR2 pts[4] = { { l, t }, { sr, t }, { sr, sb }, { l, sb } };
		AppendStrokePolyline(pts, 4, true);
	}
}

void VkPad::Ellipse(int x0, int y0, int x1, int y1)
{
	if (x1 == x0 || y1 == y0)
		return;

	const float l = float(std::min(x0, x1) + origin.x);
	const float t = float(std::min(y0, y1) + origin.y);
	const float r = float(std::max(x0, x1) + origin.x);
	const float b = float(std::max(y0, y1) + origin.y);

	const bool hasPen = HasActivePen();
	const bool hasBrush = HasActiveBrush();
	if (!hasPen && !hasBrush)
		return;

	// Inset outline bounds by 1px on bottom-right so the pen stays inside [l, r) x [t, b).
	const float sr = hasPen ? std::max(l, r - 1.0f) : r;
	const float sb = hasPen ? std::max(t, b - 1.0f) : b;
	const float cx = 0.5f * (l + sr);
	const float cy = 0.5f * (t + sb);
	const float rx = 0.5f * (sr - l);
	const float ry = 0.5f * (sb - t);
	if (rx <= 0.0f && ry <= 0.0f)
		return;

	const float maxR = std::max(rx, ry);
	const int segs = ((std::clamp(int(maxR * 2.5f), 20, 128) + 3) & ~3);

	std::vector<FVECTOR2> ring(static_cast<size_t>(segs));
	for (int i = 0; i < segs; ++i) {
		// Half-step phase places flat axis-aligned edges across all 4 cardinal poles.
		const float a = (2.0f * kPi * (float(i) + 0.5f)) / float(segs);
		ring[size_t(i)] = { cx + std::cos(a) * rx, cy + std::sin(a) * ry };
	}

	if (hasBrush) {
		DrawBatch &batch = BeginBatch(false, 0, nullptr, nullptr, 0, 1.0f);
		const uint32_t clr = ActiveBrushColor();
		const uint32_t sw = VK_SKPSW_CENTER | VK_SKPSW_FRAGMENT;
		const uint32_t base = uint32_t(vertices.size());

		// Align fill vertices to the pen centerline (+0.5px) when stroked so fill never bleeds out.
		const float fShift = hasPen ? 0.5f : 0.0f;
		vertices.push_back({ cx + fShift, cy + fShift, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, clr, sw });
		for (int i = 0; i < segs; ++i)
			vertices.push_back({ ring[size_t(i)].x + fShift, ring[size_t(i)].y + fShift, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, clr, sw });

		for (int i = 0; i < segs; ++i) {
			indices.push_back(base);
			indices.push_back(base + 1 + uint32_t(i));
			indices.push_back(base + 1 + uint32_t((i + 1) % segs));
			batch.indexCount += 3;
		}
	}

	if (hasPen)
		AppendStrokePolyline(ring.data(), segs, true);
}

void VkPad::Polygon(const IVECTOR2 *pt, int npt)
{
	if (!pt || npt < 2)
		return;

	std::vector<FVECTOR2> pts(static_cast<size_t>(npt));
	for (int i = 0; i < npt; ++i)
		pts[size_t(i)] = { float(pt[i].x + origin.x), float(pt[i].y + origin.y) };

	if (HasActiveBrush() && npt >= 3) {
		std::vector<uint32_t> triIdx;
		TriangulatePolygon2D(pts, triIdx);
		if (!triIdx.empty()) {
			DrawBatch &batch = BeginBatch(false, 0, nullptr, nullptr, 0, 1.0f);
			const uint32_t clr = ActiveBrushColor();
			const uint32_t sw = VK_SKPSW_CENTER | VK_SKPSW_FRAGMENT;
			const uint32_t base = uint32_t(vertices.size());

			for (const FVECTOR2 &p : pts)
				vertices.push_back({ p.x, p.y, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, clr, sw });

			for (uint32_t idx : triIdx) {
				indices.push_back(base + idx);
				++batch.indexCount;
			}
		}
	}

	if (HasActivePen())
		AppendStrokePolyline(pts.data(), npt, true);
}

void VkPad::Polyline(const IVECTOR2 *pt, int npt)
{
	if (!pt || npt < 2 || !HasActivePen())
		return;

	std::vector<FVECTOR2> pts(static_cast<size_t>(npt));
	for (int i = 0; i < npt; ++i)
		pts[size_t(i)] = { float(pt[i].x + origin.x), float(pt[i].y + origin.y) };

	AppendStrokePolyline(pts.data(), npt, false);
}

void VkPad::PolyPolygon(const IVECTOR2 *pt, const int *npt, const int nlines)
{
	if (!pt || !npt || nlines <= 0)
		return;

	int offset = 0;
	for (int i = 0; i < nlines; ++i) {
		if (npt[i] > 0) {
			Polygon(pt + offset, npt[i]);
			offset += npt[i];
		}
	}
}

void VkPad::PolyPolyline(const IVECTOR2 *pt, const int *npt, const int nlines)
{
	if (!pt || !npt || nlines <= 0)
		return;

	int offset = 0;
	for (int i = 0; i < nlines; ++i) {
		if (npt[i] > 0) {
			Polyline(pt + offset, npt[i]);
			offset += npt[i];
		}
	}
}

// --- Sketchpad2 / 3 Extensions ---

void VkPad::GetRenderSurfaceSize(LPSIZE size)
{
	if (!size)
		return;

	VkImageView rv = VK_NULL_HANDLE;
	VkFormat rf = VK_FORMAT_UNDEFINED;
	VkExtent2D ext{};
	VkImageView dv = VK_NULL_HANDLE;
	VkFormat df = VK_FORMAT_UNDEFINED;
	if (ResolveTargetInfo(rv, rf, ext, dv, df)) {
		size->cx = LONG(ext.width);
		size->cy = LONG(ext.height);
	} else {
		size->cx = 0;
		size->cy = 0;
	}
}

void VkPad::QuickPen(DWORD color, float width, DWORD style)
{
	if (color == 0 || width <= 0.0f) {
		quickPenActive = false;
		return;
	}
	quickPenActive = true;
	quickPenColor = ResolveCompatColor(color);
	quickPenWidth = width;
	quickPenDashed = (style == 2);
}

void VkPad::QuickBrush(DWORD color)
{
	if (color == 0) {
		quickBrushActive = false;
		return;
	}
	quickBrushActive = true;
	quickBrushColor = ResolveCompatColor(color);
}

void VkPad::SetGlobalLineScale(float width, float pattern)
{
	penWidthScale = std::max(0.01f, width);
	patternScale = std::max(0.01f, pattern);
}

void VkPad::SetWorldTransform(const FMATRIX4 *pWT)
{
	mWorld = pWT ? *pWT : MatIdentity();
	UpdateWVP();
}

void VkPad::SetWorldTransform2D(float scale, float rot, const IVECTOR2 *c, const IVECTOR2 *t)
{
	FMATRIX4 m = MatIdentity();
	if (c)
		m = MatMul(m, MatTranslation(-float(c->x), -float(c->y), 0.0f));
	if (scale != 1.0f)
		m = MatMul(m, MatScaling(scale, scale, 1.0f));
	if (rot != 0.0f)
		m = MatMul(m, MatRotationZ(rot));
	if (c)
		m = MatMul(m, MatTranslation(float(c->x), float(c->y), 0.0f));
	if (t)
		m = MatMul(m, MatTranslation(float(t->x), float(t->y), 0.0f));
	mWorld = m;
	UpdateWVP();
}

void VkPad::SetWorldScaleTransform2D(const FVECTOR2 *scale, const IVECTOR2 *tr)
{
	FMATRIX4 m = MatIdentity();
	if (scale)
		m = MatMul(m, MatScaling(scale->x, scale->y, 1.0f));
	if (tr)
		m = MatMul(m, MatTranslation(float(tr->x), float(tr->y), 0.0f));
	mWorld = m;
	UpdateWVP();
}

void VkPad::SetWorldBillboard(const FVECTOR3 &pos, float scale, bool bFixed, const FVECTOR3 *up)
{
	(void)bFixed;
	(void)up;
	// Invert view upper-left 3x3 rotation so billboard faces camera
	FMATRIX4 m = MatIdentity();
	m.m11 = mView.m11 * scale; m.m12 = mView.m21 * scale; m.m13 = mView.m31 * scale;
	m.m21 = mView.m12 * scale; m.m22 = mView.m22 * scale; m.m23 = mView.m32 * scale;
	m.m31 = mView.m13 * scale; m.m32 = mView.m23 * scale; m.m33 = mView.m33 * scale;
	m.m41 = pos.x;
	m.m42 = pos.y;
	m.m43 = pos.z;
	mWorld = m;
	UpdateWVP();
}

void VkPad::PushWorldTransform()
{
	worldStack.push_back(mWorld);
}

void VkPad::PopWorldTransform()
{
	if (worldStack.empty()) {
		mWorld = MatIdentity();
	} else {
		mWorld = worldStack.back();
		worldStack.pop_back();
	}
	UpdateWVP();
}

void VkPad::DrawPoly(const HPOLY hPoly, DWORD flags)
{
	(void)hPoly;
	(void)flags;
}

void VkPad::Lines(const FVECTOR2 *pt1, int nlines)
{
	if (!pt1 || nlines <= 0 || !HasActivePen())
		return;

	const float width = ActivePenWidth();
	if (width <= 1.05f) {
		AppendHairlineSegments(pt1, nlines * 2, false, true);
	} else {
		for (int i = 0; i < nlines; ++i)
			AppendWidePolyline(&pt1[i * 2], 2, false);
	}
}

void VkPad::CopyRect(const SURFHANDLE pSrc, const LPRECT s, int tx, int ty)
{
	if (!pSrc)
		return;
	auto *src = static_cast<VkSurfNative *>(pSrc);
	if (!client.IsValidSurface(src))
		return;

	const RECT srcR = s ? *s : RECT{ 0, 0, LONG(src->GetWidth()), LONG(src->GetHeight()) };
	const float sw = float(srcR.right - srcR.left);
	const float sh = float(srcR.bottom - srcR.top);
	const float x0 = float(tx + origin.x);
	const float y0 = float(ty + origin.y);
	const float x1 = x0 + sw;
	const float y1 = y0 + sh;

	const FVECTOR2 pos[4] = { { x0, y0 }, { x1, y0 }, { x1, y1 }, { x0, y1 } };
	const FVECTOR2 uv[4] = {
		{ float(srcR.left),  float(srcR.top) },
		{ float(srcR.right), float(srcR.top) },
		{ float(srcR.right), float(srcR.bottom) },
		{ float(srcR.left),  float(srcR.bottom) }
	};
	AppendTexturedQuad(src, pos, uv, src->IsColorKeyEnabled());
}

void VkPad::StretchRect(const SURFHANDLE pSrc, const LPRECT s, const LPRECT t)
{
	if (!pSrc)
		return;
	auto *src = static_cast<VkSurfNative *>(pSrc);
	if (!client.IsValidSurface(src))
		return;

	const RECT srcR = s ? *s : RECT{ 0, 0, LONG(src->GetWidth()), LONG(src->GetHeight()) };

	RECT dstR{};
	if (t) {
		dstR = *t;
	} else {
		SIZE sz{};
		GetRenderSurfaceSize(&sz);
		dstR = { 0, 0, sz.cx, sz.cy };
	}

	const float x0 = float(dstR.left + origin.x);
	const float y0 = float(dstR.top + origin.y);
	const float x1 = float(dstR.right + origin.x);
	const float y1 = float(dstR.bottom + origin.y);

	const FVECTOR2 pos[4] = { { x0, y0 }, { x1, y0 }, { x1, y1 }, { x0, y1 } };
	const FVECTOR2 uv[4] = {
		{ float(srcR.left),  float(srcR.top) },
		{ float(srcR.right), float(srcR.top) },
		{ float(srcR.right), float(srcR.bottom) },
		{ float(srcR.left),  float(srcR.bottom) }
	};
	AppendTexturedQuad(src, pos, uv, src->IsColorKeyEnabled());
}

void VkPad::RotateRect(const SURFHANDLE pSrc, const LPRECT s, int cx, int cy, float angle, float sw, float sh)
{
	if (!pSrc)
		return;
	auto *src = static_cast<VkSurfNative *>(pSrc);
	if (!client.IsValidSurface(src))
		return;

	const RECT srcR = s ? *s : RECT{ 0, 0, LONG(src->GetWidth()), LONG(src->GetHeight()) };
	const float hw = 0.5f * float(srcR.right - srcR.left) * sw;
	const float hh = 0.5f * float(srcR.bottom - srcR.top) * sh;

	const float c = std::cos(angle);
	const float si = std::sin(angle);
	const float ocx = float(cx + origin.x);
	const float ocy = float(cy + origin.y);

	auto rotPt = [&](float lx, float ly) -> FVECTOR2 {
		return { ocx + lx * c - ly * si, ocy + lx * si + ly * c };
	};

	const FVECTOR2 pos[4] = {
		rotPt(-hw, -hh),
		rotPt(+hw, -hh),
		rotPt(+hw, +hh),
		rotPt(-hw, +hh)
	};
	const FVECTOR2 uv[4] = {
		{ float(srcR.left),  float(srcR.top) },
		{ float(srcR.right), float(srcR.top) },
		{ float(srcR.right), float(srcR.bottom) },
		{ float(srcR.left),  float(srcR.bottom) }
	};
	AppendTexturedQuad(src, pos, uv, src->IsColorKeyEnabled());
}

void VkPad::ColorKey(const SURFHANDLE pSrc, const LPRECT s, int tx, int ty)
{
	if (!pSrc)
		return;
	auto *src = static_cast<VkSurfNative *>(pSrc);
	if (!client.IsValidSurface(src))
		return;

	const RECT srcR = s ? *s : RECT{ 0, 0, LONG(src->GetWidth()), LONG(src->GetHeight()) };
	const float sw = float(srcR.right - srcR.left);
	const float sh = float(srcR.bottom - srcR.top);
	const float x0 = float(tx + origin.x);
	const float y0 = float(ty + origin.y);
	const float x1 = x0 + sw;
	const float y1 = y0 + sh;

	const FVECTOR2 pos[4] = { { x0, y0 }, { x1, y0 }, { x1, y1 }, { x0, y1 } };
	const FVECTOR2 uv[4] = {
		{ float(srcR.left),  float(srcR.top) },
		{ float(srcR.right), float(srcR.top) },
		{ float(srcR.right), float(srcR.bottom) },
		{ float(srcR.left),  float(srcR.bottom) }
	};
	AppendTexturedQuad(src, pos, uv, true);
}

void VkPad::ColorFill(DWORD color, const LPRECT t)
{
	RECT dstR{};
	if (t) {
		dstR = *t;
	} else {
		SIZE sz{};
		GetRenderSurfaceSize(&sz);
		dstR = { 0, 0, sz.cx, sz.cy };
	}

	AppendSolidQuad(
		float(dstR.left + origin.x),
		float(dstR.top + origin.y),
		float(dstR.right + origin.x),
		float(dstR.bottom + origin.y),
		ResolveCompatColor(color));
}

void VkPad::CopyTetragon(const SURFHANDLE pSrc, const LPRECT _s, const FVECTOR2 pt[4])
{
	if (!pSrc || !pt)
		return;
	auto *src = static_cast<VkSurfNative *>(pSrc);
	if (!client.IsValidSurface(src))
		return;

	const RECT srcR = _s ? *_s : RECT{ 0, 0, LONG(src->GetWidth()), LONG(src->GetHeight()) };
	const FVECTOR2 uv[4] = {
		{ float(srcR.left),  float(srcR.top) },
		{ float(srcR.right), float(srcR.top) },
		{ float(srcR.right), float(srcR.bottom) },
		{ float(srcR.left),  float(srcR.bottom) }
	};
	AppendTexturedQuad(src, pt, uv, src->IsColorKeyEnabled());
}

void VkPad::FillTetragon(DWORD color, const FVECTOR2 pt[4])
{
	if (!pt)
		return;

	const uint32_t abgr = ResolveCompatColor(color);
	DrawBatch &batch = BeginBatch(false, 0, nullptr, nullptr, 0, 1.0f);
	const uint32_t base = uint32_t(vertices.size());
	const uint32_t sw = VK_SKPSW_CENTER | VK_SKPSW_FRAGMENT;

	for (int i = 0; i < 4; ++i)
		vertices.push_back({ pt[i].x, pt[i].y, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, abgr, sw });

	indices.push_back(base + 0);
	indices.push_back(base + 1);
	indices.push_back(base + 2);
	indices.push_back(base + 0);
	indices.push_back(base + 2);
	indices.push_back(base + 3);
	batch.indexCount += 6;
}

void VkPad::GradientFillRect(const LPRECT rect, DWORD c1, DWORD c2, bool bVertical)
{
	RECT dstR{};
	if (rect) {
		dstR = *rect;
	} else {
		SIZE sz{};
		GetRenderSurfaceSize(&sz);
		dstR = { 0, 0, sz.cx, sz.cy };
	}

	const uint32_t k1 = ResolveCompatColor(c1);
	const uint32_t k2 = ResolveCompatColor(c2);
	const float x0 = float(dstR.left + origin.x);
	const float y0 = float(dstR.top + origin.y);
	const float x1 = float(dstR.right + origin.x);
	const float y1 = float(dstR.bottom + origin.y);

	if (bVertical)
		AppendGradientQuad(x0, y0, x1, y1, k1, k1, k2, k2);
	else
		AppendGradientQuad(x0, y0, x1, y1, k1, k2, k2, k1);
}

void VkPad::StretchRegion(const skpRegion *rgn, const SURFHANDLE hSrc, const LPRECT out)
{
	if (!rgn || !hSrc || !out)
		return;
	auto *src = static_cast<VkSurfNative *>(hSrc);
	if (!client.IsValidSurface(src))
		return;

	const LONG ml = std::max(0L, rgn->intr.left - rgn->outr.left);
	const LONG mr = std::max(0L, rgn->outr.right - rgn->intr.right);
	const LONG mt = std::max(0L, rgn->intr.top - rgn->outr.top);
	const LONG mb = std::max(0L, rgn->outr.bottom - rgn->intr.bottom);

	const LONG dx[4] = {
		out->left,
		std::min(out->right, out->left + ml),
		std::max(out->left, out->right - mr),
		out->right
	};
	const LONG dy[4] = {
		out->top,
		std::min(out->bottom, out->top + mt),
		std::max(out->top, out->bottom - mb),
		out->bottom
	};
	const LONG sx[4] = { rgn->outr.left, rgn->intr.left, rgn->intr.right, rgn->outr.right };
	const LONG sy[4] = { rgn->outr.top,  rgn->intr.top,  rgn->intr.bottom, rgn->outr.bottom };

	for (int row = 0; row < 3; ++row) {
		if (dy[row + 1] <= dy[row] || sy[row + 1] <= sy[row])
			continue;
		for (int col = 0; col < 3; ++col) {
			if (dx[col + 1] <= dx[col] || sx[col + 1] <= sx[col])
				continue;

			const FVECTOR2 pos[4] = {
				{ float(dx[col]),     float(dy[row]) },
				{ float(dx[col + 1]), float(dy[row]) },
				{ float(dx[col + 1]), float(dy[row + 1]) },
				{ float(dx[col]),     float(dy[row + 1]) }
			};
			const FVECTOR2 uv[4] = {
				{ float(sx[col]),     float(sy[row]) },
				{ float(sx[col + 1]), float(sy[row]) },
				{ float(sx[col + 1]), float(sy[row + 1]) },
				{ float(sx[col]),     float(sy[row + 1]) }
			};
			AppendTexturedQuad(src, pos, uv, src->IsColorKeyEnabled());
		}
	}
}

void VkPad::ClipRect(const LPRECT clip)
{
	if (!clip) {
		clipEnabled = false;
		clipRect = {};
	} else {
		clipEnabled = true;
		clipRect = *clip;
	}
}

void VkPad::Clipper(int idx, const VECTOR3 *pPos, double cos_angle, double dir)
{
	(void)idx;
	(void)pPos;
	(void)cos_angle;
	(void)dir;
}

void VkPad::DepthEnable(bool bEnable)
{
	depthEnable = bEnable;
}

void VkPad::SetViewMode(SkpView mode)
{
	viewMode = mode;
	UpdateWVP();
}

const FMATRIX4 *VkPad::ViewMatrix() const
{
	return &mView;
}

const FMATRIX4 *VkPad::ProjectionMatrix() const
{
	return &mProj;
}

const FMATRIX4 *VkPad::GetViewProjectionMatrix() const
{
	mVP = MatMul(mView, mProj);
	return &mVP;
}

void VkPad::SetViewMatrix(const FMATRIX4 *pV)
{
	mView = pV ? *pV : MatIdentity();
	UpdateWVP();
}

void VkPad::SetProjectionMatrix(const FMATRIX4 *pP)
{
	if (pP)
		mProj = *pP;
	else
		viewMode = ORTHO;
	UpdateWVP();
}

void VkPad::SetClipDistance(float zmin, float zmax)
{
	clipNear = zmin;
	clipFar = zmax;
	if (viewMode == ORTHO)
		UpdateWVP();
}

const FMATRIX4 *VkPad::GetColorMatrix()
{
	return &mColorMatrix;
}

void VkPad::SetColorMatrix(const FMATRIX4 *pMatrix)
{
	mColorMatrix = pMatrix ? *pMatrix : MatIdentity();
	RefreshEffectsFlag();
}

void VkPad::SetBrightness(const FVECTOR4 *pBrightness)
{
	mColorMatrix = MatIdentity();
	if (pBrightness) {
		mColorMatrix.m11 = pBrightness->r;
		mColorMatrix.m22 = pBrightness->g;
		mColorMatrix.m33 = pBrightness->b;
		mColorMatrix.m44 = pBrightness->a;
	}
	RefreshEffectsFlag();
}

FVECTOR4 VkPad::GetRenderParam(RenderParam param)
{
	switch (param) {
	case RenderParam::PRM_GAMMA: return gammaVal;
	case RenderParam::PRM_NOISE: return noiseColor;
	default:                     return FVECTOR4(0, 0, 0, 0);
	}
}

void VkPad::SetRenderParam(RenderParam param, const FVECTOR4 *data)
{
	switch (param) {
	case RenderParam::PRM_GAMMA:
		gammaVal = data ? *data : FVECTOR4(1.0f, 1.0f, 1.0f, 1.0f);
		break;
	case RenderParam::PRM_NOISE:
		noiseColor = data ? *data : FVECTOR4(0.0f, 0.0f, 0.0f, 1.0f);
		break;
	default:
		return;
	}
	RefreshEffectsFlag();
}

void VkPad::SetBlendState(BlendState dwState)
{
	const uint32_t bits = uint32_t(dwState);
	if (bits & uint32_t(BlendState::FILTER_ANISOTROPIC))
		texFilter = VkPadPipeline::FilterMode::Aniso;
	else if (bits & uint32_t(BlendState::FILTER_LINEAR))
		texFilter = VkPadPipeline::FilterMode::Linear;
	else if (bits & uint32_t(BlendState::FILTER_POINT))
		texFilter = VkPadPipeline::FilterMode::Point;

	if ((bits & uint32_t(BlendState::COPY_ALPHA)) == uint32_t(BlendState::COPY_ALPHA))
		blendOp = VkPadPipeline::ColorBlendOp::CopyAlpha;
	else if ((bits & uint32_t(BlendState::COPY_COLOR)) == uint32_t(BlendState::COPY_COLOR))
		blendOp = VkPadPipeline::ColorBlendOp::CopyColor;
	else if ((bits & uint32_t(BlendState::COPY)) == uint32_t(BlendState::COPY))
		blendOp = VkPadPipeline::ColorBlendOp::Copy;
	else
		blendOp = VkPadPipeline::ColorBlendOp::AlphaBlend;
}

void VkPad::ColorCompatibility(bool bEnable)
{
	colorCompat = bEnable;
}

void VkPad::Clear(DWORD color, bool bColor, bool bDepth)
{
	if (!bColor && !bDepth)
		return;

	if (!batches.empty())
		Flush();

	if (bColor) {
		hasPendingClearColor = true;
		pendingClearAbgr = color;
	}
	if (bDepth)
		hasPendingClearDepth = true;
}

bool VkPad::Flush(SURFHANDLE hTarget)
{
	if (hTarget && hTarget != targetSurf) {
		Flush(nullptr);
		targetSurf = hTarget;
		nativeTarget = static_cast<VkSurfNative *>(hTarget);
		UpdateWVP();
	}

	if (batches.empty() && !hasPendingClearColor)
		return true;

	VkSwapchain *swap = pipeline.Swapchain();
	if (!swap)
		return false;

	VkImageView colorView = VK_NULL_HANDLE;
	VkFormat colorFmt = VK_FORMAT_UNDEFINED;
	VkExtent2D extent{};
	VkImageView depthView = VK_NULL_HANDLE;
	VkFormat depthFmt = VK_FORMAT_UNDEFINED;
	if (!ResolveTargetInfo(colorView, colorFmt, extent, depthView, depthFmt)) {
		vertices.clear();
		indices.clear();
		batches.clear();
		hasPendingClearColor = false;
		hasPendingClearDepth = false;
		return false;
	}

	VkCommandBuffer cmd = swap->ActiveCommandBuffer();
	if (cmd == VK_NULL_HANDLE)
		return false;

	const uint32_t slot = swap->ActiveSlotIndex();
	const uint64_t slotGen = swap->SlotGeneration(slot);
	pipeline.BeginSlotFrame(slot, slotGen);

	for (DrawBatch &b : batches) {
		if (b.tex) {
			if (b.tex == nativeTarget)
				b.tex = nullptr;
			else
				b.tex->TransitionToShaderRead(cmd);
		}
		if (b.fntTex)
			b.fntTex->TransitionToShaderRead(cmd);
	}

	if (!targetSurf) {
		swap->TransitionSceneLayout(
			cmd,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
			VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
			VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
	} else if (nativeTarget) {
		nativeTarget->TransitionToColorAttachment(cmd);
		nativeTarget->MarkGpuDirty();
	}

	VkPadPipeline::BufferSlice vtxSlice{};
	VkPadPipeline::BufferSlice idxSlice{};
	if (!vertices.empty() && !indices.empty()) {
		const VkDeviceSize vtxBytes = vertices.size() * sizeof(VkSkpVtx);
		const VkDeviceSize idxBytes = indices.size() * sizeof(uint32_t);
		vtxSlice = pipeline.AllocVertices(slot, vtxBytes);
		idxSlice = pipeline.AllocIndices(slot, idxBytes);
		if (!vtxSlice.mapped || !idxSlice.mapped) {
			vertices.clear();
			indices.clear();
			batches.clear();
			hasPendingClearColor = false;
			hasPendingClearDepth = false;
			return false;
		}
		std::memcpy(vtxSlice.mapped, vertices.data(), size_t(vtxBytes));
		std::memcpy(idxSlice.mapped, indices.data(), size_t(idxBytes));
	}

	std::vector<VkDescriptorSet> batchSets(batches.size(), VK_NULL_HANDLE);
	for (size_t i = 0; i < batches.size(); ++i) {
		const DrawBatch &b = batches[i];
		if (b.indexCount == 0)
			continue;

		VkPadPipeline::BufferSlice uboSlice = pipeline.AllocUniform(slot, sizeof(VkSkpUniforms));
		VkDescriptorSet dset = pipeline.AllocateDescriptorSet(slot);
		if (!uboSlice.mapped || dset == VK_NULL_HANDLE)
			return false;

		VkSkpUniforms u = b.params;
		u.gMisc[0] = float(((noiseCounter++) * 37u) % 256u) / 256.0f;
		std::memcpy(uboSlice.mapped, &u, sizeof(VkSkpUniforms));

		const VkImageView tv = b.tex ? b.tex->GetImageView() : pipeline.WhiteView();
		const VkImageView fv = b.fntTex ? b.fntTex->GetImageView() : pipeline.WhiteView();
		pipeline.UpdateBatchDescriptorSet(dset, uboSlice, tv, pipeline.SamplerForFilter(b.filter), fv);
		batchSets[i] = dset;
	}

	VkRenderingAttachmentInfo colorAtt{};
	colorAtt.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	colorAtt.imageView   = colorView;
	colorAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	colorAtt.loadOp      = hasPendingClearColor ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
	colorAtt.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
	UnpackAbgrToFloat4(pendingClearAbgr, colorAtt.clearValue.color.float32);

	VkRenderingInfo renderInfo{};
	renderInfo.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO;
	renderInfo.renderArea           = { { 0, 0 }, extent };
	renderInfo.layerCount           = 1;
	renderInfo.colorAttachmentCount = 1;
	renderInfo.pColorAttachments    = &colorAtt;

	vkCmdBeginRendering(cmd, &renderInfo);

	// Negative viewport height keeps D3D9 top-down ortho matrices and 2D/3D transforms consistent
	VkViewport vp{};
	vp.x        = 0.0f;
	vp.y        = float(extent.height);
	vp.width    = float(extent.width);
	vp.height   = -float(extent.height);
	vp.minDepth = 0.0f;
	vp.maxDepth = 1.0f;
	vkCmdSetViewport(cmd, 0, 1, &vp);

	if (vtxSlice.buffer != VK_NULL_HANDLE && idxSlice.buffer != VK_NULL_HANDLE) {
		vkCmdBindVertexBuffers(cmd, 0, 1, &vtxSlice.buffer, &vtxSlice.offset);
		vkCmdBindIndexBuffer(cmd, idxSlice.buffer, idxSlice.offset, VK_INDEX_TYPE_UINT32);
	}

	VkPipeline boundPipe = VK_NULL_HANDLE;
	for (size_t i = 0; i < batches.size(); ++i) {
		const DrawBatch &b = batches[i];
		if (b.indexCount == 0 || batchSets[i] == VK_NULL_HANDLE)
			continue;
		if (b.scissor.extent.width == 0 || b.scissor.extent.height == 0)
			continue;

		const VkPrimitiveTopology topo = b.isLineList ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		const VkPipeline pipe = pipeline.GetOrCreatePipeline(colorFmt, VK_FORMAT_UNDEFINED, b.blend, topo, false);
		if (pipe == VK_NULL_HANDLE)
			continue;

		if (pipe != boundPipe) {
			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
			boundPipe = pipe;
		}

		vkCmdSetScissor(cmd, 0, 1, &b.scissor);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.PipelineLayout(), 0, 1, &batchSets[i], 0, nullptr);
		vkCmdDrawIndexed(cmd, b.indexCount, 1, b.firstIndex, 0, 0);
	}

	vkCmdEndRendering(cmd);

	didSubmitOnTarget = true;
	vertices.clear();
	indices.clear();
	batches.clear();
	hasPendingClearColor = false;
	hasPendingClearDepth = false;

	if (!swap->IsFrameRecording()) {
		if (nativeTarget)
			nativeTarget->TransitionToShaderRead(cmd);
		swap->FlushOffscreenCommands();
	}

	return true;
}

int VkPad::DrawMeshGroup(const MESHHANDLE hMesh, DWORD grp, MeshFlags flags, const SURFHANDLE hTex)
{
	(void)hMesh;
	(void)grp;
	(void)flags;
	(void)hTex;
	return 0; // Phase 2 3D mesh integration
}
