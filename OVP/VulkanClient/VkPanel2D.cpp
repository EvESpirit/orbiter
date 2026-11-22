// ==============================================================
// VkPanel2D.cpp
// Hardware 2D instrument panel, MFD & HUD mesh renderer
// Part of the ORBITER VISUALISATION PROJECT (OVP) - VulkanClient
// Licensed under the MIT License
// ==============================================================

#include "VkPanel2D.h"
#include "VulkanClient.h"
#include "VkContext.h"
#include "VkSwapchain.h"
#include "VkPad.h"
#include "VkSurface.h"
#include "VkLog.h"
#include "Shaders/Panel2DVertSpv.h"
#include "Shaders/Panel2DFragSpv.h"

#include <cstring>
#include <vector>

namespace {

struct VkPanel2DVtx {
	float x;
	float y;
	float u;
	float v;
};
static_assert(sizeof(VkPanel2DVtx) == 16, "VkPanel2DVtx must be 16 bytes");

struct PanelPushConstants {
	float scaleOffset[4];
	float alpha;
};
static_assert(sizeof(PanelPushConstants) == 20, "PanelPushConstants must be 20 bytes");

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

} // namespace

VkPanel2D::~VkPanel2D()
{
	Destroy();
}

bool VkPanel2D::Init(VkContext &context, VkSwapchain &swap, VkPadPipeline &padPipeline)
{
	Destroy();

	ctx = &context;
	swapchain = &swap;
	padPipe = &padPipeline;

	vertShader = CreateSpvModule(ctx->Device(), g_Panel2DVertSpv, sizeof(g_Panel2DVertSpv));
	fragShader = CreateSpvModule(ctx->Device(), g_Panel2DFragSpv, sizeof(g_Panel2DFragSpv));
	if (vertShader == VK_NULL_HANDLE || fragShader == VK_NULL_HANDLE) {
		VkLogErr("VkPanel2D: failed to create Panel2D SPIR-V shader modules");
		Destroy();
		return false;
	}

	VkDescriptorSetLayoutBinding binding{};
	binding.binding         = 0;
	binding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	binding.descriptorCount = 1;
	binding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutCreateInfo dslInfo{};
	dslInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	dslInfo.bindingCount = 1;
	dslInfo.pBindings    = &binding;

	if (vkCreateDescriptorSetLayout(ctx->Device(), &dslInfo, nullptr, &setLayout) != VK_SUCCESS) {
		VkLogErr("VkPanel2D: vkCreateDescriptorSetLayout failed");
		Destroy();
		return false;
	}

	VkPushConstantRange pcRange{};
	pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	pcRange.offset     = 0;
	pcRange.size       = sizeof(PanelPushConstants);

	VkPipelineLayoutCreateInfo plInfo{};
	plInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	plInfo.setLayoutCount         = 1;
	plInfo.pSetLayouts            = &setLayout;
	plInfo.pushConstantRangeCount = 1;
	plInfo.pPushConstantRanges    = &pcRange;

	if (vkCreatePipelineLayout(ctx->Device(), &plInfo, nullptr, &pipeLayout) != VK_SUCCESS) {
		VkLogErr("VkPanel2D: vkCreatePipelineLayout failed");
		Destroy();
		return false;
	}

	return true;
}

