// ==============================================================
// VkCelSphere.cpp
// Celestial sphere renderer (skybox, stars, constellations, grids & markers)
// Part of the ORBITER VISUALISATION PROJECT (OVP) - VulkanClient
// Licensed under the MIT License
// ==============================================================

#include "VkCelSphere.h"
#include "VulkanClient.h"
#include "VkContext.h"
#include "VkSwapchain.h"
#include "VkPad.h"
#include "VkScene.h"
#include "VkSurface.h"
#include "VkVertex.h"
#include "VkLog.h"
#include "Shaders/SceneLineVertSpv.h"
#include "Shaders/SceneLineFragSpv.h"
#include "Shaders/SceneStarVertSpv.h"
#include "Shaders/SceneStarFragSpv.h"
#include "Shaders/SceneLabelVertSpv.h"
#include "Shaders/SceneLabelFragSpv.h"
#include "Shaders/CelSphereVertSpv.h"
#include "Shaders/CelSphereFragSpv.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace {
constexpr double kPi = 3.14159265358979323846;

struct alignas(16) SceneLinePC {
	oapi::FMATRIX4 mWVP;
	float color[4];
	float params[4];
};
static_assert(sizeof(SceneLinePC) == 96, "SceneLine push constant must be 96 bytes");

struct alignas(16) SceneStarPC {
	oapi::FMATRIX4 mWVP;
	float skyColor[4];
};
static_assert(sizeof(SceneStarPC) == 80, "SceneStar push constant must be 80 bytes");

struct alignas(16) SceneLabelPC {
	oapi::FMATRIX4 mWVP;
	float color[4];
};
static_assert(sizeof(SceneLabelPC) == 80, "SceneLabel push constant must be 80 bytes");

static inline uint32_t PackBgraColor(float r, float g, float b, float a)
{
	const uint32_t cr = uint32_t(std::clamp(r, 0.0f, 1.0f) * 255.0f + 0.5f);
	const uint32_t cg = uint32_t(std::clamp(g, 0.0f, 1.0f) * 255.0f + 0.5f);
	const uint32_t cb = uint32_t(std::clamp(b, 0.0f, 1.0f) * 255.0f + 0.5f);
	const uint32_t ca = uint32_t(std::clamp(a, 0.0f, 1.0f) * 255.0f + 0.5f);
	return (ca << 24) | (cr << 16) | (cg << 8) | cb;
}

static MATRIX3 TransposeMat3(const MATRIX3 &m)
{
	return _M(
		m.m11, m.m21, m.m31,
		m.m12, m.m22, m.m32,
		m.m13, m.m23, m.m33);
}
} // namespace

VkCelSphere::VkCelSphere(oapi::VulkanClient *client, VkScene *scn)
	: oapi::CelestialSphere(client)
	, gc(client)
	, scene(scn)
{
	mGal2Ecl.Ident();
	VkMatSetRotation(&mGal2Ecl, &Ecliptic_Galactic());
	starCutoffIdx.fill(0);
}

VkCelSphere::~VkCelSphere()
{
	Destroy();
}

void VkCelSphere::SetViewportSize(DWORD w, DWORD h)
{
	viewW = w;
	viewH = h;
}

bool VkCelSphere::Init()
{
	Destroy();

	if (!gc || !scene || !gc->GetContext() || !gc->GetContext()->IsValid() || !gc->GetSwapchain())
		return false;

	if (!CreatePipelines())
		return false;

	if (!InitStars())
		VkLogWarn("VkCelSphere: InitStars had no star records");
	if (!InitConstellationLines())
		VkLogWarn("VkCelSphere: InitConstellationLines failed");
	if (!InitConstellationBoundaries())
		VkLogWarn("VkCelSphere: InitConstellationBoundaries failed");
	if (!AllocGrids())
		VkLogWarn("VkCelSphere: AllocGrids failed");
	if (!AllocGridLabels())
		VkLogWarn("VkCelSphere: AllocGridLabels failed");

	useStarDots = true;
	if (const void *prm = gc->GetConfigParam(CFGPRM_CSPHEREUSESTARDOTS))
		useStarDots = *static_cast<const bool *>(prm);

	useStarImage = false;
	if (const void *prm = gc->GetConfigParam(CFGPRM_CSPHEREUSESTARIMAGE))
		useStarImage = *static_cast<const bool *>(prm);

	useBgImage = false;
	if (const void *prm = gc->GetConfigParam(CFGPRM_CSPHEREUSEBGIMAGE))
		useBgImage = *static_cast<const bool *>(prm);

	bgIntens = 0.3f;
	if (const void *prm = gc->GetConfigParam(CFGPRM_CSPHEREINTENS))
		bgIntens = float(*static_cast<const double *>(prm));

	if (useBgImage)
		LoadBackgroundImageLayer();
	if (useStarImage)
		LoadStarImageLayer();

	return true;
}

