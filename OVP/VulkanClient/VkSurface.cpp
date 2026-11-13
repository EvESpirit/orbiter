// ==============================================================
// VkSurface.cpp
// Native Vulkan surface wrapper (VkSurfNative) and DDS/WIC texture loader
// Part of the ORBITER VISUALISATION PROJECT (OVP) - VulkanClient
// Licensed under the MIT License
// ==============================================================

#define STRICT 1
#include "VkSurface.h"
#include "VkContext.h"
#include "VkSwapchain.h"
#include "VkConfig.h"
#include "VkLog.h"
#include "VulkanClient.h"
#include "imgui.h"
#include "imgui_impl_vulkan.h"

#include <wincodec.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {

#pragma pack(push, 1)
struct DdsPixelFormat {
	uint32_t size;
	uint32_t flags;
	uint32_t fourCC;
	uint32_t rgbBitCount;
	uint32_t rBitMask;
	uint32_t gBitMask;
	uint32_t bBitMask;
	uint32_t aBitMask;
};

struct DdsHeader {
	uint32_t size;
	uint32_t flags;
	uint32_t height;
	uint32_t width;
	uint32_t pitchOrLinearSize;
	uint32_t depth;
	uint32_t mipMapCount;
	uint32_t reserved1[11];
	DdsPixelFormat ddspf;
	uint32_t caps;
	uint32_t caps2;
	uint32_t caps3;
	uint32_t caps4;
	uint32_t reserved2;
};
#pragma pack(pop)

constexpr uint32_t kDdsMagic      = 0x20534444u; // "DDS "
constexpr uint32_t kDdpfAlpha     = 0x00000001u;
constexpr uint32_t kDdpfAlphaOnly = 0x00000002u;
constexpr uint32_t kDdpfFourCC    = 0x00000004u;
constexpr uint32_t kDdpfRgb       = 0x00000040u;
constexpr uint32_t kDdpfLuminance = 0x00020000u;

constexpr uint32_t MakeFourCC(char a, char b, char c, char d)
{
	return (uint32_t)(uint8_t)a |
		((uint32_t)(uint8_t)b << 8) |
		((uint32_t)(uint8_t)c << 16) |
		((uint32_t)(uint8_t)d << 24);
}

constexpr uint32_t kFourCC_DXT1 = MakeFourCC('D', 'X', 'T', '1');
constexpr uint32_t kFourCC_DXT2 = MakeFourCC('D', 'X', 'T', '2');
constexpr uint32_t kFourCC_DXT3 = MakeFourCC('D', 'X', 'T', '3');
constexpr uint32_t kFourCC_DXT4 = MakeFourCC('D', 'X', 'T', '4');
constexpr uint32_t kFourCC_DXT5 = MakeFourCC('D', 'X', 'T', '5');

void DecodeRgb565(uint16_t c, uint8_t &r, uint8_t &g, uint8_t &b)
{
	r = (uint8_t)(((c >> 11) & 0x1Fu) * 255u / 31u);
	g = (uint8_t)(((c >> 5) & 0x3Fu) * 255u / 63u);
	b = (uint8_t)((c & 0x1Fu) * 255u / 31u);
}

void DecodeBc1ColorBlock(
	const uint8_t *block,
	bool forceFourColor,
	uint8_t outColors[4][4],
	bool &outHas1BitTransparency)
{
	const uint16_t c0 = (uint16_t)(block[0] | (block[1] << 8));
	const uint16_t c1 = (uint16_t)(block[2] | (block[3] << 8));

	DecodeRgb565(c0, outColors[0][2], outColors[0][1], outColors[0][0]);
	outColors[0][3] = 255;
	DecodeRgb565(c1, outColors[1][2], outColors[1][1], outColors[1][0]);
	outColors[1][3] = 255;

	if (forceFourColor || c0 > c1) {
		for (int ch = 0; ch < 3; ++ch) {
			outColors[2][ch] = (uint8_t)((2u * outColors[0][ch] + outColors[1][ch]) / 3u);
			outColors[3][ch] = (uint8_t)((outColors[0][ch] + 2u * outColors[1][ch]) / 3u);
		}
		outColors[2][3] = 255;
		outColors[3][3] = 255;
	} else {
		for (int ch = 0; ch < 3; ++ch)
			outColors[2][ch] = (uint8_t)((outColors[0][ch] + outColors[1][ch]) / 2u);
		outColors[2][3] = 255;
		outColors[3][0] = 0;
		outColors[3][1] = 0;
		outColors[3][2] = 0;
		outColors[3][3] = 0;
		outHas1BitTransparency = true;
	}
}

void DecompressBcImageToBgra(
	const uint8_t *blocks,
	uint32_t width,
	uint32_t height,
	VkFormat bcFormat,
	std::vector<uint8_t> &outBgra,
	bool &outHasAlpha)
{
	outBgra.assign((size_t)width * height * 4u, 0);
	outHasAlpha = (bcFormat == VK_FORMAT_BC2_UNORM_BLOCK || bcFormat == VK_FORMAT_BC3_UNORM_BLOCK);

	const uint32_t blocksX = std::max(1u, (width + 3u) / 4u);
	const uint32_t blocksY = std::max(1u, (height + 3u) / 4u);
	const size_t blockSize = (bcFormat == VK_FORMAT_BC1_RGBA_UNORM_BLOCK || bcFormat == VK_FORMAT_BC1_RGB_UNORM_BLOCK) ? 8u : 16u;
	const bool forceFourColor = (blockSize == 16u);

	for (uint32_t by = 0; by < blocksY; ++by) {
		for (uint32_t bx = 0; bx < blocksX; ++bx) {
			const uint8_t *blk = blocks + ((size_t)by * blocksX + bx) * blockSize;
			const uint8_t *colorBlk = (blockSize == 16u) ? (blk + 8) : blk;

			uint8_t colors[4][4];
			bool has1BitAlpha = false;
			DecodeBc1ColorBlock(colorBlk, forceFourColor, colors, has1BitAlpha);

			uint8_t alphaLut[8]{};
			uint64_t bc3Indices = 0;
			if (bcFormat == VK_FORMAT_BC3_UNORM_BLOCK) {
				const uint8_t a0 = blk[0];
				const uint8_t a1 = blk[1];
				alphaLut[0] = a0;
				alphaLut[1] = a1;
				if (a0 > a1) {
					for (uint32_t i = 1; i <= 6; ++i)
						alphaLut[i + 1] = (uint8_t)(((7u - i) * a0 + i * a1) / 7u);
				} else {
					for (uint32_t i = 1; i <= 4; ++i)
						alphaLut[i + 1] = (uint8_t)(((5u - i) * a0 + i * a1) / 5u);
					alphaLut[6] = 0;
					alphaLut[7] = 255;
				}
				for (int i = 0; i < 6; ++i)
					bc3Indices |= (uint64_t)blk[2 + i] << (8 * i);
			}

			const uint32_t colorBits = (uint32_t)colorBlk[4] |
				((uint32_t)colorBlk[5] << 8) |
				((uint32_t)colorBlk[6] << 16) |
				((uint32_t)colorBlk[7] << 24);

			for (uint32_t py = 0; py < 4; ++py) {
				const uint32_t dstY = by * 4u + py;
				if (dstY >= height)
					continue;
				for (uint32_t px = 0; px < 4; ++px) {
					const uint32_t dstX = bx * 4u + px;
					if (dstX >= width)
						continue;

					const uint32_t pixIdx = py * 4u + px;
					const uint32_t cIdx = (colorBits >> (2u * pixIdx)) & 0x3u;
					uint8_t *dst = outBgra.data() + ((size_t)dstY * width + dstX) * 4u;
					dst[0] = colors[cIdx][0];
					dst[1] = colors[cIdx][1];
					dst[2] = colors[cIdx][2];

					if (bcFormat == VK_FORMAT_BC2_UNORM_BLOCK) {
						const uint8_t aByte = blk[pixIdx >> 1];
						const uint8_t a4 = (pixIdx & 1u) ? (aByte >> 4) : (aByte & 0xFu);
						dst[3] = (uint8_t)(a4 * 17u);
					} else if (bcFormat == VK_FORMAT_BC3_UNORM_BLOCK) {
						const uint32_t aIdx = (uint32_t)((bc3Indices >> (3u * pixIdx)) & 0x7u);
						dst[3] = alphaLut[aIdx];
					} else {
						dst[3] = colors[cIdx][3];
						if (dst[3] < 255)
							outHasAlpha = true;
					}
				}
			}
		}
	}
}

