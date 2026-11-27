// ==============================================================
// VkSpherePatch.cpp
// Tessellated spherical patch geometry & static GPU buffer helpers
// Part of the ORBITER VISUALISATION PROJECT (OVP) - VulkanClient
// Licensed under the MIT License
// ==============================================================

#include "VkSpherePatch.h"
#include "VkContext.h"
#include "VkLog.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
constexpr double kPi = 3.14159265358979323846;
}

bool VkGpuBuffer::UploadStatic(VkContext &ctx, const void *data, VkDeviceSize byteSize, VkBufferUsageFlags usage)
{
	Destroy(ctx);

	if (!data || byteSize == 0 || !ctx.IsValid())
		return false;

	VkBufferCreateInfo dstBci{};
	dstBci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	dstBci.size        = byteSize;
	dstBci.usage       = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	dstBci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VmaAllocationCreateInfo dstAci{};
	dstAci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

	if (vmaCreateBuffer(ctx.Allocator(), &dstBci, &dstAci, &buffer, &alloc, nullptr) != VK_SUCCESS) {
		VkLogErr("VkGpuBuffer: vmaCreateBuffer(device) failed (bytes=%llu)", (unsigned long long)byteSize);
		buffer = VK_NULL_HANDLE;
		alloc = VK_NULL_HANDLE;
		return false;
	}

	VkBuffer stagingBuf = VK_NULL_HANDLE;
	VmaAllocation stagingAlloc = VK_NULL_HANDLE;
	VmaAllocationInfo stagingInfo{};

	VkBufferCreateInfo srcBci{};
	srcBci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	srcBci.size        = byteSize;
	srcBci.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	srcBci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VmaAllocationCreateInfo srcAci{};
	srcAci.usage = VMA_MEMORY_USAGE_AUTO;
	srcAci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

	if (vmaCreateBuffer(ctx.Allocator(), &srcBci, &srcAci, &stagingBuf, &stagingAlloc, &stagingInfo) != VK_SUCCESS) {
		VkLogErr("VkGpuBuffer: vmaCreateBuffer(staging) failed (bytes=%llu)", (unsigned long long)byteSize);
		Destroy(ctx);
		return false;
	}

	if (!stagingInfo.pMappedData) {
		void *mapped = nullptr;
		if (vmaMapMemory(ctx.Allocator(), stagingAlloc, &mapped) != VK_SUCCESS || !mapped) {
			vmaDestroyBuffer(ctx.Allocator(), stagingBuf, stagingAlloc);
			Destroy(ctx);
			return false;
		}
		std::memcpy(mapped, data, size_t(byteSize));
		vmaUnmapMemory(ctx.Allocator(), stagingAlloc);
	} else {
		std::memcpy(stagingInfo.pMappedData, data, size_t(byteSize));
	}
	vmaFlushAllocation(ctx.Allocator(), stagingAlloc, 0, VK_WHOLE_SIZE);

	VkCommandPool pool = VK_NULL_HANDLE;
	VkCommandPoolCreateInfo cpci{};
	cpci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	cpci.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
	cpci.queueFamilyIndex = ctx.GraphicsQueueFamily();

	if (vkCreateCommandPool(ctx.Device(), &cpci, nullptr, &pool) != VK_SUCCESS) {
		vmaDestroyBuffer(ctx.Allocator(), stagingBuf, stagingAlloc);
		Destroy(ctx);
		return false;
	}

	VkCommandBuffer cmd = VK_NULL_HANDLE;
	VkCommandBufferAllocateInfo cbai{};
	cbai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	cbai.commandPool        = pool;
	cbai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cbai.commandBufferCount = 1;

	VkFence fence = VK_NULL_HANDLE;
	VkFenceCreateInfo fci{};
	fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

	bool ok = false;
	if (vkAllocateCommandBuffers(ctx.Device(), &cbai, &cmd) == VK_SUCCESS &&
	    vkCreateFence(ctx.Device(), &fci, nullptr, &fence) == VK_SUCCESS)
	{
		VkCommandBufferBeginInfo cbbi{};
		cbbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

		if (vkBeginCommandBuffer(cmd, &cbbi) == VK_SUCCESS) {
			VkBufferCopy copy{};
			copy.size = byteSize;
			vkCmdCopyBuffer(cmd, stagingBuf, buffer, 1, &copy);

			VkBufferMemoryBarrier2 barrier{};
			barrier.sType         = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
			barrier.srcStageMask  = VK_PIPELINE_STAGE_2_COPY_BIT;
			barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
			barrier.dstStageMask  = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT;
			barrier.dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_2_INDEX_READ_BIT;
			barrier.buffer        = buffer;
			barrier.offset        = 0;
			barrier.size          = byteSize;

			VkDependencyInfo dep{};
			dep.sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
			dep.bufferMemoryBarrierCount = 1;
			dep.pBufferMemoryBarriers    = &barrier;
			vkCmdPipelineBarrier2(cmd, &dep);

			if (vkEndCommandBuffer(cmd) == VK_SUCCESS) {
				VkCommandBufferSubmitInfo cbsi{};
				cbsi.sType         = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
				cbsi.commandBuffer = cmd;

				VkSubmitInfo2 si{};
				si.sType                  = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
				si.commandBufferInfoCount = 1;
				si.pCommandBufferInfos    = &cbsi;

				if (vkQueueSubmit2(ctx.GraphicsQueue(), 1, &si, fence) == VK_SUCCESS) {
					if (vkWaitForFences(ctx.Device(), 1, &fence, VK_TRUE, UINT64_MAX) == VK_SUCCESS)
						ok = true;
				}
			}
		}
	}

	if (fence != VK_NULL_HANDLE)
		vkDestroyFence(ctx.Device(), fence, nullptr);
	vkDestroyCommandPool(ctx.Device(), pool, nullptr);
	vmaDestroyBuffer(ctx.Allocator(), stagingBuf, stagingAlloc);

	if (!ok) {
		VkLogErr("VkGpuBuffer: transfer submit failed (bytes=%llu)", (unsigned long long)byteSize);
		Destroy(ctx);
		return false;
	}

	size = byteSize;
	return true;
}

