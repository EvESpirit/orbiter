// ==============================================================
// VkBlit.cpp
// Shader-driven 2D textured, scaled & color-keyed blit pipeline
// Part of the ORBITER VISUALISATION PROJECT (OVP) - VulkanClient
// Licensed under the MIT License
// ==============================================================

#include "VkBlit.h"
#include "VkContext.h"
#include "VkLog.h"
#include "Shaders/BlitVertSpv.h"
#include "Shaders/BlitFragSpv.h"
#include <algorithm>

namespace {

constexpr uint32_t kSetsPerPool = 512;

struct BlitPushConstants {
	float dstRectNdc[4];
	float uvRect[4];
	float colorKey[4];
};
static_assert(sizeof(BlitPushConstants) == 48, "BlitPushConstants must match GLSL push_constant layout");

} // namespace

VkBlitPipeline::~VkBlitPipeline()
{
	Destroy();
}

bool VkBlitPipeline::Create(VkContext &context)
{
	Destroy();
	ctx = &context;
	VkDevice dev = ctx->Device();

	VkShaderModuleCreateInfo smci{};
	smci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	smci.codeSize = sizeof(g_BlitVertSpv);
	smci.pCode    = g_BlitVertSpv;
	if (vkCreateShaderModule(dev, &smci, nullptr, &vertModule) != VK_SUCCESS) {
		VkLogErr("VkBlitPipeline: vkCreateShaderModule (vert) failed");
		Destroy();
		return false;
	}

	smci.codeSize = sizeof(g_BlitFragSpv);
	smci.pCode    = g_BlitFragSpv;
	if (vkCreateShaderModule(dev, &smci, nullptr, &fragModule) != VK_SUCCESS) {
		VkLogErr("VkBlitPipeline: vkCreateShaderModule (frag) failed");
		Destroy();
		return false;
	}

	VkSamplerCreateInfo sampInfo{};
	sampInfo.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	sampInfo.magFilter    = VK_FILTER_NEAREST;
	sampInfo.minFilter    = VK_FILTER_NEAREST;
	sampInfo.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	sampInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampInfo.minLod       = 0.0f;
	sampInfo.maxLod       = 0.0f;
	if (vkCreateSampler(dev, &sampInfo, nullptr, &nearestSampler) != VK_SUCCESS) {
		VkLogErr("VkBlitPipeline: vkCreateSampler (nearest) failed");
		Destroy();
		return false;
	}

	sampInfo.magFilter = VK_FILTER_LINEAR;
	sampInfo.minFilter = VK_FILTER_LINEAR;
	if (vkCreateSampler(dev, &sampInfo, nullptr, &linearSampler) != VK_SUCCESS) {
		VkLogErr("VkBlitPipeline: vkCreateSampler (linear) failed");
		Destroy();
		return false;
	}

	VkDescriptorSetLayoutBinding texBind{};
	texBind.binding         = 0;
	texBind.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	texBind.descriptorCount = 1;
	texBind.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutCreateInfo dslci{};
	dslci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	dslci.bindingCount = 1;
	dslci.pBindings    = &texBind;
	if (vkCreateDescriptorSetLayout(dev, &dslci, nullptr, &descSetLayout) != VK_SUCCESS) {
		VkLogErr("VkBlitPipeline: vkCreateDescriptorSetLayout failed");
		Destroy();
		return false;
	}

	VkPushConstantRange pcr{};
	pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	pcr.offset     = 0;
	pcr.size       = sizeof(BlitPushConstants);

	VkPipelineLayoutCreateInfo plci{};
	plci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	plci.setLayoutCount         = 1;
	plci.pSetLayouts            = &descSetLayout;
	plci.pushConstantRangeCount = 1;
	plci.pPushConstantRanges    = &pcr;
	if (vkCreatePipelineLayout(dev, &plci, nullptr, &pipelineLayout) != VK_SUCCESS) {
		VkLogErr("VkBlitPipeline: vkCreatePipelineLayout failed");
		Destroy();
		return false;
	}

	if (GetOrCreatePipeline(VK_FORMAT_B8G8R8A8_UNORM) == VK_NULL_HANDLE) {
		Destroy();
		return false;
	}

	return true;
}

