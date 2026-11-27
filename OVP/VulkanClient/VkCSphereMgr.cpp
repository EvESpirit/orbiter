// ==============================================================
// VkCSphereMgr.cpp
// Tiled celestial background & star-image sphere manager
// Part of the ORBITER VISUALISATION PROJECT (OVP) - VulkanClient
// Licensed under the MIT License
// ==============================================================

#include "VkCSphereMgr.h"
#include "VulkanClient.h"
#include "VkContext.h"
#include "VkSwapchain.h"
#include "VkSurface.h"
#include "VkScene.h"
#include "VkLog.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>

namespace {
constexpr double kPi = 3.14159265358979323846;

struct DdsSpan {
	size_t offset = 0;
	size_t size = 0;
};

struct alignas(16) CelSpherePC {
	oapi::FMATRIX4 mWVP;
	float color[4];
};
static_assert(sizeof(CelSpherePC) == 80, "CelSphere push constant must be 80 bytes");

static bool EndsWithTexIgnoreCase(const std::string &s)
{
	if (s.size() < 4)
		return false;
	const size_t n = s.size();
	return std::tolower(static_cast<unsigned char>(s[n - 4])) == '.' &&
	       std::tolower(static_cast<unsigned char>(s[n - 3])) == 't' &&
	       std::tolower(static_cast<unsigned char>(s[n - 2])) == 'e' &&
	       std::tolower(static_cast<unsigned char>(s[n - 1])) == 'x';
}
} // namespace

VkCSphereMgr::~VkCSphereMgr()
{
	Destroy(ownerCtx);
}

void VkCSphereMgr::Destroy(VkContext *ctx)
{
	VkContext *useCtx = ctx ? ctx : ownerCtx;
	if (!tiles.empty() && useCtx && useCtx->IsValid())
		vkDeviceWaitIdle(useCtx->Device());

	for (Tile &t : tiles) {
		if (t.surf) {
			if (t.surf->DecRef())
				delete t.surf;
			t.surf = nullptr;
		}
		t.descSet = VK_NULL_HANDLE;
	}
	tiles.clear();

	if (useCtx && useCtx->IsValid()) {
		if (descPool != VK_NULL_HANDLE) {
			vkDestroyDescriptorPool(useCtx->Device(), descPool, nullptr);
			descPool = VK_NULL_HANDLE;
		}
		for (VBMESH &m : bands)
			m.Destroy(*useCtx);
	}
	bands.clear();
	loadedPath.clear();
	loadedLevel = 0;
	ownerCtx = nullptr;
}

bool VkCSphereMgr::ResolveTexFile(oapi::VulkanClient &client, const char *relPath, std::string &outFullPath) const
{
	if (!relPath || !relPath[0])
		return false;

	char fullBuf[MAX_PATH]{};
	std::string base(relPath);
	if (!EndsWithTexIgnoreCase(base)) {
		const std::string withTex = base + ".tex";
		if (client.TexturePath(withTex.c_str(), fullBuf) && fullBuf[0]) {
			std::ifstream probe(fullBuf, std::ios::binary);
			if (probe.is_open()) {
				outFullPath = fullBuf;
				return true;
			}
		}
	}

	fullBuf[0] = '\0';
	if (client.TexturePath(base.c_str(), fullBuf) && fullBuf[0]) {
		std::ifstream probe(fullBuf, std::ios::binary);
		if (probe.is_open()) {
			outFullPath = fullBuf;
			return true;
		}
	}

	return false;
}