void VkCelSphere::Destroy()
{
	VkContext *ctx = gc ? gc->GetContext() : nullptr;

	if (mgrBkgImage) {
		mgrBkgImage->Destroy(ctx);
		mgrBkgImage.reset();
	}
	if (mgrStarImage) {
		mgrStarImage->Destroy(ctx);
		mgrStarImage.reset();
	}

	if (gridLabelTex) {
		if (gridLabelTex->DecRef())
			delete gridLabelTex;
		gridLabelTex = nullptr;
	}
	labelDescSet = VK_NULL_HANDLE;

	if (ctx && ctx->IsValid()) {
		if (labelDescPool != VK_NULL_HANDLE) {
			vkDestroyDescriptorPool(ctx->Device(), labelDescPool, nullptr);
			labelDescPool = VK_NULL_HANDLE;
		}
		starVB.Destroy(*ctx);
		constLineVB.Destroy(*ctx);
		constBndVB.Destroy(*ctx);
		gridVB.Destroy(*ctx);
		gridLabelVB.Destroy(*ctx);
		gridLabelIB.Destroy(*ctx);
		DestroyPipelines();
	}

	starCount = 0;
	constLineVtxCount = 0;
	constBndVtxCount = 0;
	equatorOffset = 0;
	equatorCount = 0;
	minorGridOffset = 0;
	minorGridCount = 0;
	azLabelFirstIdx = 0;
	azLabelIdxCount = 0;
	elLabelFirstIdx = 0;
	elLabelIdxCount = 0;
}