void VkBlitPipeline::Destroy()
{
	if (ctx && ctx->Device() != VK_NULL_HANDLE) {
		VkDevice dev = ctx->Device();
		for (auto &kv : pipelinesByFormat) {
			if (kv.second != VK_NULL_HANDLE)
				vkDestroyPipeline(dev, kv.second, nullptr);
		}
		pipelinesByFormat.clear();

		for (uint32_t s = 0; s < kNumSlots; ++s) {
			for (VkDescriptorPool p : slotPools[s].pools) {
				if (p != VK_NULL_HANDLE)
					vkDestroyDescriptorPool(dev, p, nullptr);
			}
			slotPools[s] = SlotPools{};
		}

		if (pipelineLayout != VK_NULL_HANDLE) {
			vkDestroyPipelineLayout(dev, pipelineLayout, nullptr);
			pipelineLayout = VK_NULL_HANDLE;
		}
		if (descSetLayout != VK_NULL_HANDLE) {
			vkDestroyDescriptorSetLayout(dev, descSetLayout, nullptr);
			descSetLayout = VK_NULL_HANDLE;
		}
		if (linearSampler != VK_NULL_HANDLE) {
			vkDestroySampler(dev, linearSampler, nullptr);
			linearSampler = VK_NULL_HANDLE;
		}
		if (nearestSampler != VK_NULL_HANDLE) {
			vkDestroySampler(dev, nearestSampler, nullptr);
			nearestSampler = VK_NULL_HANDLE;
		}
		if (fragModule != VK_NULL_HANDLE) {
			vkDestroyShaderModule(dev, fragModule, nullptr);
			fragModule = VK_NULL_HANDLE;
		}
		if (vertModule != VK_NULL_HANDLE) {
			vkDestroyShaderModule(dev, vertModule, nullptr);
			vertModule = VK_NULL_HANDLE;
		}
	}
	ctx = nullptr;
}

VkPipeline VkBlitPipeline::GetOrCreatePipeline(VkFormat dstFormat)
{
	auto it = pipelinesByFormat.find((uint32_t)dstFormat);
	if (it != pipelinesByFormat.end())
		return it->second;

	VkPipelineShaderStageCreateInfo stages[2]{};
	stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = vertModule;
	stages[0].pName  = "main";

	stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = fragModule;
	stages[1].pName  = "main";

	VkPipelineVertexInputStateCreateInfo vi{};
	vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

	VkPipelineInputAssemblyStateCreateInfo ia{};
	ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

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

	VkPipelineColorBlendAttachmentState cba{};
	cba.blendEnable    = VK_FALSE;
	cba.colorWriteMask =
		VK_COLOR_COMPONENT_R_BIT |
		VK_COLOR_COMPONENT_G_BIT |
		VK_COLOR_COMPONENT_B_BIT |
		VK_COLOR_COMPONENT_A_BIT;

	VkPipelineColorBlendStateCreateInfo cb{};
	cb.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	cb.attachmentCount = 1;
	cb.pAttachments    = &cba;

	const VkDynamicState dynStates[2] = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR
	};
	VkPipelineDynamicStateCreateInfo dyn{};
	dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dyn.dynamicStateCount = 2;
	dyn.pDynamicStates    = dynStates;

	VkPipelineRenderingCreateInfo prci{};
	prci.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	prci.colorAttachmentCount    = 1;
	prci.pColorAttachmentFormats = &dstFormat;

	VkGraphicsPipelineCreateInfo gpci{};
	gpci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	gpci.pNext               = &prci;
	gpci.stageCount          = 2;
	gpci.pStages             = stages;
	gpci.pVertexInputState   = &vi;
	gpci.pInputAssemblyState = &ia;
	gpci.pViewportState      = &vp;
	gpci.pRasterizationState = &rs;
	gpci.pMultisampleState   = &ms;
	gpci.pColorBlendState    = &cb;
	gpci.pDynamicState       = &dyn;
	gpci.layout              = pipelineLayout;

	VkPipeline pipe = VK_NULL_HANDLE;
	VkResult vr = vkCreateGraphicsPipelines(ctx->Device(), VK_NULL_HANDLE, 1, &gpci, nullptr, &pipe);
	if (vr != VK_SUCCESS || pipe == VK_NULL_HANDLE) {
		VkLogErr("VkBlitPipeline: vkCreateGraphicsPipelines (fmt=%d) failed (VkResult=%d)", (int)dstFormat, (int)vr);
		return VK_NULL_HANDLE;
	}

	pipelinesByFormat[(uint32_t)dstFormat] = pipe;
	return pipe;
}