bool VkCSphereMgr::Load(
	oapi::VulkanClient &client,
	const char *texRelPath,
	VkDescriptorSetLayout texSetLayout,
	VkSampler sampler,
	const oapi::FMATRIX4 &galToEcl)
{
	VkContext *ctx = client.GetContext();
	VkSwapchain *swap = client.GetSwapchain();
	Destroy(ctx);

	if (!ctx || !ctx->IsValid() || !swap || texSetLayout == VK_NULL_HANDLE || sampler == VK_NULL_HANDLE)
		return false;
	if (!texRelPath || !texRelPath[0])
		return false;

	std::string fullPath;
	if (!ResolveTexFile(client, texRelPath, fullPath)) {
		VkLogWarn("VkCSphereMgr: unable to locate texture archive '%s'", texRelPath);
		return false;
	}

	std::ifstream file(fullPath, std::ios::binary | std::ios::ate);
	if (!file.is_open()) {
		VkLogErr("VkCSphereMgr: failed to open '%s'", fullPath.c_str());
		return false;
	}

	const std::streamoff fileLen = file.tellg();
	if (fileLen <= 0) {
		VkLogWarn("VkCSphereMgr: empty archive '%s'", fullPath.c_str());
		return false;
	}

	std::vector<uint8_t> archive(static_cast<size_t>(fileLen));
	file.seekg(0, std::ios::beg);
	if (!file.read(reinterpret_cast<char *>(archive.data()), fileLen)) {
		VkLogErr("VkCSphereMgr: failed to read '%s'", fullPath.c_str());
		return false;
	}

	std::vector<DdsSpan> spans;
	size_t cursor = 0;
	while (cursor + 128 <= archive.size()) {
		size_t ddsSize = 0;
		if (!VkSurfNative::ComputeDdsByteSize(archive.data() + cursor, archive.size() - cursor, ddsSize) || ddsSize == 0)
			break;
		spans.push_back({ cursor, ddsSize });
		cursor += ddsSize;
	}

	if (spans.empty()) {
		VkLogErr("VkCSphereMgr: no valid DDS tiles inside '%s'", fullPath.c_str());
		return false;
	}

	static constexpr int kNpLevel5[] = { 4 };
	static constexpr int kNpLevel6[] = { 4, 8 };
	static constexpr int kNpLevel7[] = { 6, 12, 16, 16 };
	static constexpr int kNpLevel8[] = { 6, 12, 18, 24, 28, 30, 32, 32 };

	int level = 1;
	size_t startIdx = 0;
	size_t tileCount = 1;
	uint32_t nband = 0;
	const int *npTable = nullptr;

	if (spans.size() >= 501) {
		level = 8;
		startIdx = 137;
		tileCount = 364;
		nband = 8;
		npTable = kNpLevel8;
	} else if (spans.size() >= 137) {
		level = 7;
		startIdx = 37;
		tileCount = 100;
		nband = 4;
		npTable = kNpLevel7;
	} else if (spans.size() >= 37) {
		level = 6;
		startIdx = 13;
		tileCount = 24;
		nband = 2;
		npTable = kNpLevel6;
	} else if (spans.size() >= 13) {
		level = 5;
		startIdx = 5;
		tileCount = 8;
		nband = 1;
		npTable = kNpLevel5;
	} else if (spans.size() >= 5) {
		level = 4;
		startIdx = 3;
		tileCount = 2;
	} else {
		level = int(spans.size());
		startIdx = spans.size() - 1;
		tileCount = 1;
	}

	ownerCtx = ctx;
	loadedLevel = level;
	loadedPath = texRelPath;

	// Build shared spherical patch meshes for active resolution level.
	if (level >= 5) {
		bands.resize(nband);
		for (uint32_t b = 0; b < nband; ++b) {
			const double latMax = (kPi * 0.5) * double(nband - b) / double(nband);
			const double latMin = (kPi * 0.5) * double(nband - b - 1) / double(nband);
			const double dPhi = (2.0 * kPi) / double(npTable[b]);
			const uint32_t segLon = std::max(6u, 48u / uint32_t(npTable[b]));
			const uint32_t segLat = std::max(4u, 32u / nband);
			bands[b] = VkCreateSpherePatch(*ctx, latMin, latMax, dPhi, segLon, segLat, 1.0, true);
		}
	} else if (level == 4) {
		bands.resize(1);
		bands[0] = VkCreateSpherePatch(*ctx, -kPi * 0.5, kPi * 0.5, kPi, 24, 24, 1.0, true);
	} else {
		bands.resize(1);
		bands[0] = VkCreateSphere(*ctx, 32, 24, 1.0, true);
	}

	// Create dedicated descriptor pool for this archive's active tile count.
	VkDescriptorPoolSize poolSize{};
	poolSize.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	poolSize.descriptorCount = uint32_t(tileCount);

	VkDescriptorPoolCreateInfo dpci{};
	dpci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	dpci.maxSets       = uint32_t(tileCount);
	dpci.poolSizeCount = 1;
	dpci.pPoolSizes    = &poolSize;

	if (vkCreateDescriptorPool(ctx->Device(), &dpci, nullptr, &descPool) != VK_SUCCESS) {
		VkLogErr("VkCSphereMgr: vkCreateDescriptorPool failed");
		Destroy(ctx);
		return false;
	}

	std::vector<VkDescriptorSetLayout> setLayouts(tileCount, texSetLayout);
	std::vector<VkDescriptorSet> descSets(tileCount, VK_NULL_HANDLE);
	VkDescriptorSetAllocateInfo dsai{};
	dsai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	dsai.descriptorPool     = descPool;
	dsai.descriptorSetCount = uint32_t(tileCount);
	dsai.pSetLayouts        = setLayouts.data();

	if (vkAllocateDescriptorSets(ctx->Device(), &dsai, descSets.data()) != VK_SUCCESS) {
		VkLogErr("VkCSphereMgr: vkAllocateDescriptorSets failed (%zu)", tileCount);
		Destroy(ctx);
		return false;
	}

	auto attachTile = [&](size_t spanIdx, size_t slotIdx, uint32_t bandIdx, const oapi::FMATRIX4 &mPatch) -> bool {
		if (spanIdx >= spans.size())
			return false;
		const DdsSpan &sp = spans[spanIdx];
		char dbgName[160]{};
		std::snprintf(dbgName, sizeof(dbgName), "%s#%zu", texRelPath, spanIdx);
		VkSurfNative *surf = VkSurfNative::LoadFromDdsMemory(
			*ctx, *swap, archive.data() + sp.offset, sp.size, dbgName, 0, fullPath.c_str());
		if (!surf || surf->GetImageView() == VK_NULL_HANDLE) {
			if (surf && surf->DecRef())
				delete surf;
			return false;
		}

		VkDescriptorImageInfo dii{};
		dii.sampler     = sampler;
		dii.imageView   = surf->GetImageView();
		dii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkWriteDescriptorSet wds{};
		wds.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		wds.dstSet          = descSets[slotIdx];
		wds.dstBinding      = 0;
		wds.descriptorCount = 1;
		wds.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		wds.pImageInfo      = &dii;
		vkUpdateDescriptorSets(ctx->Device(), 1, &wds, 0, nullptr);

		Tile t;
		t.surf    = surf;
		t.descSet = descSets[slotIdx];
		t.bandIdx = bandIdx;
		t.mWorld  = VkMatMul(mPatch, galToEcl);

		const oapi::FVECTOR4 &b = bands[bandIdx].bnd;
		t.bndCenter = oapi::FVECTOR3(
			b.x * t.mWorld.m11 + b.y * t.mWorld.m21 + b.z * t.mWorld.m31,
			b.x * t.mWorld.m12 + b.y * t.mWorld.m22 + b.z * t.mWorld.m32,
			b.x * t.mWorld.m13 + b.y * t.mWorld.m23 + b.z * t.mWorld.m33);
		t.bndRadius = b.w;

		tiles.push_back(t);
		return true;
	};

	tiles.reserve(tileCount);
	size_t curSpan = startIdx;
	size_t curSlot = 0;

	if (level >= 5) {
		for (int hemi = 0; hemi < 2; ++hemi) {
			for (uint32_t band = 0; band < nband; ++band) {
				const int nlon = npTable[band];
				const double dPhi = (2.0 * kPi) / double(nlon);
				for (int i = 0; i < nlon; ++i) {
					const double lngC = -kPi + (double(i) + 0.5) * dPhi;
					oapi::FMATRIX4 mPatch;
					VkMatRotY(&mPatch, lngC);
					if (hemi == 1) {
						// Southern tiles in Pltex are rotated 180 deg (RotX(Pi) applied in row-vector form).
						mPatch.m12 = -mPatch.m12; mPatch.m13 = -mPatch.m13;
						mPatch.m22 = -mPatch.m22; mPatch.m23 = -mPatch.m23;
						mPatch.m32 = -mPatch.m32; mPatch.m33 = -mPatch.m33;
					}
					if (!attachTile(curSpan++, curSlot++, band, mPatch)) {
						VkLogErr("VkCSphereMgr: failed decoding tile %zu in '%s'", curSpan - 1, fullPath.c_str());
						Destroy(ctx);
						return false;
					}
				}
			}
		}
	} else if (level == 4) {
		for (int i = 0; i < 2; ++i) {
			oapi::FMATRIX4 mPatch;
			VkMatRotY(&mPatch, (i == 0) ? (-0.5 * kPi) : (0.5 * kPi));
			if (!attachTile(curSpan++, curSlot++, 0, mPatch)) {
				Destroy(ctx);
				return false;
			}
		}
	} else {
		if (!attachTile(curSpan, curSlot, 0, VkMatIdentity())) {
			Destroy(ctx);
			return false;
		}
	}

	VkLogInfo("VkCSphereMgr: loaded '%s' (level=%d, tiles=%zu)", texRelPath, loadedLevel, tiles.size());
	return true;
}