bool ReadAllFileBytes(const char *path, std::vector<uint8_t> &outBytes)
{
	FILE *fp = std::fopen(path, "rb");
	if (!fp)
		return false;
	std::fseek(fp, 0, SEEK_END);
	const long len = std::ftell(fp);
	std::fseek(fp, 0, SEEK_SET);
	if (len <= 0) {
		std::fclose(fp);
		return false;
	}
	outBytes.resize((size_t)len);
	const size_t read = std::fread(outBytes.data(), 1, (size_t)len, fp);
	std::fclose(fp);
	return read == (size_t)len;
}

bool DecodeImageViaWIC(
	const char *path,
	std::vector<uint8_t> &outBgra,
	uint32_t &outWidth,
	uint32_t &outHeight,
	bool &outHasAlpha)
{
	wchar_t wpath[MAX_PATH]{};
	if (MultiByteToWideChar(CP_ACP, 0, path, -1, wpath, MAX_PATH) <= 0)
		return false;

	IWICImagingFactory *factory = nullptr;
	HRESULT hr = CoCreateInstance(
		CLSID_WICImagingFactory,
		nullptr,
		CLSCTX_INPROC_SERVER,
		IID_PPV_ARGS(&factory));
	if (FAILED(hr) || !factory)
		return false;

	IWICBitmapDecoder *decoder = nullptr;
	hr = factory->CreateDecoderFromFilename(
		wpath,
		nullptr,
		GENERIC_READ,
		WICDecodeMetadataCacheOnDemand,
		&decoder);
	if (FAILED(hr) || !decoder) {
		factory->Release();
		return false;
	}

	IWICBitmapFrameDecode *frame = nullptr;
	hr = decoder->GetFrame(0, &frame);
	if (FAILED(hr) || !frame) {
		decoder->Release();
		factory->Release();
		return false;
	}

	UINT w = 0, h = 0;
	frame->GetSize(&w, &h);
	if (w == 0 || h == 0) {
		frame->Release();
		decoder->Release();
		factory->Release();
		return false;
	}

	IWICFormatConverter *converter = nullptr;
	hr = factory->CreateFormatConverter(&converter);
	if (FAILED(hr) || !converter) {
		frame->Release();
		decoder->Release();
		factory->Release();
		return false;
	}

	hr = converter->Initialize(
		frame,
		GUID_WICPixelFormat32bppBGRA,
		WICBitmapDitherTypeNone,
		nullptr,
		0.0f,
		WICBitmapPaletteTypeCustom);
	if (FAILED(hr)) {
		converter->Release();
		frame->Release();
		decoder->Release();
		factory->Release();
		return false;
	}

	const UINT stride = w * 4u;
	const UINT bufSize = stride * h;
	outBgra.resize(bufSize);
	hr = converter->CopyPixels(nullptr, stride, bufSize, outBgra.data());

	converter->Release();
	frame->Release();
	decoder->Release();
	factory->Release();

	if (FAILED(hr))
		return false;

	outWidth = w;
	outHeight = h;
	outHasAlpha = false;
	for (size_t i = 3; i < outBgra.size(); i += 4) {
		if (outBgra[i] < 255) {
			outHasAlpha = true;
			break;
		}
	}
	return true;
}

bool BuildAuxMapFilename(char *out, size_t maxLen, const char *fname, const char *suffix)
{
	if (!fname || !suffix)
		return false;
	char buf[MAX_PATH]{};
	strncpy_s(buf, sizeof(buf), fname, _TRUNCATE);
	char *dot = std::strrchr(buf, '.');
	if (!dot)
		return false;
	*dot = '\0';
	std::snprintf(out, maxLen, "%s_%s.%s", buf, suffix, dot + 1);
	return true;
}

VkFormat SelectSupportedDepthFormat(VkPhysicalDevice physDev)
{
	const VkFormat candidates[] = {
		VK_FORMAT_D24_UNORM_S8_UINT,
		VK_FORMAT_D32_SFLOAT_S8_UINT,
		VK_FORMAT_D32_SFLOAT
	};
	for (VkFormat fmt : candidates) {
		VkFormatProperties props{};
		vkGetPhysicalDeviceFormatProperties(physDev, fmt, &props);
		if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
			return fmt;
	}
	return VK_FORMAT_D32_SFLOAT;
}

} // namespace

// ==============================================================
// VkSurfNative Implementation
// ==============================================================

VkSurfNative::VkSurfNative(VkContext &context, VkSwapchain &swap)
	: ctx(&context)
	, swapchain(&swap)
{
	std::strcpy(name, "unnamed");
}

VkSurfNative::~VkSurfNative()
{
	for (uint32_t i = 0; i < MAP_MAX_COUNT; ++i) {
		delete pMap[i];
		pMap[i] = nullptr;
	}
	DestroyGpuResources();
}

void VkSurfNative::DestroyGdiResources()
{
	if (gdiMemDC) {
		if (gdiOldObj) {
			SelectObject(gdiMemDC, gdiOldObj);
			gdiOldObj = nullptr;
		}
		DeleteDC(gdiMemDC);
		gdiMemDC = nullptr;
	}
	if (gdiBitmap) {
		DeleteObject(gdiBitmap);
		gdiBitmap = nullptr;
	}
	gdiBits = nullptr;
	gdiOpen = false;
	gdiPreSnapshot.clear();
}

void VkSurfNative::DestroyGpuResources()
{
	DestroyGdiResources();
	ReleaseImGuiDescriptorSet();

	if (!ctx || !ctx->Device())
		return;

	if (tempBlitView != VK_NULL_HANDLE) {
		vkDestroyImageView(ctx->Device(), tempBlitView, nullptr);
		tempBlitView = VK_NULL_HANDLE;
	}
	if (tempBlitImage != VK_NULL_HANDLE) {
		vmaDestroyImage(ctx->Allocator(), tempBlitImage, tempBlitAlloc);
		tempBlitImage = VK_NULL_HANDLE;
		tempBlitAlloc = VK_NULL_HANDLE;
	}
	if (depthView != VK_NULL_HANDLE) {
		vkDestroyImageView(ctx->Device(), depthView, nullptr);
		depthView = VK_NULL_HANDLE;
	}
	if (depthImage != VK_NULL_HANDLE) {
		vmaDestroyImage(ctx->Allocator(), depthImage, depthAlloc);
		depthImage = VK_NULL_HANDLE;
		depthAlloc = VK_NULL_HANDLE;
	}
	if (rtMip0View != VK_NULL_HANDLE) {
		vkDestroyImageView(ctx->Device(), rtMip0View, nullptr);
		rtMip0View = VK_NULL_HANDLE;
	}
	if (imageView != VK_NULL_HANDLE) {
		vkDestroyImageView(ctx->Device(), imageView, nullptr);
		imageView = VK_NULL_HANDLE;
	}
	if (image != VK_NULL_HANDLE) {
		vmaDestroyImage(ctx->Allocator(), image, allocation);
		image = VK_NULL_HANDLE;
		allocation = VK_NULL_HANDLE;
	}
	currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
}

void VkSurfNative::ReleaseImGuiDescriptorSet()
{
	if (imguiDescSet != VK_NULL_HANDLE) {
		if (ImGui::GetCurrentContext() && ImGui::GetIO().BackendRendererUserData != nullptr)
			ImGui_ImplVulkan_RemoveTexture(imguiDescSet);
		imguiDescSet = VK_NULL_HANDLE;
	}
}

void VkSurfNative::SetName(const char *surfName)
{
	if (!surfName || !surfName[0]) {
		std::strcpy(name, "unnamed");
		return;
	}
	strncpy_s(name, sizeof(name), surfName, _TRUNCATE);
	for (char *p = name; *p; ++p) {
		if (*p == '/')
			*p = '\\';
	}
}