void VkPanel2D::Destroy()
{
	if (ctx && ctx->Device() != VK_NULL_HANDLE) {
		VkDevice dev = ctx->Device();
		vkDeviceWaitIdle(dev);

		for (auto &kv : pipelines) {
			if (kv.second != VK_NULL_HANDLE)
				vkDestroyPipeline(dev, kv.second, nullptr);
		}
		pipelines.clear();

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
	padPipe = nullptr;
}

VkPipeline VkPanel2D::GetOrCreatePipeline(VkFormat colorFormat, bool additive)
{
	if (!IsInitialized() || colorFormat == VK_FORMAT_UNDEFINED)
		return VK_NULL_HANDLE;

	const uint64_t key = (uint64_t(colorFormat) << 1) | (additive ? 1ULL : 0ULL);
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
	vtxBind.stride    = sizeof(VkPanel2DVtx);
	vtxBind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	VkVertexInputAttributeDescription vtxAttrs[2]{};
	vtxAttrs[0] = { 0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(VkPanel2DVtx, x) };
	vtxAttrs[1] = { 1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(VkPanel2DVtx, u) };

	VkPipelineVertexInputStateCreateInfo vi{};
	vi.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vi.vertexBindingDescriptionCount   = 1;
	vi.pVertexBindingDescriptions      = &vtxBind;
	vi.vertexAttributeDescriptionCount = 2;
	vi.pVertexAttributeDescriptions    = vtxAttrs;

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
	// DefaultPanel quads use alternating triangle winding {0,1,2, 3,2,1}
	rs.cullMode    = VK_CULL_MODE_NONE;
	rs.frontFace   = VK_FRONT_FACE_CLOCKWISE;
	rs.lineWidth   = 1.0f;

	VkPipelineMultisampleStateCreateInfo ms{};
	ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineDepthStencilStateCreateInfo ds{};
	ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	ds.depthTestEnable  = VK_FALSE;
	ds.depthWriteEnable = VK_FALSE;

	VkPipelineColorBlendAttachmentState cbAtt{};
	cbAtt.blendEnable         = VK_TRUE;
	cbAtt.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	cbAtt.dstColorBlendFactor = additive ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	cbAtt.colorBlendOp        = VK_BLEND_OP_ADD;
	cbAtt.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	cbAtt.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	cbAtt.alphaBlendOp        = VK_BLEND_OP_ADD;
	cbAtt.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

	VkPipelineColorBlendStateCreateInfo cb{};
	cb.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	cb.attachmentCount = 1;
	cb.pAttachments    = &cbAtt;

	const VkDynamicState dynStates[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dyn{};
	dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dyn.dynamicStateCount = 2;
	dyn.pDynamicStates    = dynStates;

	VkPipelineRenderingCreateInfo rendInfo{};
	rendInfo.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	rendInfo.colorAttachmentCount    = 1;
	rendInfo.pColorAttachmentFormats = &colorFormat;

	VkGraphicsPipelineCreateInfo pci{};
	pci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pci.pNext               = &rendInfo;
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
		VkLogErr("VkPanel2D: vkCreateGraphicsPipelines failed (fmt=%d, additive=%d)", (int)colorFormat, additive ? 1 : 0);
		return VK_NULL_HANDLE;
	}

	pipelines[key] = pipe;
	return pipe;
}

void VkPanel2D::Render(
	oapi::VulkanClient &client,
	SURFHANDLE *hSurf,
	MESHHANDLE hMesh,
	MATRIX3 *T,
	float alpha,
	bool additive,
	SURFHANDLE defaultTex)
{
	if (!IsInitialized() || !padPipe->IsInitialized() || !hMesh)
		return;
	if (!swapchain->IsValid() || !swapchain->IsFrameRecording())
		return;

	const VkExtent2D ext = swapchain->SceneExtent();
	if (ext.width == 0 || ext.height == 0)
		return;

	const DWORD ngrp = oapiMeshGroupCount(hMesh);
	if (ngrp == 0)
		return;

	padPipe->FlushActivePads();

	VkCommandBuffer cmd = swapchain->CurrentCommandBuffer();
	if (cmd == VK_NULL_HANDLE)
		return;

	const uint32_t slot = swapchain->ActiveSlotIndex();
	const uint64_t slotGen = swapchain->SlotGeneration(slot);
	padPipe->BeginSlotFrame(slot, slotGen);

	const VkSampler sampler = padPipe->SamplerForFilter(VkPadPipeline::FilterMode::Aniso);

	struct GroupDraw {
		VkPadPipeline::BufferSlice vtx;
		VkPadPipeline::BufferSlice idx;
		VkDescriptorSet            descSet = VK_NULL_HANDLE;
		uint32_t                   idxCount = 0;
	};
	std::vector<GroupDraw> draws;
	draws.reserve(ngrp);

	for (DWORD i = 0; i < ngrp; ++i) {
		const MESHGROUP *gr = oapiMeshGroup(hMesh, i);
		if (!gr || (gr->UsrFlag & 2) || !gr->Vtx || !gr->Idx || gr->nVtx == 0)
			continue;

		const uint32_t idxCount = (gr->nIdx / 3u) * 3u;
		if (idxCount == 0)
			continue;

		SURFHANDLE surf = nullptr;
		bool isMfdSlot = false;
		const DWORD texIdx = gr->TexIdx;
		if (texIdx >= TEXIDX_MFD0 && (texIdx - TEXIDX_MFD0) < MAXMFD) {
			isMfdSlot = true;
			surf = client.GetMFDSurface(int(texIdx - TEXIDX_MFD0));
			if (!surf)
				surf = defaultTex;
		} else if (texIdx != DWORD(-1)) {
			surf = hSurf ? hSurf[texIdx] : oapiGetTextureHandle(hMesh, texIdx + 1);
		} else if (!hSurf) {
			surf = oapiGetTextureHandle(hMesh, 0);
		}

		VkImageView texView = VK_NULL_HANDLE;
		if (surf) {
			auto *nat = reinterpret_cast<VkSurfNative *>(surf);
			if (client.IsValidSurface(nat) && nat->GetImageView() != VK_NULL_HANDLE) {
				nat->TransitionToShaderRead(cmd);
				texView = nat->GetImageView();
			}
		}
		if (texView == VK_NULL_HANDLE) {
			if (isMfdSlot)
				texView = padPipe->BlackView();
			else
				continue;
		}

		VkPadPipeline::BufferSlice vtxSlice = padPipe->AllocVertices(slot, VkDeviceSize(gr->nVtx) * sizeof(VkPanel2DVtx));
		VkPadPipeline::BufferSlice idxSlice = padPipe->AllocIndices(slot, VkDeviceSize(idxCount) * sizeof(uint16_t));
		VkDescriptorSet dset = padPipe->AllocateDescriptorSet(slot, setLayout);
		if (!vtxSlice.mapped || !idxSlice.mapped || dset == VK_NULL_HANDLE)
			return;

		// Copy into transient host-visible staging without mutating Core's MESHGROUP vertices
		auto *dstVtx = static_cast<VkPanel2DVtx *>(vtxSlice.mapped);
		for (DWORD k = 0; k < gr->nVtx; ++k) {
			dstVtx[k].x = gr->Vtx[k].x;
			dstVtx[k].y = gr->Vtx[k].y;
			dstVtx[k].u = gr->Vtx[k].tu;
			dstVtx[k].v = gr->Vtx[k].tv;
		}
		std::memcpy(idxSlice.mapped, gr->Idx, size_t(idxCount) * sizeof(uint16_t));

		VkDescriptorImageInfo imgInfo{};
		imgInfo.sampler     = sampler;
		imgInfo.imageView   = texView;
		imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkWriteDescriptorSet write{};
		write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet          = dset;
		write.dstBinding      = 0;
		write.descriptorCount = 1;
		write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write.pImageInfo      = &imgInfo;
		vkUpdateDescriptorSets(ctx->Device(), 1, &write, 0, nullptr);

		draws.push_back({ vtxSlice, idxSlice, dset, idxCount });
	}

	if (draws.empty())
		return;

	const VkPipeline pipe = GetOrCreatePipeline(swapchain->SceneFormat(), additive);
	if (pipe == VK_NULL_HANDLE)
		return;

	const float vw = float(ext.width);
	const float vh = float(ext.height);
	// Core's Defpanel/HUD pass {1,-0.5,0, 0,1,-0.5, 0,0,1}; read only m11, m13, m22, m23 like D3D9Client.
	// Add +0.5 px offset so Panel2D's x0-0.5 / Defpanel's -0.5 align with Vulkan pixel centers at +0.5.
	const float m11 = (T && T->m11 != 0.0) ? float(T->m11) : 1.0f;
	const float m22 = (T && T->m22 != 0.0) ? float(T->m22) : 1.0f;
	const float dx  = (T ? float(T->m13) : 0.0f) + 0.5f;
	const float dy  = (T ? float(T->m23) : 0.0f) + 0.5f;

	PanelPushConstants pc{};
	pc.scaleOffset[0] =  2.0f * m11 / vw;
	pc.scaleOffset[1] =  2.0f * dx  / vw - 1.0f;
	pc.scaleOffset[2] = -2.0f * m22 / vh;
	pc.scaleOffset[3] =  1.0f - 2.0f * dy / vh;
	pc.alpha          = alpha;

	swapchain->TransitionSceneLayout(
		cmd,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
		VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
		VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

	VkRenderingAttachmentInfo colorAtt{};
	colorAtt.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	colorAtt.imageView   = swapchain->SceneImageView();
	colorAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	colorAtt.loadOp      = VK_ATTACHMENT_LOAD_OP_LOAD;
	colorAtt.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;

	VkRenderingInfo renderInfo{};
	renderInfo.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO;
	renderInfo.renderArea           = { { 0, 0 }, ext };
	renderInfo.layerCount           = 1;
	renderInfo.colorAttachmentCount = 1;
	renderInfo.pColorAttachments    = &colorAtt;

	vkCmdBeginRendering(cmd, &renderInfo);

	VkViewport vp{};
	vp.x        = 0.0f;
	vp.y        = vh;
	vp.width    = vw;
	vp.height   = -vh;
	vp.minDepth = 0.0f;
	vp.maxDepth = 1.0f;
	vkCmdSetViewport(cmd, 0, 1, &vp);

	const VkRect2D scissor = { { 0, 0 }, ext };
	vkCmdSetScissor(cmd, 0, 1, &scissor);

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
	vkCmdPushConstants(cmd, pipeLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);

	for (const GroupDraw &g : draws) {
		vkCmdBindVertexBuffers(cmd, 0, 1, &g.vtx.buffer, &g.vtx.offset);
		vkCmdBindIndexBuffer(cmd, g.idx.buffer, g.idx.offset, VK_INDEX_TYPE_UINT16);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeLayout, 0, 1, &g.descSet, 0, nullptr);
		vkCmdDrawIndexed(cmd, g.idxCount, 1, 0, 0, 0);
	}

	vkCmdEndRendering(cmd);
}