VkDescriptorSet VkBlitPipeline::AllocateDescriptorSet(uint32_t slot, uint64_t slotGen)
{
	if (!ctx || slot >= kNumSlots)
		return VK_NULL_HANDLE;

	VkDevice dev = ctx->Device();
	SlotPools &sp = slotPools[slot];

	if (sp.lastGeneration != slotGen) {
		for (VkDescriptorPool p : sp.pools)
			vkResetDescriptorPool(dev, p, 0);
		sp.activePoolIdx = 0;
		sp.usedInActivePool = 0;
		sp.lastGeneration = slotGen;
	}

	if (sp.pools.empty() || sp.usedInActivePool >= kSetsPerPool) {
		if (!sp.pools.empty() && sp.activePoolIdx + 1 < sp.pools.size()) {
			++sp.activePoolIdx;
			sp.usedInActivePool = 0;
		} else {
			VkDescriptorPoolSize poolSize{};
			poolSize.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			poolSize.descriptorCount = kSetsPerPool;

			VkDescriptorPoolCreateInfo dpci{};
			dpci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			dpci.maxSets       = kSetsPerPool;
			dpci.poolSizeCount = 1;
			dpci.pPoolSizes    = &poolSize;

			VkDescriptorPool newPool = VK_NULL_HANDLE;
			if (vkCreateDescriptorPool(dev, &dpci, nullptr, &newPool) != VK_SUCCESS) {
				VkLogErr("VkBlitPipeline: vkCreateDescriptorPool failed");
				return VK_NULL_HANDLE;
			}
			sp.pools.push_back(newPool);
			sp.activePoolIdx = sp.pools.size() - 1;
			sp.usedInActivePool = 0;
		}
	}

	VkDescriptorSetAllocateInfo dsai{};
	dsai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	dsai.descriptorPool     = sp.pools[sp.activePoolIdx];
	dsai.descriptorSetCount = 1;
	dsai.pSetLayouts        = &descSetLayout;

	VkDescriptorSet ds = VK_NULL_HANDLE;
	if (vkAllocateDescriptorSets(dev, &dsai, &ds) != VK_SUCCESS)
		return VK_NULL_HANDLE;

	++sp.usedInActivePool;
	return ds;
}