bool VkSurfNative::IsCompressed() const
{
	return format == VK_FORMAT_BC1_RGB_UNORM_BLOCK ||
		format == VK_FORMAT_BC1_RGBA_UNORM_BLOCK ||
		format == VK_FORMAT_BC2_UNORM_BLOCK ||
		format == VK_FORMAT_BC3_UNORM_BLOCK;
}

void VkSurfNative::SetMap(uint32_t slot, VkSurfNative *mapSurf)
{
	if (slot >= MAP_MAX_COUNT)
		return;
	delete pMap[slot];
	pMap[slot] = mapSurf;
	if (mapSurf)
		flags |= OAPISURFACE_MAPS;
}

bool VkSurfNative::AllocateGpuImage(
	uint32_t w,
	uint32_t h,
	uint32_t mips,
	VkFormat fmt,
	VkImageUsageFlags usage,
	bool alpha,
	bool createDepth)
{
	DestroyGpuResources();

	width      = std::max(1u, w);
	height     = std::max(1u, h);
	mipLevels  = std::max(1u, mips);
	format     = fmt;
	imageUsage = usage;
	hasAlpha   = alpha;

	VkImageCreateInfo imgInfo{};
	imgInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imgInfo.imageType     = VK_IMAGE_TYPE_2D;
	imgInfo.format        = format;
	imgInfo.extent.width  = width;
	imgInfo.extent.height = height;
	imgInfo.extent.depth  = 1;
	imgInfo.mipLevels     = mipLevels;
	imgInfo.arrayLayers   = 1;
	imgInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
	imgInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
	imgInfo.usage         = imageUsage;
	imgInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
	imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	VmaAllocationCreateInfo allocInfo{};
	allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

	VkResult vr = vmaCreateImage(ctx->Allocator(), &imgInfo, &allocInfo, &image, &allocation, nullptr);
	if (vr != VK_SUCCESS) {
		VkLogErr("vmaCreateImage (%ux%u, fmt=%d) failed (VkResult = %d)", width, height, (int)format, (int)vr);
		return false;
	}

	VkImageViewCreateInfo viewInfo{};
	viewInfo.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image    = image;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format   = format;
	viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
	viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
	viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
	viewInfo.components.a = hasAlpha ? VK_COMPONENT_SWIZZLE_IDENTITY : VK_COMPONENT_SWIZZLE_ONE;
	viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
	viewInfo.subresourceRange.baseMipLevel   = 0;
	viewInfo.subresourceRange.levelCount     = mipLevels;
	viewInfo.subresourceRange.baseArrayLayer = 0;
	viewInfo.subresourceRange.layerCount     = 1;

	vr = vkCreateImageView(ctx->Device(), &viewInfo, nullptr, &imageView);
	if (vr != VK_SUCCESS) {
		VkLogErr("vkCreateImageView failed (VkResult = %d)", (int)vr);
		DestroyGpuResources();
		return false;
	}

	if (createDepth) {
		depthFormat = SelectSupportedDepthFormat(ctx->PhysicalDevice());

		VkImageCreateInfo depInfo{};
		depInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		depInfo.imageType     = VK_IMAGE_TYPE_2D;
		depInfo.format        = depthFormat;
		depInfo.extent.width  = width;
		depInfo.extent.height = height;
		depInfo.extent.depth  = 1;
		depInfo.mipLevels     = 1;
		depInfo.arrayLayers   = 1;
		depInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
		depInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
		depInfo.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		depInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
		depInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		vr = vmaCreateImage(ctx->Allocator(), &depInfo, &allocInfo, &depthImage, &depthAlloc, nullptr);
		if (vr != VK_SUCCESS) {
			VkLogErr("vmaCreateImage (depth %ux%u) failed (VkResult = %d)", width, height, (int)vr);
			DestroyGpuResources();
			return false;
		}

		VkImageViewCreateInfo depView{};
		depView.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		depView.image    = depthImage;
		depView.viewType = VK_IMAGE_VIEW_TYPE_2D;
		depView.format   = depthFormat;
		depView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		if (depthFormat == VK_FORMAT_D24_UNORM_S8_UINT || depthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT)
			depView.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
		depView.subresourceRange.baseMipLevel   = 0;
		depView.subresourceRange.levelCount     = 1;
		depView.subresourceRange.baseArrayLayer = 0;
		depView.subresourceRange.layerCount     = 1;

		vr = vkCreateImageView(ctx->Device(), &depView, nullptr, &depthView);
		if (vr != VK_SUCCESS) {
			VkLogErr("vkCreateImageView (depth) failed (VkResult = %d)", (int)vr);
			DestroyGpuResources();
			return false;
		}
	}

	return true;
}

bool VkSurfNative::UploadMipChain(
	const uint8_t *data,
	size_t dataSize,
	const std::vector<size_t> &mipOffsets,
	const std::vector<size_t> &mipSizes)
{
	if (!ctx || !swapchain || !data || dataSize == 0 || mipOffsets.empty())
		return false;

	VkBufferCreateInfo bufInfo{};
	bufInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufInfo.size        = dataSize;
	bufInfo.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VmaAllocationCreateInfo stagingInfo{};
	stagingInfo.usage = VMA_MEMORY_USAGE_AUTO;
	stagingInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

	VkBuffer stagingBuf = VK_NULL_HANDLE;
	VmaAllocation stagingAlloc = VK_NULL_HANDLE;
	VmaAllocationInfo mapInfo{};
	VkResult vr = vmaCreateBuffer(ctx->Allocator(), &bufInfo, &stagingInfo, &stagingBuf, &stagingAlloc, &mapInfo);
	if (vr != VK_SUCCESS || !mapInfo.pMappedData) {
		VkLogErr("vmaCreateBuffer (surface upload staging) failed (VkResult = %d)", (int)vr);
		if (stagingBuf != VK_NULL_HANDLE)
			vmaDestroyBuffer(ctx->Allocator(), stagingBuf, stagingAlloc);
		return false;
	}

	std::memcpy(mapInfo.pMappedData, data, dataSize);
	vmaFlushAllocation(ctx->Allocator(), stagingAlloc, 0, dataSize);

	const bool recordingFrame = swapchain->IsFrameRecording();
	VkCommandBuffer cmd = recordingFrame ? swapchain->CurrentCommandBuffer() : swapchain->OffscreenCommandBuffer();
	if (cmd == VK_NULL_HANDLE) {
		vmaDestroyBuffer(ctx->Allocator(), stagingBuf, stagingAlloc);
		return false;
	}

	TransitionToTransferDst(cmd);

	std::vector<VkBufferImageCopy2> regions(mipOffsets.size());
	for (size_t m = 0; m < mipOffsets.size(); ++m) {
		(void)mipSizes;
		const uint32_t mipW = std::max(1u, width >> m);
		const uint32_t mipH = std::max(1u, height >> m);

		VkBufferImageCopy2 &r = regions[m];
		r = {};
		r.sType             = VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2;
		r.bufferOffset      = (VkDeviceSize)mipOffsets[m];
		r.bufferRowLength   = 0;
		r.bufferImageHeight = 0;
		r.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
		r.imageSubresource.mipLevel       = (uint32_t)m;
		r.imageSubresource.baseArrayLayer = 0;
		r.imageSubresource.layerCount     = 1;
		r.imageOffset = { 0, 0, 0 };
		r.imageExtent = { mipW, mipH, 1 };
	}

	VkCopyBufferToImageInfo2 copyInfo{};
	copyInfo.sType          = VK_STRUCTURE_TYPE_COPY_BUFFER_TO_IMAGE_INFO_2;
	copyInfo.srcBuffer      = stagingBuf;
	copyInfo.dstImage       = image;
	copyInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	copyInfo.regionCount    = (uint32_t)regions.size();
	copyInfo.pRegions       = regions.data();
	vkCmdCopyBufferToImage2(cmd, &copyInfo);

	if (mipOffsets.size() < mipLevels)
		RegenerateMipmaps(cmd);
	else
		TransitionToShaderRead(cmd);

	gpuDirty = true;

	if (recordingFrame) {
		swapchain->DeferStagingFree(stagingBuf, stagingAlloc);
		return true;
	}

	const bool ok = swapchain->FlushOffscreenCommands();
	vmaDestroyBuffer(ctx->Allocator(), stagingBuf, stagingAlloc);
	return ok;
}