bool VkCelSphere::CreatePipelines()
{
	VkContext *ctx = gc->GetContext();
	VkSwapchain *swap = gc->GetSwapchain();
	const VkDevice dev = ctx->Device();
	const VkFormat colorFmt = swap->SceneFormat();
	const VkFormat depthFmt = swap->DepthFormat();
	const VkFormat stencilFmt = swap->SceneStencilFormat();

	// Single sampled-texture set layout shared by SceneLabel and CelSphere
	VkDescriptorSetLayoutBinding texBinding{};
	texBinding.binding         = 0;
	texBinding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	texBinding.descriptorCount = 1;
	texBinding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutCreateInfo dslci{};
	dslci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	dslci.bindingCount = 1;
	dslci.pBindings    = &texBinding;

	if (vkCreateDescriptorSetLayout(dev, &dslci, nullptr, &texSetLayout) != VK_SUCCESS) {
		VkLogErr("VkCelSphere: vkCreateDescriptorSetLayout failed");
		DestroyPipelines();
		return false;
	}

	auto makeLayout = [&](uint32_t setCount, const VkDescriptorSetLayout *sets, VkShaderStageFlags pcStages, uint32_t pcSize, VkPipelineLayout &out) -> bool {
		VkPushConstantRange pcr{};
		pcr.stageFlags = pcStages;
		pcr.offset     = 0;
		pcr.size       = pcSize;

		VkPipelineLayoutCreateInfo plci{};
		plci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		plci.setLayoutCount         = setCount;
		plci.pSetLayouts            = sets;
		plci.pushConstantRangeCount = 1;
		plci.pPushConstantRanges    = &pcr;
		return vkCreatePipelineLayout(dev, &plci, nullptr, &out) == VK_SUCCESS;
	};

	if (!makeLayout(0, nullptr, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(SceneLinePC), pipeLayoutLine) ||
	    !makeLayout(0, nullptr, VK_SHADER_STAGE_VERTEX_BIT, sizeof(SceneStarPC), pipeLayoutStar) ||
	    !makeLayout(1, &texSetLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(SceneLabelPC), pipeLayoutTex))
	{
		VkLogErr("VkCelSphere: vkCreatePipelineLayout failed");
		DestroyPipelines();
		return false;
	}

	auto buildPipe = [&](
		const void *vertBytes,
		size_t vertSize,
		const void *fragBytes,
		size_t fragSize,
		VkPipelineLayout layout,
		VkPrimitiveTopology topology,
		uint32_t stride,
		uint32_t attrCount,
		const VkVertexInputAttributeDescription *attrs,
		VkCullModeFlags cullMode,
		VkBlendFactor srcColorBlend,
		VkPipeline &outPipe) -> bool
	{
		VkShaderModule vs = VkCreateShaderModuleFromBytes(dev, vertBytes, vertSize);
		VkShaderModule fs = VkCreateShaderModuleFromBytes(dev, fragBytes, fragSize);
		if (vs == VK_NULL_HANDLE || fs == VK_NULL_HANDLE) {
			if (vs != VK_NULL_HANDLE) vkDestroyShaderModule(dev, vs, nullptr);
			if (fs != VK_NULL_HANDLE) vkDestroyShaderModule(dev, fs, nullptr);
			return false;
		}

		VkPipelineShaderStageCreateInfo stages[2]{};
		stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
		stages[0].module = vs;
		stages[0].pName  = "main";
		stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
		stages[1].module = fs;
		stages[1].pName  = "main";

		VkVertexInputBindingDescription vibd{};
		vibd.binding   = 0;
		vibd.stride    = stride;
		vibd.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		VkPipelineVertexInputStateCreateInfo vi{};
		vi.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vi.vertexBindingDescriptionCount   = 1;
		vi.pVertexBindingDescriptions      = &vibd;
		vi.vertexAttributeDescriptionCount = attrCount;
		vi.pVertexAttributeDescriptions    = attrs;

		VkPipelineInputAssemblyStateCreateInfo ia{};
		ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		ia.topology = topology;

		VkPipelineViewportStateCreateInfo vp{};
		vp.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		vp.viewportCount = 1;
		vp.scissorCount  = 1;

		VkPipelineRasterizationStateCreateInfo rs{};
		rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rs.polygonMode = VK_POLYGON_MODE_FILL;
		rs.cullMode    = cullMode;
		rs.frontFace   = VK_FRONT_FACE_CLOCKWISE;
		rs.lineWidth   = 1.0f;

		VkPipelineMultisampleStateCreateInfo ms{};
		ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		VkPipelineDepthStencilStateCreateInfo ds{};
		ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		ds.depthTestEnable  = VK_FALSE;
		ds.depthWriteEnable = VK_FALSE;
		ds.depthCompareOp   = VK_COMPARE_OP_ALWAYS;

		VkPipelineColorBlendAttachmentState cbAtt{};
		cbAtt.blendEnable         = VK_TRUE;
		cbAtt.srcColorBlendFactor = srcColorBlend;
		cbAtt.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
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

		VkPipelineRenderingCreateInfo rci{};
		rci.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
		rci.colorAttachmentCount    = 1;
		rci.pColorAttachmentFormats = &colorFmt;
		rci.depthAttachmentFormat   = depthFmt;
		rci.stencilAttachmentFormat = stencilFmt;

		VkGraphicsPipelineCreateInfo pci{};
		pci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pci.pNext               = &rci;
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
		pci.layout              = layout;

		const VkResult res = vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &pci, nullptr, &outPipe);
		vkDestroyShaderModule(dev, fs, nullptr);
		vkDestroyShaderModule(dev, vs, nullptr);
		return res == VK_SUCCESS;
	};

	const VkVertexInputAttributeDescription lineAttrs[1] = {
		{ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VkVertexXYZ, x) }
	};
	const VkVertexInputAttributeDescription starAttrs[2] = {
		{ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VkVertexXYZC, x) },
		{ 1, 0, VK_FORMAT_B8G8R8A8_UNORM,   offsetof(VkVertexXYZC, col) }
	};
	const VkVertexInputAttributeDescription labelAttrs[2] = {
		{ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VkVertexXYZTex, x) },
		{ 1, 0, VK_FORMAT_R32G32_SFLOAT,    offsetof(VkVertexXYZTex, tu) }
	};
	const VkVertexInputAttributeDescription csphereAttrs[2] = {
		{ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VkVertex2Tex, x) },
		{ 1, 0, VK_FORMAT_R32G32_SFLOAT,    offsetof(VkVertex2Tex, tu0) }
	};

	if (!buildPipe(g_SceneLineVertSpv, sizeof(g_SceneLineVertSpv), g_SceneLineFragSpv, sizeof(g_SceneLineFragSpv),
	               pipeLayoutLine, VK_PRIMITIVE_TOPOLOGY_LINE_LIST, sizeof(VkVertexXYZ), 1, lineAttrs,
	               VK_CULL_MODE_NONE, VK_BLEND_FACTOR_SRC_ALPHA, pipeLine) ||
	    !buildPipe(g_SceneStarVertSpv, sizeof(g_SceneStarVertSpv), g_SceneStarFragSpv, sizeof(g_SceneStarFragSpv),
	               pipeLayoutStar, VK_PRIMITIVE_TOPOLOGY_POINT_LIST, sizeof(VkVertexXYZC), 2, starAttrs,
	               VK_CULL_MODE_NONE, VK_BLEND_FACTOR_ONE, pipeStar) ||
	    !buildPipe(g_SceneLabelVertSpv, sizeof(g_SceneLabelVertSpv), g_SceneLabelFragSpv, sizeof(g_SceneLabelFragSpv),
	               pipeLayoutTex, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, sizeof(VkVertexXYZTex), 2, labelAttrs,
	               VK_CULL_MODE_BACK_BIT, VK_BLEND_FACTOR_SRC_ALPHA, pipeLabel) ||
	    !buildPipe(g_CelSphereVertSpv, sizeof(g_CelSphereVertSpv), g_CelSphereFragSpv, sizeof(g_CelSphereFragSpv),
	               pipeLayoutTex, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, sizeof(VkVertex2Tex), 2, csphereAttrs,
	               VK_CULL_MODE_BACK_BIT, VK_BLEND_FACTOR_ONE, pipeCSphere))
	{
		VkLogErr("VkCelSphere: graphics pipeline creation failed");
		DestroyPipelines();
		return false;
	}

	return true;
}

void VkCelSphere::DestroyPipelines()
{
	VkContext *ctx = gc ? gc->GetContext() : nullptr;
	if (!ctx || !ctx->IsValid())
		return;

	const VkDevice dev = ctx->Device();
	auto destroyPipe = [&](VkPipeline &p) {
		if (p != VK_NULL_HANDLE) {
			vkDestroyPipeline(dev, p, nullptr);
			p = VK_NULL_HANDLE;
		}
	};
	auto destroyLayout = [&](VkPipelineLayout &l) {
		if (l != VK_NULL_HANDLE) {
			vkDestroyPipelineLayout(dev, l, nullptr);
			l = VK_NULL_HANDLE;
		}
	};

	destroyPipe(pipeCSphere);
	destroyPipe(pipeLabel);
	destroyPipe(pipeStar);
	destroyPipe(pipeLine);

	destroyLayout(pipeLayoutTex);
	destroyLayout(pipeLayoutStar);
	destroyLayout(pipeLayoutLine);

	if (texSetLayout != VK_NULL_HANDLE) {
		vkDestroyDescriptorSetLayout(dev, texSetLayout, nullptr);
		texSetLayout = VK_NULL_HANDLE;
	}
}