void VkCSphereMgr::Render(
	VkCommandBuffer cmd,
	VkPipelineLayout pipeLayout,
	const VkScene *scene,
	const oapi::FVECTOR3 &colorScale,
	float alpha) const
{
	if (tiles.empty() || !scene || pipeLayout == VK_NULL_HANDLE)
		return;

	const oapi::FMATRIX4 *mProjViewInf = scene->GetProjectionViewInfMatrix();
	uint32_t boundBand = UINT32_MAX;

	CelSpherePC pc{};
	pc.color[0] = colorScale.x;
	pc.color[1] = colorScale.y;
	pc.color[2] = colorScale.z;
	pc.color[3] = alpha;

	for (const Tile &t : tiles) {
		if (!scene->IsVisibleInCamera(&t.bndCenter, t.bndRadius))
			continue;

		const VBMESH &mesh = bands[t.bandIdx];
		if (mesh.vb.buffer == VK_NULL_HANDLE || mesh.ib.buffer == VK_NULL_HANDLE || mesh.ni == 0)
			continue;

		if (boundBand != t.bandIdx) {
			const VkDeviceSize offset = 0;
			vkCmdBindVertexBuffers(cmd, 0, 1, &mesh.vb.buffer, &offset);
			vkCmdBindIndexBuffer(cmd, mesh.ib.buffer, 0, VK_INDEX_TYPE_UINT16);
			boundBand = t.bandIdx;
		}

		pc.mWVP = VkMatMul(t.mWorld, *mProjViewInf);
		vkCmdPushConstants(
			cmd,
			pipeLayout,
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			0,
			sizeof(CelSpherePC),
			&pc);
		vkCmdBindDescriptorSets(
			cmd,
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			pipeLayout,
			0,
			1,
			&t.descSet,
			0,
			nullptr);
		vkCmdDrawIndexed(cmd, mesh.ni, 1, 0, 0, 0);
	}
}