void VkSurfNative::TransitionLayout(
	VkCommandBuffer cmd,
	VkImageLayout newLayout,
	VkPipelineStageFlags2 srcStage,
	VkAccessFlags2 srcAccess,
	VkPipelineStageFlags2 dstStage,
	VkAccessFlags2 dstAccess)
{
	if (image == VK_NULL_HANDLE)
		return;
	if (currentLayout == newLayout) {
		if (newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL ||
			newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
		{
			VkMemoryBarrier2 mb{};
			mb.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
			mb.srcStageMask  = srcStage;
			mb.srcAccessMask = srcAccess;
			mb.dstStageMask  = dstStage;
			mb.dstAccessMask = dstAccess;

			VkDependencyInfo dep{};
			dep.sType              = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
			dep.memoryBarrierCount = 1;
			dep.pMemoryBarriers    = &mb;
			vkCmdPipelineBarrier2(cmd, &dep);
		}
		return;
	}

	VkImageMemoryBarrier2 barrier{};
	barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	barrier.srcStageMask        = srcStage;
	barrier.srcAccessMask       = srcAccess;
	barrier.dstStageMask        = dstStage;
	barrier.dstAccessMask       = dstAccess;
	barrier.oldLayout           = currentLayout;
	barrier.newLayout           = newLayout;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image               = image;
	barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseMipLevel   = 0;
	barrier.subresourceRange.levelCount     = mipLevels;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount     = 1;

	VkDependencyInfo dep{};
	dep.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	dep.imageMemoryBarrierCount = 1;
	dep.pImageMemoryBarriers    = &barrier;

	vkCmdPipelineBarrier2(cmd, &dep);
	currentLayout = newLayout;
}

void VkSurfNative::TransitionToShaderRead(VkCommandBuffer cmd)
{
	TransitionLayout(
		cmd,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
		VK_ACCESS_2_MEMORY_WRITE_BIT,
		VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
		VK_ACCESS_2_SHADER_READ_BIT);
}

void VkSurfNative::TransitionToTransferDst(VkCommandBuffer cmd)
{
	TransitionLayout(
		cmd,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
		VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
		VK_PIPELINE_STAGE_2_TRANSFER_BIT,
		VK_ACCESS_2_TRANSFER_WRITE_BIT);
}

void VkSurfNative::TransitionToTransferSrc(VkCommandBuffer cmd)
{
	TransitionLayout(
		cmd,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
		VK_ACCESS_2_MEMORY_WRITE_BIT,
		VK_PIPELINE_STAGE_2_TRANSFER_BIT,
		VK_ACCESS_2_TRANSFER_READ_BIT);
}

void VkSurfNative::TransitionToColorAttachment(VkCommandBuffer cmd)
{
	TransitionLayout(
		cmd,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
		VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
		VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
}

VkDescriptorSet VkSurfNative::GetImGuiDescriptorSet(VkSampler sampler)
{
	if (!ctx || !swapchain || imageView == VK_NULL_HANDLE || sampler == VK_NULL_HANDLE)
		return VK_NULL_HANDLE;
	if (!ImGui::GetCurrentContext() || ImGui::GetIO().BackendRendererUserData == nullptr)
		return VK_NULL_HANDLE;

	if (currentLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
		if (swapchain->IsFrameRecording()) {
			TransitionToShaderRead(swapchain->CurrentCommandBuffer());
		} else {
			VkCommandBuffer cmd = swapchain->OffscreenCommandBuffer();
			if (cmd != VK_NULL_HANDLE) {
				TransitionToShaderRead(cmd);
				swapchain->FlushOffscreenCommands();
			}
		}
	}

	if (imguiDescSet == VK_NULL_HANDLE) {
		imguiDescSet = ImGui_ImplVulkan_AddTexture(
			sampler,
			imageView,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	}
	return imguiDescSet;
}

VkSurfNative *VkSurfNative::Create(
	VkContext &context,
	VkSwapchain &swap,
	uint32_t width,
	uint32_t height,
	DWORD flags)
{
	if (width == 0 || height == 0)
		return nullptr;

	if (flags & (OAPISURFACE_RENDER3D | OAPISURFACE_SKETCHPAD))
		flags |= OAPISURFACE_RENDERTARGET;

	const DWORD pf = flags & OAPISURFACE_PF_MASK;
	const bool alpha = ((flags & OAPISURFACE_ALPHA) != 0) || (pf == OAPISURFACE_PF_ARGB);
	const bool wantDepth = (flags & OAPISURFACE_RENDER3D) != 0;

	VkImageUsageFlags usage =
		VK_IMAGE_USAGE_SAMPLED_BIT |
		VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
		VK_IMAGE_USAGE_TRANSFER_DST_BIT |
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

	auto *surf = new VkSurfNative(context, swap);
	surf->flags = flags;
	if (!surf->AllocateGpuImage(width, height, 1, VK_FORMAT_B8G8R8A8_UNORM, usage, alpha, wantDepth)) {
		delete surf;
		return nullptr;
	}

	VkCommandBuffer cmd = swap.OffscreenCommandBuffer();
	if (cmd != VK_NULL_HANDLE) {
		surf->TransitionToTransferDst(cmd);
		const VkClearColorValue clearVal = { { 0.0f, 0.0f, 0.0f, alpha ? 0.0f : 1.0f } };
		VkImageSubresourceRange range{};
		range.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
		range.baseMipLevel   = 0;
		range.levelCount     = 1;
		range.baseArrayLayer = 0;
		range.layerCount     = 1;
		vkCmdClearColorImage(cmd, surf->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearVal, 1, &range);
		surf->TransitionToShaderRead(cmd);
		swap.FlushOffscreenCommands();
	}

	return surf;
}

VkSurfNative *VkSurfNative::LoadFromFile(
	oapi::VulkanClient &client,
	VkContext &context,
	VkSwapchain &swap,
	const char *fname,
	DWORD flags,
	bool bPath,
	bool loadAuxMaps)
{
	if (!fname || !fname[0])
		return nullptr;

	if (flags & (OAPISURFACE_RENDER3D | OAPISURFACE_SKETCHPAD))
		flags |= OAPISURFACE_RENDERTARGET;
	if (flags & (OAPISURFACE_RENDERTARGET | OAPISURFACE_GDI | OAPISURFACE_SYSMEM))
		flags |= OAPISURFACE_UNCOMPRESS;

	char fullPath[MAX_PATH]{};
	if (bPath) {
		strncpy_s(fullPath, sizeof(fullPath), fname, _TRUNCATE);
	} else {
		if (!client.TexturePath(fname, fullPath))
			return nullptr;
	}

	std::vector<uint8_t> fileBytes;
	if (!ReadAllFileBytes(fullPath, fileBytes) || fileBytes.size() < 4)
		return nullptr;

	uint32_t magic = 0;
	std::memcpy(&magic, fileBytes.data(), sizeof(uint32_t));

	VkSurfNative *surf = nullptr;

	if (magic == kDdsMagic && fileBytes.size() >= 4 + sizeof(DdsHeader)) {
		DdsHeader hdr{};
		std::memcpy(&hdr, fileBytes.data() + 4, sizeof(DdsHeader));

		const uint32_t w = std::max(1u, hdr.width);
		const uint32_t h = std::max(1u, hdr.height);
		uint32_t fileMips = std::max(1u, hdr.mipMapCount);
		if (flags & (OAPISURFACE_NOMIPMAPS | OAPISURFACE_RENDERTARGET | OAPISURFACE_GDI | OAPISURFACE_SYSMEM))
			fileMips = 1;

		const uint8_t *payload = fileBytes.data() + 4 + sizeof(DdsHeader);
		const size_t payloadLen = fileBytes.size() - (4 + sizeof(DdsHeader));

		const bool isFourCC = (hdr.ddspf.flags & kDdpfFourCC) != 0;
		const uint32_t fourCC = hdr.ddspf.fourCC;

		if (isFourCC && (fourCC == kFourCC_DXT1 || fourCC == kFourCC_DXT2 || fourCC == kFourCC_DXT3 || fourCC == kFourCC_DXT4 || fourCC == kFourCC_DXT5)) {
			VkFormat bcFmt = VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
			size_t blockBytes = 8;
			bool alpha = false;
			if (fourCC == kFourCC_DXT2 || fourCC == kFourCC_DXT3) {
				bcFmt = VK_FORMAT_BC2_UNORM_BLOCK;
				blockBytes = 16;
				alpha = true;
			} else if (fourCC == kFourCC_DXT4 || fourCC == kFourCC_DXT5) {
				bcFmt = VK_FORMAT_BC3_UNORM_BLOCK;
				blockBytes = 16;
				alpha = true;
			} else {
				bcFmt = VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
				blockBytes = 8;
				alpha = true;
			}

			const bool forceUncompress = (flags & OAPISURFACE_UNCOMPRESS) != 0;
			if (forceUncompress) {
				const size_t mip0Size = (size_t)std::max(1u, (w + 3u) / 4u) * std::max(1u, (h + 3u) / 4u) * blockBytes;
				if (payloadLen < mip0Size)
					return nullptr;

				std::vector<uint8_t> bgra;
				bool decodedAlpha = false;
				DecompressBcImageToBgra(payload, w, h, bcFmt, bgra, decodedAlpha);
				if (flags & OAPISURFACE_NOALPHA)
					decodedAlpha = false;
				else if (flags & OAPISURFACE_ALPHA)
					decodedAlpha = true;

				VkImageUsageFlags usage =
					VK_IMAGE_USAGE_SAMPLED_BIT |
					VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
					VK_IMAGE_USAGE_TRANSFER_DST_BIT |
					VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

				surf = new VkSurfNative(context, swap);
				surf->flags      = flags;
				surf->sourcePath = fullPath;
				surf->SetName(fname);
				if (!surf->AllocateGpuImage(w, h, 1, VK_FORMAT_B8G8R8A8_UNORM, usage, decodedAlpha, (flags & OAPISURFACE_RENDER3D) != 0) ||
					!surf->UploadMipChain(bgra.data(), bgra.size(), { 0 }, { bgra.size() })) {
					delete surf;
					return nullptr;
				}
			} else {
				std::vector<size_t> mipOffsets;
				std::vector<size_t> mipSizes;
				size_t cursor = 0;
				for (uint32_t m = 0; m < fileMips; ++m) {
					const uint32_t mw = std::max(1u, w >> m);
					const uint32_t mh = std::max(1u, h >> m);
					const size_t sz = (size_t)std::max(1u, (mw + 3u) / 4u) * std::max(1u, (mh + 3u) / 4u) * blockBytes;
					if (cursor + sz > payloadLen)
						break;
					mipOffsets.push_back(cursor);
					mipSizes.push_back(sz);
					cursor += sz;
				}
				if (mipOffsets.empty())
					return nullptr;

				VkImageUsageFlags usage =
					VK_IMAGE_USAGE_SAMPLED_BIT |
					VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
					VK_IMAGE_USAGE_TRANSFER_DST_BIT;

				surf = new VkSurfNative(context, swap);
				surf->flags      = flags;
				surf->sourcePath = fullPath;
				surf->SetName(fname);
				surf->compressedMip0.assign(payload, payload + mipSizes[0]);
				if (!surf->AllocateGpuImage(w, h, (uint32_t)mipOffsets.size(), bcFmt, usage, alpha, false) ||
					!surf->UploadMipChain(payload, cursor, mipOffsets, mipSizes)) {
					delete surf;
					return nullptr;
				}
			}
		} else {
			// Uncompressed DDS (32-bit, 24-bit, 16-bit A4R4G4B4/R5G6B5, 8-bit A8/L8) -> convert mip chain to BGRA8
			const uint32_t bpp = hdr.ddspf.rgbBitCount;
			const size_t bytesPerPix = bpp / 8u;
			if (bytesPerPix == 0 || bytesPerPix > 4)
				return nullptr;

			std::vector<uint8_t> bgraAll;
			std::vector<size_t> mipOffsets;
			std::vector<size_t> mipSizes;
			size_t srcCursor = 0;
			bool alpha = ((hdr.ddspf.flags & (kDdpfAlpha | kDdpfAlphaOnly)) != 0) || (hdr.ddspf.aBitMask != 0);

			for (uint32_t m = 0; m < fileMips; ++m) {
				const uint32_t mw = std::max(1u, w >> m);
				const uint32_t mh = std::max(1u, h >> m);
				const size_t srcMipBytes = (size_t)mw * mh * bytesPerPix;
				if (srcCursor + srcMipBytes > payloadLen)
					break;

				const size_t dstOffset = bgraAll.size();
				const size_t dstMipBytes = (size_t)mw * mh * 4u;
				bgraAll.resize(dstOffset + dstMipBytes);

				const uint8_t *src = payload + srcCursor;
				uint8_t *dst = bgraAll.data() + dstOffset;
				const size_t numPix = (size_t)mw * mh;

				if (bpp == 32) {
					const bool swapRB = (hdr.ddspf.rBitMask == 0x000000FFu);
					const bool hasAMask = (hdr.ddspf.aBitMask != 0);
					for (size_t i = 0; i < numPix; ++i) {
						dst[i * 4 + 0] = swapRB ? src[i * 4 + 2] : src[i * 4 + 0];
						dst[i * 4 + 1] = src[i * 4 + 1];
						dst[i * 4 + 2] = swapRB ? src[i * 4 + 0] : src[i * 4 + 2];
						dst[i * 4 + 3] = hasAMask ? src[i * 4 + 3] : 255;
					}
				} else if (bpp == 24) {
					for (size_t i = 0; i < numPix; ++i) {
						dst[i * 4 + 0] = src[i * 3 + 0];
						dst[i * 4 + 1] = src[i * 3 + 1];
						dst[i * 4 + 2] = src[i * 3 + 2];
						dst[i * 4 + 3] = 255;
					}
				} else if (bpp == 16) {
					if (hdr.ddspf.rBitMask == 0x0F00u) {
						// A4R4G4B4 or X4R4G4B4
						const bool hasA4 = (hdr.ddspf.aBitMask == 0xF000u);
						for (size_t i = 0; i < numPix; ++i) {
							const uint16_t v = (uint16_t)(src[i * 2 + 0] | (src[i * 2 + 1] << 8));
							dst[i * 4 + 0] = (uint8_t)((v & 0xFu) * 17u);
							dst[i * 4 + 1] = (uint8_t)(((v >> 4) & 0xFu) * 17u);
							dst[i * 4 + 2] = (uint8_t)(((v >> 8) & 0xFu) * 17u);
							dst[i * 4 + 3] = hasA4 ? (uint8_t)(((v >> 12) & 0xFu) * 17u) : 255;
						}
					} else {
						// R5G6B5 or A1R5G5B5
						const bool is5551 = (hdr.ddspf.gBitMask == 0x03E0u);
						for (size_t i = 0; i < numPix; ++i) {
							const uint16_t v = (uint16_t)(src[i * 2 + 0] | (src[i * 2 + 1] << 8));
							if (is5551) {
								dst[i * 4 + 0] = (uint8_t)((v & 0x1Fu) * 255u / 31u);
								dst[i * 4 + 1] = (uint8_t)(((v >> 5) & 0x1Fu) * 255u / 31u);
								dst[i * 4 + 2] = (uint8_t)(((v >> 10) & 0x1Fu) * 255u / 31u);
								dst[i * 4 + 3] = (hdr.ddspf.aBitMask ? ((v & 0x8000u) ? 255 : 0) : 255);
							} else {
								DecodeRgb565(v, dst[i * 4 + 2], dst[i * 4 + 1], dst[i * 4 + 0]);
								dst[i * 4 + 3] = 255;
							}
						}
					}
				} else if (bpp == 8) {
					const bool isAlphaOnly = (hdr.ddspf.flags & kDdpfAlphaOnly) != 0 || (hdr.ddspf.aBitMask == 0xFFu && hdr.ddspf.rBitMask == 0);
					for (size_t i = 0; i < numPix; ++i) {
						const uint8_t v = src[i];
						if (isAlphaOnly) {
							dst[i * 4 + 0] = 255;
							dst[i * 4 + 1] = 255;
							dst[i * 4 + 2] = 255;
							dst[i * 4 + 3] = v;
						} else {
							dst[i * 4 + 0] = v;
							dst[i * 4 + 1] = v;
							dst[i * 4 + 2] = v;
							dst[i * 4 + 3] = 255;
						}
					}
				}

				mipOffsets.push_back(dstOffset);
				mipSizes.push_back(dstMipBytes);
				srcCursor += srcMipBytes;
			}

			if (mipOffsets.empty())
				return nullptr;

			if (flags & OAPISURFACE_NOALPHA)
				alpha = false;
			else if (flags & OAPISURFACE_ALPHA)
				alpha = true;

			VkImageUsageFlags usage =
				VK_IMAGE_USAGE_SAMPLED_BIT |
				VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
				VK_IMAGE_USAGE_TRANSFER_DST_BIT |
				VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

			surf = new VkSurfNative(context, swap);
			surf->flags      = flags;
			surf->sourcePath = fullPath;
			surf->SetName(fname);
			if (!surf->AllocateGpuImage(w, h, (uint32_t)mipOffsets.size(), VK_FORMAT_B8G8R8A8_UNORM, usage, alpha, (flags & OAPISURFACE_RENDER3D) != 0) ||
				!surf->UploadMipChain(bgraAll.data(), bgraAll.size(), mipOffsets, mipSizes)) {
				delete surf;
				return nullptr;
			}
		}
	} else {
		// Non-DDS file (PNG, BMP, JPG, TIF, or BMP disguised with .dds extension like Textures\adi.dds)
		std::vector<uint8_t> bgra;
		uint32_t w = 0, h = 0;
		bool alpha = false;
		if (!DecodeImageViaWIC(fullPath, bgra, w, h, alpha))
			return nullptr;

		if (flags & OAPISURFACE_NOALPHA)
			alpha = false;
		else if (flags & OAPISURFACE_ALPHA)
			alpha = true;

		VkImageUsageFlags usage =
			VK_IMAGE_USAGE_SAMPLED_BIT |
			VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
			VK_IMAGE_USAGE_TRANSFER_DST_BIT |
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

		surf = new VkSurfNative(context, swap);
		surf->flags      = flags;
		surf->sourcePath = fullPath;
		surf->SetName(fname);
		if (!surf->AllocateGpuImage(w, h, 1, VK_FORMAT_B8G8R8A8_UNORM, usage, alpha, (flags & OAPISURFACE_RENDER3D) != 0) ||
			!surf->UploadMipChain(bgra.data(), bgra.size(), { 0 }, { bgra.size() })) {
			delete surf;
			return nullptr;
		}
	}

	if (surf && loadAuxMaps && !bPath && ((flags & ~(OAPISURFACE_TEXTURE | OAPISURFACE_SHARED)) == 0)) {
		struct AuxEntry { uint32_t slot; const char *suffix; };
		static const AuxEntry kAuxTable[] = {
			{ MAP_HEAT,          "heat"   },
			{ MAP_NORMAL,        "norm"   },
			{ MAP_SPECULAR,      "spec"   },
			{ MAP_EMISSION,      "emis"   },
			{ MAP_ROUGHNESS,     "rghn"   },
			{ MAP_METALNESS,     "metal"  },
			{ MAP_REFLECTION,    "refl"   },
			{ MAP_TRANSLUCENCE,  "transl" },
			{ MAP_TRANSMITTANCE, "transm" }
		};
		for (const AuxEntry &e : kAuxTable) {
			char auxName[MAX_PATH]{};
			if (BuildAuxMapFilename(auxName, sizeof(auxName), fname, e.suffix)) {
				VkSurfNative *auxSurf = LoadFromFile(client, context, swap, auxName, OAPISURFACE_TEXTURE, false, false);
				if (auxSurf)
					surf->SetMap(e.slot, auxSurf);
			}
		}
	}

	return surf;
}

bool VkSurfNative::CopyFromHBITMAP(HBITMAP hbm, int srcX, int srcY, int srcW, int srcH)
{
	if (!hbm || !ctx || !swapchain || width == 0 || height == 0)
		return false;

	if (IsCompressed() && !Decompress())
		return false;

	BITMAP bm{};
	if (GetObject(hbm, sizeof(bm), &bm) == 0)
		return false;
	if (srcW <= 0) srcW = bm.bmWidth;
	if (srcH <= 0) srcH = bm.bmHeight;

	HDC hdcSrc = CreateCompatibleDC(nullptr);
	HDC hdcDst = CreateCompatibleDC(nullptr);
	if (!hdcSrc || !hdcDst) {
		if (hdcSrc) DeleteDC(hdcSrc);
		if (hdcDst) DeleteDC(hdcDst);
		return false;
	}

	BITMAPINFO bmi{};
	bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth       = (LONG)width;
	bmi.bmiHeader.biHeight      = -(LONG)height;
	bmi.bmiHeader.biPlanes      = 1;
	bmi.bmiHeader.biBitCount    = 32;
	bmi.bmiHeader.biCompression = BI_RGB;

	void *dibBits = nullptr;
	HBITMAP hDib = CreateDIBSection(hdcDst, &bmi, DIB_RGB_COLORS, &dibBits, nullptr, 0);
	if (!hDib || !dibBits) {
		if (hDib) DeleteObject(hDib);
		DeleteDC(hdcDst);
		DeleteDC(hdcSrc);
		return false;
	}

	HGDIOBJ oldSrc = SelectObject(hdcSrc, hbm);
	HGDIOBJ oldDst = SelectObject(hdcDst, hDib);
	SetStretchBltMode(hdcDst, HALFTONE);
	StretchBlt(hdcDst, 0, 0, (int)width, (int)height, hdcSrc, srcX, srcY, srcW, srcH, SRCCOPY);
	GdiFlush();

	const size_t byteSize = (size_t)width * height * 4u;
	std::vector<uint8_t> bgra(byteSize);
	std::memcpy(bgra.data(), dibBits, byteSize);

	SelectObject(hdcDst, oldDst);
	SelectObject(hdcSrc, oldSrc);
	DeleteObject(hDib);
	DeleteDC(hdcDst);
	DeleteDC(hdcSrc);

	if (!hasAlpha) {
		for (size_t i = 3; i < byteSize; i += 4)
			bgra[i] = 255;
	}

	return UploadMipChain(bgra.data(), bgra.size(), { 0 }, { bgra.size() });
}

bool VkSurfNative::Decompress()
{
	if (!IsCompressed())
		return true;
	if (compressedMip0.empty() || !ctx || !swapchain)
		return false;

	std::vector<uint8_t> bgra;
	bool decodedAlpha = false;
	DecompressBcImageToBgra(compressedMip0.data(), width, height, format, bgra, decodedAlpha);

	const uint32_t w = width;
	const uint32_t h = height;
	flags |= OAPISURFACE_RENDERTARGET | OAPISURFACE_TEXTURE | OAPISURFACE_UNCOMPRESS;

	VkImageUsageFlags usage =
		VK_IMAGE_USAGE_SAMPLED_BIT |
		VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
		VK_IMAGE_USAGE_TRANSFER_DST_BIT |
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

	if (!AllocateGpuImage(w, h, 1, VK_FORMAT_B8G8R8A8_UNORM, usage, decodedAlpha, (flags & OAPISURFACE_RENDER3D) != 0))
		return false;

	compressedMip0.clear();
	return UploadMipChain(bgra.data(), bgra.size(), { 0 }, { bgra.size() });
}

bool VkSurfNative::ReadbackBgra(std::vector<uint8_t> &outBgra, uint32_t &outWidth, uint32_t &outHeight)
{
	if (!ctx || !swapchain || image == VK_NULL_HANDLE || width == 0 || height == 0)
		return false;

	outWidth  = width;
	outHeight = height;

	if (IsCompressed()) {
		if (compressedMip0.empty())
			return false;
		bool dummyAlpha = false;
		DecompressBcImageToBgra(compressedMip0.data(), width, height, format, outBgra, dummyAlpha);
		return true;
	}

	if (!swapchain->FlushOffscreenCommands())
		return false;

	const VkDeviceSize byteSize = (VkDeviceSize)width * height * 4u;

	VkBufferCreateInfo bufInfo{};
	bufInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufInfo.size        = byteSize;
	bufInfo.usage       = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VmaAllocationCreateInfo allocInfo{};
	allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
	allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

	VkBuffer stagingBuf = VK_NULL_HANDLE;
	VmaAllocation stagingAlloc = VK_NULL_HANDLE;
	VmaAllocationInfo mapInfo{};
	VkResult vr = vmaCreateBuffer(ctx->Allocator(), &bufInfo, &allocInfo, &stagingBuf, &stagingAlloc, &mapInfo);
	if (vr != VK_SUCCESS || !mapInfo.pMappedData) {
		if (stagingBuf != VK_NULL_HANDLE)
			vmaDestroyBuffer(ctx->Allocator(), stagingBuf, stagingAlloc);
		return false;
	}

	VkCommandBuffer cmd = swapchain->OffscreenCommandBuffer();
	if (cmd == VK_NULL_HANDLE) {
		vmaDestroyBuffer(ctx->Allocator(), stagingBuf, stagingAlloc);
		return false;
	}

	const VkImageLayout prevLayout = currentLayout;
	TransitionToTransferSrc(cmd);

	VkBufferImageCopy2 copyRegion{};
	copyRegion.sType             = VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2;
	copyRegion.bufferOffset      = 0;
	copyRegion.bufferRowLength   = 0;
	copyRegion.bufferImageHeight = 0;
	copyRegion.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
	copyRegion.imageSubresource.mipLevel       = 0;
	copyRegion.imageSubresource.baseArrayLayer = 0;
	copyRegion.imageSubresource.layerCount     = 1;
	copyRegion.imageOffset = { 0, 0, 0 };
	copyRegion.imageExtent = { width, height, 1 };

	VkCopyImageToBufferInfo2 copyInfo{};
	copyInfo.sType          = VK_STRUCTURE_TYPE_COPY_IMAGE_TO_BUFFER_INFO_2;
	copyInfo.srcImage       = image;
	copyInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	copyInfo.dstBuffer      = stagingBuf;
	copyInfo.regionCount    = 1;
	copyInfo.pRegions       = &copyRegion;
	vkCmdCopyImageToBuffer2(cmd, &copyInfo);

	if (prevLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
		TransitionToShaderRead(cmd);
	else if (prevLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
		TransitionToColorAttachment(cmd);
	else if (prevLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
		TransitionToTransferDst(cmd);

	const bool ok = swapchain->FlushOffscreenCommands();
	if (ok) {
		vmaInvalidateAllocation(ctx->Allocator(), stagingAlloc, 0, byteSize);
		outBgra.resize((size_t)byteSize);
		std::memcpy(outBgra.data(), mapInfo.pMappedData, (size_t)byteSize);
	}

	vmaDestroyBuffer(ctx->Allocator(), stagingBuf, stagingAlloc);
	return ok;
}

VkImageView VkSurfNative::GetRenderTargetView()
{
	if (mipLevels <= 1)
		return imageView;
	if (rtMip0View != VK_NULL_HANDLE)
		return rtMip0View;
	if (!ctx || image == VK_NULL_HANDLE)
		return VK_NULL_HANDLE;

	VkImageViewCreateInfo viewInfo{};
	viewInfo.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image    = image;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format   = format;
	viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
	viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
	viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
	viewInfo.components.a = hasAlpha ? VK_COMPONENT_SWIZZLE_IDENTITY : VK_COMPONENT_SWIZZLE_ONE;
	viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
	viewInfo.subresourceRange.baseMipLevel   = 0;
	viewInfo.subresourceRange.levelCount     = 1;
	viewInfo.subresourceRange.baseArrayLayer = 0;
	viewInfo.subresourceRange.layerCount     = 1;

	if (vkCreateImageView(ctx->Device(), &viewInfo, nullptr, &rtMip0View) != VK_SUCCESS)
		return VK_NULL_HANDLE;
	return rtMip0View;
}

bool VkSurfNative::EnsureTempBlitImage()
{
	if (tempBlitImage != VK_NULL_HANDLE && tempBlitView != VK_NULL_HANDLE)
		return true;
	if (!ctx || width == 0 || height == 0)
		return false;

	VkImageCreateInfo ici{};
	ici.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	ici.imageType     = VK_IMAGE_TYPE_2D;
	ici.format        = format;
	ici.extent        = { width, height, 1 };
	ici.mipLevels     = 1;
	ici.arrayLayers   = 1;
	ici.samples       = VK_SAMPLE_COUNT_1_BIT;
	ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
	ici.usage         = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
	ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	VmaAllocationCreateInfo aci{};
	aci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

	if (vmaCreateImage(ctx->Allocator(), &ici, &aci, &tempBlitImage, &tempBlitAlloc, nullptr) != VK_SUCCESS) {
		tempBlitImage = VK_NULL_HANDLE;
		tempBlitAlloc = VK_NULL_HANDLE;
		return false;
	}

	VkImageViewCreateInfo ivci{};
	ivci.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	ivci.image    = tempBlitImage;
	ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
	ivci.format   = format;
	ivci.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
	ivci.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
	ivci.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
	ivci.components.a = hasAlpha ? VK_COMPONENT_SWIZZLE_IDENTITY : VK_COMPONENT_SWIZZLE_ONE;
	ivci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
	ivci.subresourceRange.baseMipLevel   = 0;
	ivci.subresourceRange.levelCount     = 1;
	ivci.subresourceRange.baseArrayLayer = 0;
	ivci.subresourceRange.layerCount     = 1;

	if (vkCreateImageView(ctx->Device(), &ivci, nullptr, &tempBlitView) != VK_SUCCESS) {
		vmaDestroyImage(ctx->Allocator(), tempBlitImage, tempBlitAlloc);
		tempBlitImage = VK_NULL_HANDLE;
		tempBlitAlloc = VK_NULL_HANDLE;
		return false;
	}
	return true;
}

void VkSurfNative::RegenerateMipmaps(VkCommandBuffer cmd)
{
	if (mipLevels <= 1 || IsCompressed() || image == VK_NULL_HANDLE) {
		TransitionToShaderRead(cmd);
		return;
	}

	// Transition mip 0 to TRANSFER_SRC and mips 1..N-1 to TRANSFER_DST
	VkImageMemoryBarrier2 initBarriers[2]{};
	initBarriers[0].sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	initBarriers[0].srcStageMask        = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
	initBarriers[0].srcAccessMask       = VK_ACCESS_2_MEMORY_WRITE_BIT;
	initBarriers[0].dstStageMask        = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
	initBarriers[0].dstAccessMask       = VK_ACCESS_2_TRANSFER_READ_BIT;
	initBarriers[0].oldLayout           = currentLayout;
	initBarriers[0].newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	initBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	initBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	initBarriers[0].image               = image;
	initBarriers[0].subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

	initBarriers[1]                     = initBarriers[0];
	initBarriers[1].dstAccessMask       = VK_ACCESS_2_TRANSFER_WRITE_BIT;
	initBarriers[1].oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
	initBarriers[1].newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	initBarriers[1].subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 1, mipLevels - 1, 0, 1 };

	VkDependencyInfo dep{};
	dep.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	dep.imageMemoryBarrierCount = 2;
	dep.pImageMemoryBarriers    = initBarriers;
	vkCmdPipelineBarrier2(cmd, &dep);

	int32_t mipW = (int32_t)width;
	int32_t mipH = (int32_t)height;
	for (uint32_t m = 1; m < mipLevels; ++m) {
		const int32_t nextW = std::max(1, mipW >> 1);
		const int32_t nextH = std::max(1, mipH >> 1);

		VkImageBlit2 region{};
		region.sType             = VK_STRUCTURE_TYPE_IMAGE_BLIT_2;
		region.srcSubresource    = { VK_IMAGE_ASPECT_COLOR_BIT, m - 1, 0, 1 };
		region.srcOffsets[0]     = { 0, 0, 0 };
		region.srcOffsets[1]     = { mipW, mipH, 1 };
		region.dstSubresource    = { VK_IMAGE_ASPECT_COLOR_BIT, m, 0, 1 };
		region.dstOffsets[0]     = { 0, 0, 0 };
		region.dstOffsets[1]     = { nextW, nextH, 1 };

		VkBlitImageInfo2 blitInfo{};
		blitInfo.sType          = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2;
		blitInfo.srcImage       = image;
		blitInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		blitInfo.dstImage       = image;
		blitInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		blitInfo.regionCount    = 1;
		blitInfo.pRegions       = &region;
		blitInfo.filter         = VK_FILTER_LINEAR;
		vkCmdBlitImage2(cmd, &blitInfo);

		VkImageMemoryBarrier2 mipToSrc{};
		mipToSrc.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
		mipToSrc.srcStageMask        = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
		mipToSrc.srcAccessMask       = VK_ACCESS_2_TRANSFER_WRITE_BIT;
		mipToSrc.dstStageMask        = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
		mipToSrc.dstAccessMask       = VK_ACCESS_2_TRANSFER_READ_BIT;
		mipToSrc.oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		mipToSrc.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		mipToSrc.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		mipToSrc.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		mipToSrc.image               = image;
		mipToSrc.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, m, 1, 0, 1 };

		dep.imageMemoryBarrierCount = 1;
		dep.pImageMemoryBarriers    = &mipToSrc;
		vkCmdPipelineBarrier2(cmd, &dep);

		mipW = nextW;
		mipH = nextH;
	}

	VkImageMemoryBarrier2 allToShader{};
	allToShader.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	allToShader.srcStageMask        = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
	allToShader.srcAccessMask       = VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT;
	allToShader.dstStageMask        = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
	allToShader.dstAccessMask       = VK_ACCESS_2_SHADER_READ_BIT;
	allToShader.oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	allToShader.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	allToShader.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	allToShader.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	allToShader.image               = image;
	allToShader.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, 1 };

	dep.imageMemoryBarrierCount = 1;
	dep.pImageMemoryBarriers    = &allToShader;
	vkCmdPipelineBarrier2(cmd, &dep);
	currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

bool VkSurfNative::Fill(const RECT *rect, DWORD col)
{
	if (!ctx || !swapchain || width == 0 || height == 0)
		return false;
	if (IsCompressed() && !Decompress())
		return false;
	if (image == VK_NULL_HANDLE)
		return false;

	VkCommandBuffer cmd = swapchain->ActiveCommandBuffer();
	if (cmd == VK_NULL_HANDLE)
		return false;

	VkClearColorValue clearVal{};
	clearVal.float32[0] = (float)((col >> 16) & 0xFFu) / 255.0f;
	clearVal.float32[1] = (float)((col >> 8) & 0xFFu) / 255.0f;
	clearVal.float32[2] = (float)(col & 0xFFu) / 255.0f;
	clearVal.float32[3] = hasAlpha ? ((float)((col >> 24) & 0xFFu) / 255.0f) : 1.0f;

	const bool fullClear = (!rect) ||
		(rect->left <= 0 && rect->top <= 0 &&
		 rect->right >= (LONG)width &&
		 rect->bottom >= (LONG)height);

	if (fullClear) {
		TransitionToTransferDst(cmd);
		VkImageSubresourceRange range{};
		range.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
		range.baseMipLevel   = 0;
		range.levelCount     = mipLevels;
		range.baseArrayLayer = 0;
		range.layerCount     = 1;
		vkCmdClearColorImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearVal, 1, &range);
		gpuDirty = true;
		return true;
	}

	const int32_t x0 = std::max<int32_t>(0, rect->left);
	const int32_t y0 = std::max<int32_t>(0, rect->top);
	const int32_t x1 = std::min<int32_t>((int32_t)width, rect->right);
	const int32_t y1 = std::min<int32_t>((int32_t)height, rect->bottom);
	if (x1 <= x0 || y1 <= y0)
		return true;

	VkImageView rtView = GetRenderTargetView();
	if (rtView == VK_NULL_HANDLE)
		return false;

	TransitionToColorAttachment(cmd);

	VkRenderingAttachmentInfo colorAtt{};
	colorAtt.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	colorAtt.imageView   = rtView;
	colorAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	colorAtt.loadOp      = VK_ATTACHMENT_LOAD_OP_LOAD;
	colorAtt.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;

	VkRenderingInfo ri{};
	ri.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO;
	ri.renderArea.offset    = { 0, 0 };
	ri.renderArea.extent    = { width, height };
	ri.layerCount           = 1;
	ri.colorAttachmentCount = 1;
	ri.pColorAttachments    = &colorAtt;

	vkCmdBeginRendering(cmd, &ri);

	VkClearAttachment ca{};
	ca.aspectMask       = VK_IMAGE_ASPECT_COLOR_BIT;
	ca.colorAttachment  = 0;
	ca.clearValue.color = clearVal;

	VkClearRect cr{};
	cr.rect.offset    = { x0, y0 };
	cr.rect.extent    = { (uint32_t)(x1 - x0), (uint32_t)(y1 - y0) };
	cr.baseArrayLayer = 0;
	cr.layerCount     = 1;
	vkCmdClearAttachments(cmd, 1, &ca, 1, &cr);

	vkCmdEndRendering(cmd);

	if (mipLevels > 1)
		RegenerateMipmaps(cmd);

	gpuDirty = true;
	return true;
}

HDC VkSurfNative::GetDC()
{
	if (!ctx || !swapchain || width == 0 || height == 0)
		return nullptr;
	if (gdiOpen) {
		VkLogWarn("VkSurfNative::GetDC called while DC is already open on '%s'", name);
		return nullptr;
	}
	if (IsCompressed() && !Decompress())
		return nullptr;

	const size_t byteSize = (size_t)width * height * 4u;

	if (!gdiMemDC) {
		gdiMemDC = CreateCompatibleDC(nullptr);
		if (!gdiMemDC)
			return nullptr;

		BITMAPINFO bmi{};
		bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
		bmi.bmiHeader.biWidth       = (LONG)width;
		bmi.bmiHeader.biHeight      = -(LONG)height; // top-down DIB
		bmi.bmiHeader.biPlanes      = 1;
		bmi.bmiHeader.biBitCount    = 32;
		bmi.bmiHeader.biCompression = BI_RGB;

		void *bits = nullptr;
		gdiBitmap = CreateDIBSection(gdiMemDC, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
		if (!gdiBitmap || !bits) {
			DestroyGdiResources();
			return nullptr;
		}
		gdiBits = static_cast<uint8_t *>(bits);
		gdiOldObj = SelectObject(gdiMemDC, gdiBitmap);

		if (!hasAlpha) {
			for (size_t i = 3; i < byteSize; i += 4)
				gdiBits[i] = 255;
		}
	}

	if (gpuDirty && currentLayout != VK_IMAGE_LAYOUT_UNDEFINED && !swapchain->IsFrameRecording()) {
		std::vector<uint8_t> readback;
		uint32_t rw = 0, rh = 0;
		if (ReadbackBgra(readback, rw, rh) && readback.size() == byteSize) {
			std::memcpy(gdiBits, readback.data(), byteSize);
			gpuDirty = false;
		}
	}

	if (hasAlpha) {
		gdiPreSnapshot.resize(byteSize);
		std::memcpy(gdiPreSnapshot.data(), gdiBits, byteSize);
	}

	gdiOpen = true;
	return gdiMemDC;
}

void VkSurfNative::ReleaseDC(HDC hDC)
{
	if (!gdiOpen || !hDC || hDC != gdiMemDC || !gdiBits)
		return;

	GdiFlush();
	gdiOpen = false;

	const size_t byteSize = (size_t)width * height * 4u;
	if (!hasAlpha) {
		for (size_t i = 3; i < byteSize; i += 4)
			gdiBits[i] = 255;
	} else if (gdiPreSnapshot.size() == byteSize) {
		const auto *beforePx = reinterpret_cast<const uint32_t *>(gdiPreSnapshot.data());
		auto *afterPx = reinterpret_cast<uint32_t *>(gdiBits);
		const size_t numPix = (size_t)width * height;
		for (size_t i = 0; i < numPix; ++i) {
			if (afterPx[i] != beforePx[i] && (afterPx[i] & 0xFF000000u) == 0)
				afterPx[i] |= 0xFF000000u;
		}
	}

	if (UploadMipChain(gdiBits, byteSize, { 0 }, { byteSize }))
		gpuDirty = false;
}