bool VkCelSphere::InitStars()
{
	VkContext *ctx = gc->GetContext();
	starVB.Destroy(*ctx);
	starCount = 0;
	starCutoffIdx.fill(0);

	const std::vector<StarRenderRec> stars = LoadStars();
	if (stars.empty())
		return false;

	starCutoffIdx = ComputeStarBrightnessCutoff(stars);

	std::vector<VkVertexXYZC> verts(stars.size());
	for (size_t i = 0; i < stars.size(); ++i) {
		const StarRenderRec &s = stars[i];
		verts[i].x   = float(s.pos.x);
		verts[i].y   = float(s.pos.y);
		verts[i].z   = float(s.pos.z);
		verts[i].col = PackBgraColor(float(s.col.x), float(s.col.y), float(s.col.z), float(s.brightness));
	}

	if (!starVB.UploadStatic(*ctx, verts.data(), VkDeviceSize(verts.size()) * sizeof(VkVertexXYZC), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT))
		return false;

	starCount = uint32_t(verts.size());
	VkLogInfo("VkCelSphere: uploaded %u Hipparcos/Tycho stars", starCount);
	return true;
}

bool VkCelSphere::InitConstellationLines()
{
	VkContext *ctx = gc->GetContext();
	constLineVB.Destroy(*ctx);
	constLineVtxCount = 0;

	const std::vector<VECTOR3> pts = LoadConstellationLines();
	if (pts.empty())
		return false;

	std::vector<VkVertexXYZ> verts(pts.size());
	for (size_t i = 0; i < pts.size(); ++i) {
		verts[i].x = float(pts[i].x);
		verts[i].y = float(pts[i].y);
		verts[i].z = float(pts[i].z);
	}

	if (!constLineVB.UploadStatic(*ctx, verts.data(), VkDeviceSize(verts.size()) * sizeof(VkVertexXYZ), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT))
		return false;

	constLineVtxCount = uint32_t(verts.size());
	return true;
}

bool VkCelSphere::InitConstellationBoundaries()
{
	VkContext *ctx = gc->GetContext();
	constBndVB.Destroy(*ctx);
	constBndVtxCount = 0;

	const std::vector<VECTOR3> pts = LoadConstellationBoundaries();
	if (pts.empty())
		return false;

	std::vector<VkVertexXYZ> verts(pts.size());
	for (size_t i = 0; i < pts.size(); ++i) {
		verts[i].x = float(pts[i].x);
		verts[i].y = float(pts[i].y);
		verts[i].z = float(pts[i].z);
	}

	if (!constBndVB.UploadStatic(*ctx, verts.data(), VkDeviceSize(verts.size()) * sizeof(VkVertexXYZ), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT))
		return false;

	constBndVtxCount = uint32_t(verts.size());
	return true;
}