void VkGpuBuffer::Destroy(VkContext &ctx)
{
	if (buffer != VK_NULL_HANDLE && ctx.Allocator() != VK_NULL_HANDLE)
		vmaDestroyBuffer(ctx.Allocator(), buffer, alloc);
	buffer = VK_NULL_HANDLE;
	alloc = VK_NULL_HANDLE;
	size = 0;
}

VkShaderModule VkCreateShaderModuleFromBytes(VkDevice device, const void *spvBytes, size_t byteSize)
{
	if (device == VK_NULL_HANDLE || !spvBytes || byteSize == 0)
		return VK_NULL_HANDLE;

	VkShaderModuleCreateInfo smci{};
	smci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	smci.codeSize = byteSize;
	smci.pCode    = reinterpret_cast<const uint32_t *>(spvBytes);

	VkShaderModule mod = VK_NULL_HANDLE;
	if (vkCreateShaderModule(device, &smci, nullptr, &mod) != VK_SUCCESS)
		return VK_NULL_HANDLE;
	return mod;
}

bool VBMESH::Upload(VkContext &ctx)
{
	if (vtx.empty() || idx.empty())
		return false;

	nv = uint32_t(vtx.size());
	ni = uint32_t(idx.size());

	if (!vb.UploadStatic(ctx, vtx.data(), VkDeviceSize(nv) * sizeof(VkVertex2Tex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT))
		return false;
	if (!ib.UploadStatic(ctx, idx.data(), VkDeviceSize(ni) * sizeof(uint16_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT)) {
		vb.Destroy(ctx);
		return false;
	}
	return true;
}

void VBMESH::Destroy(VkContext &ctx)
{
	vb.Destroy(ctx);
	ib.Destroy(ctx);
	nv = 0;
	ni = 0;
	ClearCpu();
}

void VBMESH::ClearCpu()
{
	std::vector<VkVertex2Tex>().swap(vtx);
	std::vector<uint16_t>().swap(idx);
}

static VBMESH BuildGridPatch(
	VkContext &ctx,
	double phiMin,
	double phiMax,
	double latMin,
	double latMax,
	uint32_t segLon,
	uint32_t segLat,
	double radius,
	bool inwardWinding,
	bool fullSphere)
{
	VBMESH mesh;
	const uint32_t cols = segLon + 1;
	const uint32_t rows = segLat + 1;
	mesh.vtx.resize(size_t(cols) * rows);
	mesh.idx.reserve(size_t(segLon) * segLat * 6);

	double cx = 0.0, cy = 0.0, cz = 0.0;
	for (uint32_t iy = 0; iy < rows; ++iy) {
		const double v = double(iy) / double(segLat);
		const double lat = latMax - v * (latMax - latMin);
		const double cosLat = std::cos(lat);
		const double sinLat = std::sin(lat);

		for (uint32_t ix = 0; ix < cols; ++ix) {
			const double u = double(ix) / double(segLon);
			const double phi = phiMin + u * (phiMax - phiMin);
			const double cosPhi = std::cos(phi);
			const double sinPhi = std::sin(phi);

			const float nx = float(cosLat * cosPhi);
			const float ny = float(sinLat);
			const float nz = float(cosLat * sinPhi);

			VkVertex2Tex &vert = mesh.vtx[size_t(iy) * cols + ix];
			vert.x   = nx * float(radius);
			vert.y   = ny * float(radius);
			vert.z   = nz * float(radius);
			vert.nx  = inwardWinding ? -nx : nx;
			vert.ny  = inwardWinding ? -ny : ny;
			vert.nz  = inwardWinding ? -nz : nz;
			vert.tu0 = float(u);
			vert.tv0 = float(v);
			vert.tu1 = float(u);
			vert.tv1 = float(v);

			cx += vert.x;
			cy += vert.y;
			cz += vert.z;
		}
	}

	if (fullSphere) {
		mesh.bnd = oapi::FVECTOR4(0.0f, 0.0f, 0.0f, float(radius));
	} else {
		const double invCount = 1.0 / double(mesh.vtx.size());
		cx *= invCount;
		cy *= invCount;
		cz *= invCount;
		float maxDistSq = 0.0f;
		for (const VkVertex2Tex &vert : mesh.vtx) {
			const float dx = vert.x - float(cx);
			const float dy = vert.y - float(cy);
			const float dz = vert.z - float(cz);
			maxDistSq = std::max(maxDistSq, dx * dx + dy * dy + dz * dz);
		}
		mesh.bnd = oapi::FVECTOR4(float(cx), float(cy), float(cz), std::sqrtf(maxDistSq) + 1e-4f);
	}

	for (uint32_t iy = 0; iy < segLat; ++iy) {
		for (uint32_t ix = 0; ix < segLon; ++ix) {
			const uint16_t i00 = uint16_t(iy * cols + ix);
			const uint16_t i10 = uint16_t(iy * cols + (ix + 1));
			const uint16_t i01 = uint16_t((iy + 1) * cols + ix);
			const uint16_t i11 = uint16_t((iy + 1) * cols + (ix + 1));

			if (inwardWinding) {
				// Clockwise when viewed from origin inside sphere:
				// TopLeft (i10) -> TopRight (i00) -> BottomRight (i01)
				mesh.idx.push_back(i10);
				mesh.idx.push_back(i00);
				mesh.idx.push_back(i01);
				mesh.idx.push_back(i10);
				mesh.idx.push_back(i01);
				mesh.idx.push_back(i11);
			} else {
				// Clockwise when viewed from outside sphere:
				mesh.idx.push_back(i00);
				mesh.idx.push_back(i10);
				mesh.idx.push_back(i11);
				mesh.idx.push_back(i00);
				mesh.idx.push_back(i11);
				mesh.idx.push_back(i01);
			}
		}
	}

	mesh.Upload(ctx);
	mesh.ClearCpu();
	return mesh;
}

VBMESH VkCreateSphere(VkContext &ctx, uint32_t segLon, uint32_t segLat, double radius, bool inwardWinding)
{
	return BuildGridPatch(
		ctx,
		-kPi,
		kPi,
		-kPi * 0.5,
		kPi * 0.5,
		segLon,
		segLat,
		radius,
		inwardWinding,
		true);
}

VBMESH VkCreateSpherePatch(
	VkContext &ctx,
	double latMin,
	double latMax,
	double dPhi,
	uint32_t segLon,
	uint32_t segLat,
	double radius,
	bool inwardWinding)
{
	return BuildGridPatch(
		ctx,
		-0.5 * dPhi,
		0.5 * dPhi,
		latMin,
		latMax,
		segLon,
		segLat,
		radius,
		inwardWinding,
		false);
}