bool VkBlitPipeline::Blit(
	VkCommandBuffer cmd,
	uint32_t slot,
	uint64_t slotGen,
	VkImageView srcView,
	uint32_t srcW,
	uint32_t srcH,
	VkImageView dstView,
	VkFormat dstFormat,
	uint32_t dstW,
	uint32_t dstH,
	const RECT &srcRect,
	const RECT &dstRect,
	bool useColorKey,
	DWORD colorKey,
	bool linearFilter)
{
	if (!ctx || cmd == VK_NULL_HANDLE || srcView == VK_NULL_HANDLE || dstView == VK_NULL_HANDLE ||
		srcW == 0 || srcH == 0 || dstW == 0 || dstH == 0)
	{
		return false;
	}

	const int32_t clipL = std::max<int32_t>(0, dstRect.left);
	const int32_t clipT = std::max<int32_t>(0, dstRect.top);
	const int32_t clipR = std::min<int32_t>((int32_t)dstW, dstRect.right);
	const int32_t clipB = std::min<int32_t>((int32_t)dstH, dstRect.bottom);
	if (clipR <= clipL || clipB <= clipT)
		return true;

	VkPipeline pipe = GetOrCreatePipeline(dstFormat);
	if (pipe == VK_NULL_HANDLE)
		return false;

	VkDescriptorSet ds = AllocateDescriptorSet(slot, slotGen);
	if (ds == VK_NULL_HANDLE)
		return false;

	VkDescriptorImageInfo dii{};
	dii.sampler     = linearFilter ? linearSampler : nearestSampler;
	dii.imageView   = srcView;
	dii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkWriteDescriptorSet wds{};
	wds.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	wds.dstSet          = ds;
	wds.dstBinding      = 0;
	wds.descriptorCount = 1;
	wds.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	wds.pImageInfo      = &dii;
	vkUpdateDescriptorSets(ctx->Device(), 1, &wds, 0, nullptr);

	BlitPushConstants pc{};
	pc.dstRectNdc[0] = (2.0f * (float)dstRect.left / (float)dstW) - 1.0f;
	pc.dstRectNdc[1] = (2.0f * (float)dstRect.top / (float)dstH) - 1.0f;
	pc.dstRectNdc[2] = (2.0f * (float)dstRect.right / (float)dstW) - 1.0f;
	pc.dstRectNdc[3] = (2.0f * (float)dstRect.bottom / (float)dstH) - 1.0f;

	pc.uvRect[0] = (float)srcRect.left / (float)srcW;
	pc.uvRect[1] = (float)srcRect.top / (float)srcH;
	pc.uvRect[2] = (float)srcRect.right / (float)srcW;
	pc.uvRect[3] = (float)srcRect.bottom / (float)srcH;

	if (useColorKey) {
		pc.colorKey[0] = (float)((colorKey >> 16) & 0xFFu) / 255.0f;
		pc.colorKey[1] = (float)((colorKey >> 8) & 0xFFu) / 255.0f;
		pc.colorKey[2] = (float)(colorKey & 0xFFu) / 255.0f;
		pc.colorKey[3] = 0.01f;
	} else {
		pc.colorKey[0] = 0.0f;
		pc.colorKey[1] = 0.0f;
		pc.colorKey[2] = 0.0f;
		pc.colorKey[3] = -1.0f;
	}

	VkRenderingAttachmentInfo colorAtt{};
	colorAtt.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	colorAtt.imageView   = dstView;
	colorAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	colorAtt.loadOp      = VK_ATTACHMENT_LOAD_OP_LOAD;
	colorAtt.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;

	VkRenderingInfo ri{};
	ri.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO;
	ri.renderArea.offset    = { clipL, clipT };
	ri.renderArea.extent    = { (uint32_t)(clipR - clipL), (uint32_t)(clipB - clipT) };
	ri.layerCount           = 1;
	ri.colorAttachmentCount = 1;
	ri.pColorAttachments    = &colorAtt;

	vkCmdBeginRendering(cmd, &ri);

	VkViewport vp{};
	vp.x        = 0.0f;
	vp.y        = 0.0f;
	vp.width    = (float)dstW;
	vp.height   = (float)dstH;
	vp.minDepth = 0.0f;
	vp.maxDepth = 1.0f;
	vkCmdSetViewport(cmd, 0, 1, &vp);

	VkRect2D scissor{};
	scissor.offset = { clipL, clipT };
	scissor.extent = { (uint32_t)(clipR - clipL), (uint32_t)(clipB - clipT) };
	vkCmdSetScissor(cmd, 0, 1, &scissor);

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &ds, 0, nullptr);
	vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
	vkCmdDraw(cmd, 6, 1, 0, 0);

	vkCmdEndRendering(cmd);
	return true;
}