bool VkCelSphere::AllocGrids()
{
	VkContext *ctx = gc->GetContext();
	gridVB.Destroy(*ctx);
	equatorOffset = 0;
	equatorCount = 0;
	minorGridOffset = 0;
	minorGridCount = 0;

	constexpr int kCircleSegs = 64;
	std::vector<VkVertexXYZ> verts;
	verts.reserve(3000);

	auto addParallel = [&](double latRad) {
		const float y = float(std::sin(latRad));
		const float r = float(std::cos(latRad));
		for (int s = 0; s < kCircleSegs; ++s) {
			const double a0 = (2.0 * kPi * double(s)) / double(kCircleSegs);
			const double a1 = (2.0 * kPi * double(s + 1)) / double(kCircleSegs);
			verts.push_back({ r * float(std::cos(a0)), y, r * float(std::sin(a0)) });
			verts.push_back({ r * float(std::cos(a1)), y, r * float(std::sin(a1)) });
		}
	};

	equatorOffset = 0;
	addParallel(0.0);
	equatorCount = uint32_t(verts.size());

	minorGridOffset = equatorCount;
	static constexpr int kParallelsDeg[] = { -75, -60, -45, -30, -15, 15, 30, 45, 60, 75 };
	for (int deg : kParallelsDeg)
		addParallel(double(deg) * (kPi / 180.0));

	// 24 hour / 15-degree meridians; stop at +/-75 deg except prime circles 0/90/180/270 deg
	for (int i = 0; i < 24; ++i) {
		const double lng = (2.0 * kPi * double(i)) / 24.0;
		const float cx = float(std::cos(lng));
		const float cz = float(std::sin(lng));
		const int latLimDeg = ((i % 6) == 0) ? 90 : 75;
		const int segs = (latLimDeg == 90) ? 36 : 30;
		for (int s = 0; s < segs; ++s) {
			const double lat0 = double(-latLimDeg + s * 5) * (kPi / 180.0);
			const double lat1 = double(-latLimDeg + (s + 1) * 5) * (kPi / 180.0);
			const float c0 = float(std::cos(lat0)), s0 = float(std::sin(lat0));
			const float c1 = float(std::cos(lat1)), s1 = float(std::sin(lat1));
			verts.push_back({ c0 * cx, s0, c0 * cz });
			verts.push_back({ c1 * cx, s1, c1 * cz });
		}
	}
	minorGridCount = uint32_t(verts.size()) - minorGridOffset;

	return gridVB.UploadStatic(*ctx, verts.data(), VkDeviceSize(verts.size()) * sizeof(VkVertexXYZ), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
}

bool VkCelSphere::AllocGridLabels()
{
	VkContext *ctx = gc->GetContext();
	VkSwapchain *swap = gc->GetSwapchain();
	VkPadPipeline *padPipe = gc->GetPadPipeline();

	const MESHHANDLE hMesh = GridLabelMesh();
	if (!hMesh || oapiMeshGroupCount(hMesh) < 2)
		return false;

	const MESHGROUP *gAz = oapiMeshGroup(hMesh, 0);
	const MESHGROUP *gEl = oapiMeshGroup(hMesh, 1);
	if (!gAz || !gEl || !gAz->Vtx || !gAz->Idx || !gEl->Vtx || !gEl->Idx)
		return false;

	std::vector<VkVertexXYZTex> verts;
	std::vector<uint16_t> indices;
	verts.reserve(gAz->nVtx + gEl->nVtx);
	indices.reserve(gAz->nIdx + gEl->nIdx);

	azLabelFirstIdx = 0;
	azLabelIdxCount = gAz->nIdx;
	for (DWORD i = 0; i < gAz->nVtx; ++i) {
		const NTVERTEX &v = gAz->Vtx[i];
		verts.push_back({ v.x, v.y, v.z, v.tu, v.tv });
	}
	for (DWORD i = 0; i < gAz->nIdx; ++i)
		indices.push_back(gAz->Idx[i]);

	const uint16_t baseEl = uint16_t(verts.size());
	elLabelFirstIdx = uint32_t(indices.size());
	elLabelIdxCount = gEl->nIdx;
	for (DWORD i = 0; i < gEl->nVtx; ++i) {
		const NTVERTEX &v = gEl->Vtx[i];
		verts.push_back({ v.x, v.y, v.z, v.tu, v.tv });
	}
	for (DWORD i = 0; i < gEl->nIdx; ++i)
		indices.push_back(uint16_t(baseEl + gEl->Idx[i]));

	if (!gridLabelVB.UploadStatic(*ctx, verts.data(), VkDeviceSize(verts.size()) * sizeof(VkVertexXYZTex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) ||
	    !gridLabelIB.UploadStatic(*ctx, indices.data(), VkDeviceSize(indices.size()) * sizeof(uint16_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT))
	{
		return false;
	}

	gridLabelTex = VkSurfNative::LoadFromFile(*gc, *ctx, *swap, "gridlabel.dds", 0, false, false);
	if (!gridLabelTex || gridLabelTex->GetImageView() == VK_NULL_HANDLE) {
		VkLogWarn("VkCelSphere: failed to load gridlabel.dds");
		if (gridLabelTex && gridLabelTex->DecRef())
			delete gridLabelTex;
		gridLabelTex = nullptr;
		return false;
	}

	VkDescriptorPoolSize poolSize{};
	poolSize.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	poolSize.descriptorCount = 1;

	VkDescriptorPoolCreateInfo dpci{};
	dpci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	dpci.maxSets       = 1;
	dpci.poolSizeCount = 1;
	dpci.pPoolSizes    = &poolSize;

	if (vkCreateDescriptorPool(ctx->Device(), &dpci, nullptr, &labelDescPool) != VK_SUCCESS)
		return false;

	VkDescriptorSetAllocateInfo dsai{};
	dsai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	dsai.descriptorPool     = labelDescPool;
	dsai.descriptorSetCount = 1;
	dsai.pSetLayouts        = &texSetLayout;

	if (vkAllocateDescriptorSets(ctx->Device(), &dsai, &labelDescSet) != VK_SUCCESS)
		return false;

	const VkSampler smp = padPipe ? padPipe->SamplerForFilter(VkPadPipeline::FilterMode::Linear) : VK_NULL_HANDLE;
	if (smp == VK_NULL_HANDLE)
		return false;

	VkDescriptorImageInfo dii{};
	dii.sampler     = smp;
	dii.imageView   = gridLabelTex->GetImageView();
	dii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkWriteDescriptorSet wds{};
	wds.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	wds.dstSet          = labelDescSet;
	wds.dstBinding      = 0;
	wds.descriptorCount = 1;
	wds.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	wds.pImageInfo      = &dii;
	vkUpdateDescriptorSets(ctx->Device(), 1, &wds, 0, nullptr);

	return true;
}

void VkCelSphere::LoadBackgroundImageLayer()
{
	const char *path = static_cast<const char *>(gc->GetConfigParam(CFGPRM_CSPHERETEXTURE));
	VkPadPipeline *padPipe = gc->GetPadPipeline();
	const VkSampler smp = padPipe ? padPipe->SamplerForFilter(VkPadPipeline::FilterMode::Aniso) : VK_NULL_HANDLE;
	if (!path || !path[0] || smp == VK_NULL_HANDLE)
		return;

	if (!mgrBkgImage)
		mgrBkgImage = std::make_unique<VkCSphereMgr>();
	if (mgrBkgImage->IsLoaded() && mgrBkgImage->LoadedPath() == path)
		return;
	mgrBkgImage->Load(*gc, path, texSetLayout, smp, mGal2Ecl);
}

void VkCelSphere::LoadStarImageLayer()
{
	const char *path = static_cast<const char *>(gc->GetConfigParam(CFGPRM_CSPHERESTARTEXTURE));
	VkPadPipeline *padPipe = gc->GetPadPipeline();
	const VkSampler smp = padPipe ? padPipe->SamplerForFilter(VkPadPipeline::FilterMode::Aniso) : VK_NULL_HANDLE;
	if (!path || !path[0] || smp == VK_NULL_HANDLE)
		return;

	if (!mgrStarImage)
		mgrStarImage = std::make_unique<VkCSphereMgr>();
	if (mgrStarImage->IsLoaded() && mgrStarImage->LoadedPath() == path)
		return;
	mgrStarImage->Load(*gc, path, texSetLayout, smp, mGal2Ecl);
}

void VkCelSphere::OnOptionChanged(int cat, int item)
{
	if (!gc || cat != OPTCAT_CELSPHERE)
		return;

	switch (item) {
	case OPTITEM_CELSPHERE_ACTIVATESTARDOTS:
		if (const void *prm = gc->GetConfigParam(CFGPRM_CSPHEREUSESTARDOTS))
			useStarDots = *static_cast<const bool *>(prm);
		break;

	case OPTITEM_CELSPHERE_STARDISPLAYPARAM:
		if (gc->GetContext() && gc->GetContext()->IsValid()) {
			vkDeviceWaitIdle(gc->GetContext()->Device());
			InitStars();
		}
		break;

	case OPTITEM_CELSPHERE_ACTIVATESTARIMAGE:
		if (const void *prm = gc->GetConfigParam(CFGPRM_CSPHEREUSESTARIMAGE))
			useStarImage = *static_cast<const bool *>(prm);
		if (useStarImage)
			LoadStarImageLayer();
		break;

	case OPTITEM_CELSPHERE_STARIMAGECHANGED:
		if (useStarImage) {
			if (mgrStarImage)
				mgrStarImage->Destroy(gc->GetContext());
			LoadStarImageLayer();
		} else if (mgrStarImage) {
			mgrStarImage->Destroy(gc->GetContext());
		}
		break;

	case OPTITEM_CELSPHERE_ACTIVATEBGIMAGE:
		if (const void *prm = gc->GetConfigParam(CFGPRM_CSPHEREUSEBGIMAGE))
			useBgImage = *static_cast<const bool *>(prm);
		if (useBgImage)
			LoadBackgroundImageLayer();
		break;

	case OPTITEM_CELSPHERE_BGIMAGECHANGED:
		if (useBgImage) {
			if (mgrBkgImage)
				mgrBkgImage->Destroy(gc->GetContext());
			LoadBackgroundImageLayer();
		} else if (mgrBkgImage) {
			mgrBkgImage->Destroy(gc->GetContext());
		}
		break;

	case OPTITEM_CELSPHERE_BGIMAGEBRIGHTNESS:
		if (const void *prm = gc->GetConfigParam(CFGPRM_CSPHEREINTENS))
			bgIntens = float(*static_cast<const double *>(prm));
		break;

	default:
		break;
	}
}

void VkCelSphere::RenderLineSet(
	VkCommandBuffer cmd,
	const VkGpuBuffer &vb,
	uint32_t firstVertex,
	uint32_t vertexCount,
	const oapi::FMATRIX4 &mModel,
	const oapi::FVECTOR4 &color) const
{
	if (vb.buffer == VK_NULL_HANDLE || vertexCount == 0 || pipeLine == VK_NULL_HANDLE)
		return;

	const VkDeviceSize offset = 0;
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeLine);
	vkCmdBindVertexBuffers(cmd, 0, 1, &vb.buffer, &offset);

	SceneLinePC pc{};
	pc.mWVP = VkMatMul(mModel, *scene->GetProjectionViewInfMatrix());
	pc.color[0] = color.x;
	pc.color[1] = color.y;
	pc.color[2] = color.z;
	pc.color[3] = color.w;

	vkCmdPushConstants(
		cmd,
		pipeLayoutLine,
		VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		0,
		sizeof(SceneLinePC),
		&pc);
	vkCmdDraw(cmd, vertexCount, 1, firstVertex, 0);
}

void VkCelSphere::RenderGrid(
	VkCommandBuffer cmd,
	const oapi::FMATRIX4 &mModel,
	const oapi::FVECTOR4 &baseCol,
	double elevRotAngle) const
{
	const oapi::FVECTOR4 adjMain = ColorAdjusted(baseCol);
	const oapi::FVECTOR4 adjSub = ColorAdjusted(oapi::FVECTOR4(baseCol.x * 0.5f, baseCol.y * 0.5f, baseCol.z * 0.5f, baseCol.w));

	if (gridVB.buffer != VK_NULL_HANDLE && pipeLine != VK_NULL_HANDLE) {
		const VkDeviceSize offset = 0;
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeLine);
		vkCmdBindVertexBuffers(cmd, 0, 1, &gridVB.buffer, &offset);

		SceneLinePC pc{};
		pc.mWVP = VkMatMul(mModel, *scene->GetProjectionViewInfMatrix());

		if (minorGridCount > 0) {
			pc.color[0] = adjSub.x;
			pc.color[1] = adjSub.y;
			pc.color[2] = adjSub.z;
			pc.color[3] = 1.0f;
			vkCmdPushConstants(cmd, pipeLayoutLine, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SceneLinePC), &pc);
			vkCmdDraw(cmd, minorGridCount, 1, minorGridOffset, 0);
		}

		if (equatorCount > 0) {
			pc.color[0] = adjMain.x;
			pc.color[1] = adjMain.y;
			pc.color[2] = adjMain.z;
			pc.color[3] = 1.0f;
			vkCmdPushConstants(cmd, pipeLayoutLine, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SceneLinePC), &pc);
			vkCmdDraw(cmd, equatorCount, 1, equatorOffset, 0);
		}
	}

	if (gridLabelVB.buffer != VK_NULL_HANDLE && gridLabelIB.buffer != VK_NULL_HANDLE &&
	    labelDescSet != VK_NULL_HANDLE && pipeLabel != VK_NULL_HANDLE)
	{
		const VkDeviceSize offset = 0;
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeLabel);
		vkCmdBindVertexBuffers(cmd, 0, 1, &gridLabelVB.buffer, &offset);
		vkCmdBindIndexBuffer(cmd, gridLabelIB.buffer, 0, VK_INDEX_TYPE_UINT16);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeLayoutTex, 0, 1, &labelDescSet, 0, nullptr);

		SceneLabelPC lpc{};
		lpc.color[0] = std::min(1.0f, adjMain.x * 1.4f);
		lpc.color[1] = std::min(1.0f, adjMain.y * 1.4f);
		lpc.color[2] = std::min(1.0f, adjMain.z * 1.4f);
		lpc.color[3] = 1.0f;

		if (azLabelIdxCount > 0) {
			lpc.mWVP = VkMatMul(mModel, *scene->GetProjectionViewInfMatrix());
			vkCmdPushConstants(cmd, pipeLayoutTex, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SceneLabelPC), &lpc);
			vkCmdDrawIndexed(cmd, azLabelIdxCount, 1, azLabelFirstIdx, 0, 0);
		}

		if (elLabelIdxCount > 0) {
			oapi::FMATRIX4 mElevRot;
			VkMatRotY(&mElevRot, elevRotAngle);
			const oapi::FMATRIX4 mElevWorld = VkMatMul(mElevRot, mModel);
			lpc.mWVP = VkMatMul(mElevWorld, *scene->GetProjectionViewInfMatrix());
			vkCmdPushConstants(cmd, pipeLayoutTex, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SceneLabelPC), &lpc);
			vkCmdDrawIndexed(cmd, elLabelIdxCount, 1, elLabelFirstIdx, 0, 0);
		}
	}
}

void VkCelSphere::Render(VkCommandBuffer cmd)
{
	if (!gc || !scene || cmd == VK_NULL_HANDLE)
		return;

	SetSkyColour(scene->GetSkyColour());
	const double skyBrt = GetSkyBrightness();
	const VECTOR3 &skyCol = GetSkyColour();
	const float skyFade = std::max(0.0f, 1.0f - float(skyBrt) * 0.9f);

	// 1. Background skybox image (e.g., Milky Way)
	if (useBgImage && mgrBkgImage && mgrBkgImage->IsLoaded() && pipeCSphere != VK_NULL_HANDLE) {
		const float intens = bgIntens * skyFade;
		if (intens > 0.002f) {
			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeCSphere);
			mgrBkgImage->Render(cmd, pipeLayoutTex, scene, oapi::FVECTOR3(intens, intens, intens), 1.0f);
		}
	}

	// 2. Deep-sky / Hipparcos star image layer
	if (useStarImage && mgrStarImage && mgrStarImage->IsLoaded() && pipeCSphere != VK_NULL_HANDLE) {
		if (skyFade > 0.002f) {
			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeCSphere);
			mgrStarImage->Render(cmd, pipeLayoutTex, scene, oapi::FVECTOR3(skyFade, skyFade, skyFade), 1.0f);
		}
	}

	// 3. Hipparcos/Tycho catalog point stars with daytime brightness cutoff
	if (useStarDots && starVB.buffer != VK_NULL_HANDLE && starCount > 0 && pipeStar != VK_NULL_HANDLE) {
		const int lvl = std::clamp(int(skyBrt * 255.0 + 0.5), 0, 255);
		const uint32_t drawCount = std::min(starCount, uint32_t(std::max(0, starCutoffIdx[size_t(lvl)])));
		if (drawCount > 0) {
			const VkDeviceSize offset = 0;
			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeStar);
			vkCmdBindVertexBuffers(cmd, 0, 1, &starVB.buffer, &offset);

			SceneStarPC pc{};
			pc.mWVP = *scene->GetProjectionViewInfMatrix();
			pc.skyColor[0] = float(skyCol.x) * 0.5f;
			pc.skyColor[1] = float(skyCol.y) * 0.5f;
			pc.skyColor[2] = float(skyCol.z) * 0.5f;
			pc.skyColor[3] = 1.0f;

			vkCmdPushConstants(cmd, pipeLayoutStar, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(SceneStarPC), &pc);
			vkCmdDraw(cmd, drawCount, 1, 0, 0);
		}
	}

	// 4. Planetarium grids, constellations, boundaries & equator
	DWORD pln = 0;
	if (const void *prm = gc->GetConfigParam(CFGPRM_PLANETARIUMFLAG))
		pln = *static_cast<const DWORD *>(prm);
	if (!(pln & PLN_ENABLE))
		return;

	const oapi::FMATRIX4 mIdent = VkMatIdentity();

	if ((pln & PLN_CNSTBND) && constBndVtxCount > 0) {
		const oapi::FVECTOR4 col = ColorAdjusted(oapi::FVECTOR4(0.25f, 0.18f, 0.32f, 1.0f));
		RenderLineSet(cmd, constBndVB, 0, constBndVtxCount, mIdent, col);
	}

	if ((pln & PLN_CONST) && constLineVtxCount > 0) {
		const oapi::FVECTOR4 col = ColorAdjusted(oapi::FVECTOR4(0.45f, 0.35f, 0.20f, 1.0f));
		RenderLineSet(cmd, constLineVB, 0, constLineVtxCount, mIdent, col);
	}

	if (pln & PLN_EGRID) {
		const MATRIX3 rIdent = _M(1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0);
		RenderGrid(cmd, mIdent, oapi::FVECTOR4(0.0f, 0.25f, 0.55f, 1.0f), ElevationScaleRotation(rIdent));
	}

	if (pln & PLN_CGRID) {
		const MATRIX3 rCel = Ecliptic_CelestialAtEpoch();
		oapi::FMATRIX4 mCel = VkMatIdentity();
		VkMatSetRotation(&mCel, &rCel);
		RenderGrid(cmd, mCel, oapi::FVECTOR4(0.42f, 0.0f, 0.42f, 1.0f), ElevationScaleRotation(rCel));
	}

	if (pln & PLN_GGRID) {
		const MATRIX3 &rGal = Ecliptic_Galactic();
		RenderGrid(cmd, mGal2Ecl, oapi::FVECTOR4(0.0f, 0.45f, 0.22f, 1.0f), ElevationScaleRotation(rGal));
	}

	if (pln & PLN_HGRID) {
		MATRIX3 rHoriz{};
		if (LocalHorizon_Ecliptic(rHoriz)) {
			oapi::FMATRIX4 mHoriz = VkMatIdentity();
			VkMatSetInvRotation(&mHoriz, &rHoriz);
			RenderGrid(
				cmd,
				mHoriz,
				oapi::FVECTOR4(0.50f, 0.35f, 0.10f, 1.0f),
				ElevationScaleRotation(TransposeMat3(rHoriz)));
		}
	}

	if ((pln & PLN_EQU) && equatorCount > 0) {
		OBJHANDLE hBody = oapiCameraTarget();
		if (!hBody || oapiGetObjectType(hBody) != OBJTP_PLANET)
			hBody = scene->GetCameraProxyBody();
		if (hBody) {
			MATRIX3 rBody{};
			oapiGetRotationMatrix(hBody, &rBody);
			oapi::FMATRIX4 mBody = VkMatIdentity();
			VkMatSetInvRotation(&mBody, &rBody);
			const oapi::FVECTOR4 col = ColorAdjusted(oapi::FVECTOR4(0.0f, 0.55f, 0.55f, 1.0f));
			RenderLineSet(cmd, gridVB, equatorOffset, equatorCount, mBody, col);
		}
	}
}

void VkCelSphere::RenderOverlay()
{
	if (!gc)
		return;

	DWORD pln = 0;
	if (const void *prm = gc->GetConfigParam(CFGPRM_PLANETARIUMFLAG))
		pln = *static_cast<const DWORD *>(prm);
	if (!(pln & PLN_ENABLE))
		return;

	oapi::Sketchpad *pSkp = nullptr;
	if (pln & PLN_CNSTLABEL)
		RenderConstellationLabels(&pSkp, (pln & PLN_CNSTLONG) != 0);
	if (pln & PLN_CCMARK)
		RenderCelestialMarkers(&pSkp);
	if (pSkp)
		gc->clbkReleaseSketchpad(pSkp);
}

bool VkCelSphere::EclDir2WindowPos(const VECTOR3 &dir, int &x, int &y) const
{
	if (!scene)
		return false;

	const DWORD w = scene->GetViewW();
	const DWORD h = scene->GetViewH();
	if (w == 0 || h == 0)
		return false;

	const oapi::FMATRIX4 *vp = scene->GetProjectionViewInfMatrix();
	const float vx = float(dir.x);
	const float vy = float(dir.y);
	const float vz = float(dir.z);

	const float cx = vx * vp->m11 + vy * vp->m21 + vz * vp->m31;
	const float cy = vx * vp->m12 + vy * vp->m22 + vz * vp->m32;
	const float cz = vx * vp->m13 + vy * vp->m23 + vz * vp->m33;
	const float cw = vx * vp->m14 + vy * vp->m24 + vz * vp->m34;

	if (cw <= 1e-5f || cz < 0.0f || cz > cw)
		return false;

	const float nx = cx / cw;
	const float ny = cy / cw;
	if (nx < -1.0f || nx > 1.0f || ny < -1.0f || ny > 1.0f)
		return false;

	x = int(float(w) * 0.5f * (1.0f + nx) + 0.5f);
	y = int(float(h) * 0.5f * (1.0f - ny) + 0.5f);
	return true;
}
