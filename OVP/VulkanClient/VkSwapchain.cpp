// ==============================================================
// VkSwapchain.cpp
// Swapchain, persistent scene target and 2-frames-in-flight loop
// Part of the ORBITER VISUALISATION PROJECT (OVP) - VulkanClient
// Licensed under the MIT License
// ==============================================================

#include "VkSwapchain.h"
#include "VkContext.h"
#include "VkLog.h"

namespace {

const char *PresentModeName(VkPresentModeKHR mode)
{
	switch (mode) {
	case VK_PRESENT_MODE_IMMEDIATE_KHR:    return "IMMEDIATE";
	case VK_PRESENT_MODE_MAILBOX_KHR:      return "MAILBOX";
	case VK_PRESENT_MODE_FIFO_KHR:         return "FIFO";
	case VK_PRESENT_MODE_FIFO_RELAXED_KHR: return "FIFO_RELAXED";
	default:                               return "OTHER";
	}
}

void RecordImageBarrier(
	VkCommandBuffer cmd,
	VkImage image,
	VkImageLayout oldLayout,
	VkImageLayout newLayout,
	VkPipelineStageFlags2 srcStage,
	VkAccessFlags2 srcAccess,
	VkPipelineStageFlags2 dstStage,
	VkAccessFlags2 dstAccess)
{
	VkImageMemoryBarrier2 barrier{};
	barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	barrier.srcStageMask        = srcStage;
	barrier.srcAccessMask       = srcAccess;
	barrier.dstStageMask        = dstStage;
	barrier.dstAccessMask       = dstAccess;
	barrier.oldLayout           = oldLayout;
	barrier.newLayout           = newLayout;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image               = image;
	barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseMipLevel   = 0;
	barrier.subresourceRange.levelCount     = 1;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount     = 1;

	VkDependencyInfo dep{};
	dep.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	dep.imageMemoryBarrierCount = 1;
	dep.pImageMemoryBarriers    = &barrier;
	vkCmdPipelineBarrier2(cmd, &dep);
}

} // namespace

VkSwapchain::~VkSwapchain()
{
	Destroy();
}

bool VkSwapchain::Create(VkContext &context, uint32_t width, uint32_t height, bool disableVSync)
{
	Destroy();

	ctx = &context;
	noVSync = disableVSync;

	if (CreateFrameResources() &&
		CreateSwapchainInternal(width, height) &&
		CreateSceneTarget(swapchain.extent.width, swapchain.extent.height))
	{
		return true;
	}

	Destroy();
	return false;
}

void VkSwapchain::Destroy()
{
	if (ctx && ctx->Device() != VK_NULL_HANDLE)
		vkDeviceWaitIdle(ctx->Device());

	DestroySceneTarget();
	DestroySwapchainInternal();
	DestroyFrameResources();

	ctx = nullptr;
	currentFrame = 0;
	currentImageIndex = 0;
	frameRecording = false;
	frameReadyToPresent = false;
	needsRecreate = false;
}

bool VkSwapchain::Recreate(uint32_t width, uint32_t height)
{
	if (!ctx || width == 0 || height == 0)
		return false;

	vkDeviceWaitIdle(ctx->Device());
	needsRecreate = false;

	if (!CreateSwapchainInternal(width, height))
		return false;

	if (sceneImage == VK_NULL_HANDLE ||
		sceneExtent.width != swapchain.extent.width ||
		sceneExtent.height != swapchain.extent.height)
	{
		if (!CreateSceneTarget(swapchain.extent.width, swapchain.extent.height))
			return false;
	}

	return true;
}

bool VkSwapchain::CreateSwapchainInternal(uint32_t width, uint32_t height)
{
	vkb::SwapchainBuilder builder(ctx->VkbDevice());
	builder.set_desired_extent(width, height)
		.set_desired_min_image_count(kMaxFramesInFlight + 1)
		.set_desired_format({ VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR })
		.add_fallback_format({ VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR })
		.set_image_usage_flags(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);

	if (noVSync) {
		builder.set_desired_present_mode(VK_PRESENT_MODE_MAILBOX_KHR)
			.add_fallback_present_mode(VK_PRESENT_MODE_IMMEDIATE_KHR)
			.add_fallback_present_mode(VK_PRESENT_MODE_FIFO_KHR);
	} else {
		builder.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR);
	}

	if (swapchain.swapchain != VK_NULL_HANDLE)
		builder.set_old_swapchain(swapchain);

	auto scRet = builder.build();
	if (!scRet) {
		VkLogErr("Swapchain creation failed: %s (VkResult=%d)",
			scRet.error().message().c_str(), (int)scRet.vk_result());
		return false;
	}

	DestroySwapchainInternal();
	swapchain = scRet.value();

	auto imgRet = swapchain.get_images();
	auto viewRet = swapchain.get_image_views();
	if (!imgRet || !viewRet) {
		VkLogErr("Failed to retrieve swapchain images/views");
		return false;
	}
	images = imgRet.value();
	imageViews = viewRet.value();

	VkSemaphoreCreateInfo sci{};
	sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	renderFinishedSemaphores.resize(images.size(), VK_NULL_HANDLE);
	for (size_t i = 0; i < images.size(); ++i) {
		VkResult res = vkCreateSemaphore(ctx->Device(), &sci, nullptr, &renderFinishedSemaphores[i]);
		if (res != VK_SUCCESS) {
			VkLogErr("vkCreateSemaphore (renderFinished[%zu]) failed (VkResult=%d)", i, (int)res);
			return false;
		}
	}

	VkLogInfo("Swapchain %ux%u (%u images, format %d, present %s)",
		swapchain.extent.width, swapchain.extent.height,
		swapchain.image_count, (int)swapchain.image_format,
		PresentModeName(swapchain.present_mode));
	return true;
}

void VkSwapchain::DestroySwapchainInternal()
{
	if (!ctx || ctx->Device() == VK_NULL_HANDLE)
		return;

	for (VkSemaphore sem : renderFinishedSemaphores) {
		if (sem != VK_NULL_HANDLE)
			vkDestroySemaphore(ctx->Device(), sem, nullptr);
	}
	renderFinishedSemaphores.clear();

	if (!imageViews.empty()) {
		swapchain.destroy_image_views(imageViews);
		imageViews.clear();
	}
	images.clear();

	if (swapchain.swapchain != VK_NULL_HANDLE) {
		vkb::destroy_swapchain(swapchain);
		swapchain = vkb::Swapchain{};
	}
}

bool VkSwapchain::CreateSceneTarget(uint32_t width, uint32_t height)
{
	DestroySceneTarget();

	VkImageCreateInfo ici{};
	ici.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	ici.imageType     = VK_IMAGE_TYPE_2D;
	ici.format        = sceneFormat;
	ici.extent        = { width, height, 1 };
	ici.mipLevels     = 1;
	ici.arrayLayers   = 1;
	ici.samples       = VK_SAMPLE_COUNT_1_BIT;
	ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
	ici.usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
		VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
		VK_IMAGE_USAGE_TRANSFER_DST_BIT |
		VK_IMAGE_USAGE_SAMPLED_BIT;
	ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
	ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	VmaAllocationCreateInfo aci{};
	aci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

	VkResult res = vmaCreateImage(ctx->Allocator(), &ici, &aci, &sceneImage, &sceneAlloc, nullptr);
	if (res != VK_SUCCESS) {
		VkLogErr("vmaCreateImage (sceneTarget %ux%u) failed (VkResult=%d)", width, height, (int)res);
		sceneImage = VK_NULL_HANDLE;
		sceneAlloc = VK_NULL_HANDLE;
		return false;
	}

	VkImageViewCreateInfo ivci{};
	ivci.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	ivci.image    = sceneImage;
	ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
	ivci.format   = sceneFormat;
	ivci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
	ivci.subresourceRange.baseMipLevel   = 0;
	ivci.subresourceRange.levelCount     = 1;
	ivci.subresourceRange.baseArrayLayer = 0;
	ivci.subresourceRange.layerCount     = 1;

	res = vkCreateImageView(ctx->Device(), &ivci, nullptr, &sceneView);
	if (res != VK_SUCCESS) {
		VkLogErr("vkCreateImageView (sceneTarget) failed (VkResult=%d)", (int)res);
		DestroySceneTarget();
		return false;
	}

	sceneExtent = { width, height };
	sceneLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	return true;
}

void VkSwapchain::DestroySceneTarget()
{
	if (!ctx || ctx->Device() == VK_NULL_HANDLE)
		return;

	if (sceneView != VK_NULL_HANDLE) {
		vkDestroyImageView(ctx->Device(), sceneView, nullptr);
		sceneView = VK_NULL_HANDLE;
	}
	if (sceneImage != VK_NULL_HANDLE && ctx->Allocator() != VK_NULL_HANDLE) {
		vmaDestroyImage(ctx->Allocator(), sceneImage, sceneAlloc);
		sceneImage = VK_NULL_HANDLE;
		sceneAlloc = VK_NULL_HANDLE;
	}
	sceneExtent = { 0, 0 };
	sceneLayout = VK_IMAGE_LAYOUT_UNDEFINED;
}

bool VkSwapchain::CreateFrameResources()
{
	VkCommandPoolCreateInfo cpci{};
	cpci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	cpci.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
	cpci.queueFamilyIndex = ctx->GraphicsQueueFamily();

	VkSemaphoreCreateInfo sci{};
	sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

	VkFenceCreateInfo fci{};
	fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
		if (vkCreateCommandPool(ctx->Device(), &cpci, nullptr, &frames[i].cmdPool) != VK_SUCCESS) {
			VkLogErr("vkCreateCommandPool (frame %u) failed", i);
			return false;
		}

		VkCommandBufferAllocateInfo cbai{};
		cbai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		cbai.commandPool        = frames[i].cmdPool;
		cbai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		cbai.commandBufferCount = 1;
		if (vkAllocateCommandBuffers(ctx->Device(), &cbai, &frames[i].cmd) != VK_SUCCESS) {
			VkLogErr("vkAllocateCommandBuffers (frame %u) failed", i);
			return false;
		}

		if (vkCreateSemaphore(ctx->Device(), &sci, nullptr, &frames[i].imageAvailable) != VK_SUCCESS ||
			vkCreateFence(ctx->Device(), &fci, nullptr, &frames[i].inFlight) != VK_SUCCESS)
		{
			VkLogErr("Frame sync creation (frame %u) failed", i);
			return false;
		}
	}

	return true;
}

void VkSwapchain::DestroyFrameResources()
{
	if (!ctx || ctx->Device() == VK_NULL_HANDLE)
		return;

	for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
		if (frames[i].inFlight != VK_NULL_HANDLE) {
			vkDestroyFence(ctx->Device(), frames[i].inFlight, nullptr);
			frames[i].inFlight = VK_NULL_HANDLE;
		}
		if (frames[i].imageAvailable != VK_NULL_HANDLE) {
			vkDestroySemaphore(ctx->Device(), frames[i].imageAvailable, nullptr);
			frames[i].imageAvailable = VK_NULL_HANDLE;
		}
		if (frames[i].cmdPool != VK_NULL_HANDLE) {
			vkDestroyCommandPool(ctx->Device(), frames[i].cmdPool, nullptr);
			frames[i].cmdPool = VK_NULL_HANDLE;
			frames[i].cmd = VK_NULL_HANDLE;
		}
	}
}

void VkSwapchain::TransitionSceneLayout(
	VkCommandBuffer cmd,
	VkImageLayout newLayout,
	VkPipelineStageFlags2 srcStage,
	VkAccessFlags2 srcAccess,
	VkPipelineStageFlags2 dstStage,
	VkAccessFlags2 dstAccess)
{
	if (sceneLayout == newLayout)
		return;

	RecordImageBarrier(cmd, sceneImage, sceneLayout, newLayout,
		srcStage, srcAccess, dstStage, dstAccess);
	sceneLayout = newLayout;
}

bool VkSwapchain::BeginFrame(HWND hWnd)
{
	if (!ctx || !IsValid())
		return false;
	if (frameRecording)
		return true;

	if (hWnd && IsIconic(hWnd))
		return false;

	RECT rc{};
	if (hWnd && GetClientRect(hWnd, &rc)) {
		const uint32_t clientW = (rc.right > rc.left) ? (uint32_t)(rc.right - rc.left) : 0;
		const uint32_t clientH = (rc.bottom > rc.top) ? (uint32_t)(rc.bottom - rc.top) : 0;
		if (clientW == 0 || clientH == 0)
			return false;
		if (needsRecreate || clientW != swapchain.extent.width || clientH != swapchain.extent.height) {
			if (!Recreate(clientW, clientH))
				return false;
		}
	}

	FrameSync &f = frames[currentFrame];
	if (vkWaitForFences(ctx->Device(), 1, &f.inFlight, VK_TRUE, UINT64_MAX) != VK_SUCCESS)
		return false;

	VkResult acqRes = vkAcquireNextImageKHR(
		ctx->Device(), swapchain.swapchain, UINT64_MAX,
		f.imageAvailable, VK_NULL_HANDLE, &currentImageIndex);

	if (acqRes == VK_ERROR_OUT_OF_DATE_KHR) {
		if (rc.right > rc.left && rc.bottom > rc.top)
			Recreate((uint32_t)(rc.right - rc.left), (uint32_t)(rc.bottom - rc.top));
		return false;
	}
	if (acqRes == VK_SUBOPTIMAL_KHR) {
		needsRecreate = true;
	} else if (acqRes != VK_SUCCESS) {
		VkLogErr("vkAcquireNextImageKHR failed (VkResult=%d)", (int)acqRes);
		return false;
	}

	vkResetFences(ctx->Device(), 1, &f.inFlight);
	vkResetCommandPool(ctx->Device(), f.cmdPool, 0);

	VkCommandBufferBeginInfo cbbi{};
	cbbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	if (vkBeginCommandBuffer(f.cmd, &cbbi) != VK_SUCCESS)
		return false;

	frameRecording = true;
	frameReadyToPresent = false;

	const bool firstTouch = (sceneLayout == VK_IMAGE_LAYOUT_UNDEFINED);
	TransitionSceneLayout(
		f.cmd,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		firstTouch ? VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_2_TRANSFER_BIT,
		firstTouch ? 0 : VK_ACCESS_2_TRANSFER_READ_BIT,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
		VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT);

	if (firstTouch) {
		const VkClearColorValue initColor = { { 1.0f, 0.0f, 1.0f, 1.0f } };
		ClearScene(initColor);
	}

	return true;
}

void VkSwapchain::ClearScene(const VkClearColorValue &clearColor)
{
	if (!frameRecording || sceneView == VK_NULL_HANDLE)
		return;

	VkCommandBuffer cmd = frames[currentFrame].cmd;
	TransitionSceneLayout(
		cmd,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
		VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
		VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

	VkRenderingAttachmentInfo colorAtt{};
	colorAtt.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	colorAtt.imageView   = sceneView;
	colorAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	colorAtt.loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR;
	colorAtt.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
	colorAtt.clearValue.color = clearColor;

	VkRenderingInfo ri{};
	ri.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO;
	ri.renderArea.offset    = { 0, 0 };
	ri.renderArea.extent    = sceneExtent;
	ri.layerCount           = 1;
	ri.colorAttachmentCount = 1;
	ri.pColorAttachments    = &colorAtt;

	vkCmdBeginRendering(cmd, &ri);

	// Negative viewport height preserves Orbiter/D3D9 left-handed clip space and winding order
	VkViewport vp{};
	vp.x        = 0.0f;
	vp.y        = (float)sceneExtent.height;
	vp.width    = (float)sceneExtent.width;
	vp.height   = -(float)sceneExtent.height;
	vp.minDepth = 0.0f;
	vp.maxDepth = 1.0f;
	vkCmdSetViewport(cmd, 0, 1, &vp);

	VkRect2D scissor{ { 0, 0 }, sceneExtent };
	vkCmdSetScissor(cmd, 0, 1, &scissor);

	vkCmdEndRendering(cmd);
}

void VkSwapchain::EndFrame()
{
	if (!frameRecording)
		return;

	VkCommandBuffer cmd = frames[currentFrame].cmd;

	TransitionSceneLayout(
		cmd,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
		VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
		VK_PIPELINE_STAGE_2_TRANSFER_BIT,
		VK_ACCESS_2_TRANSFER_READ_BIT);

	VkImage dstImg = images[currentImageIndex];
	RecordImageBarrier(
		cmd,
		dstImg,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_PIPELINE_STAGE_2_TRANSFER_BIT,
		0,
		VK_PIPELINE_STAGE_2_TRANSFER_BIT,
		VK_ACCESS_2_TRANSFER_WRITE_BIT);

	VkImageBlit2 region{};
	region.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2;
	region.srcSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
	region.srcSubresource.mipLevel       = 0;
	region.srcSubresource.baseArrayLayer = 0;
	region.srcSubresource.layerCount     = 1;
	region.srcOffsets[0] = { 0, 0, 0 };
	region.srcOffsets[1] = { (int32_t)sceneExtent.width, (int32_t)sceneExtent.height, 1 };
	region.dstSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
	region.dstSubresource.mipLevel       = 0;
	region.dstSubresource.baseArrayLayer = 0;
	region.dstSubresource.layerCount     = 1;
	region.dstOffsets[0] = { 0, 0, 0 };
	region.dstOffsets[1] = { (int32_t)swapchain.extent.width, (int32_t)swapchain.extent.height, 1 };

	VkBlitImageInfo2 blitInfo{};
	blitInfo.sType          = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2;
	blitInfo.srcImage       = sceneImage;
	blitInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	blitInfo.dstImage       = dstImg;
	blitInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	blitInfo.regionCount    = 1;
	blitInfo.pRegions       = &region;
	blitInfo.filter         = VK_FILTER_LINEAR;
	vkCmdBlitImage2(cmd, &blitInfo);

	RecordImageBarrier(
		cmd,
		dstImg,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
		VK_PIPELINE_STAGE_2_TRANSFER_BIT,
		VK_ACCESS_2_TRANSFER_WRITE_BIT,
		VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
		0);

	vkEndCommandBuffer(cmd);
	frameRecording = false;
	frameReadyToPresent = true;
}

bool VkSwapchain::Present(HWND hWnd)
{
	if (!ctx || !IsValid())
		return false;

	// Tolerate un-bracketed clbkDisplayFrame() calls (e.g. splash screen or initial window clear)
	if (!frameReadyToPresent) {
		if (!frameRecording && !BeginFrame(hWnd))
			return false;
		EndFrame();
	}
	frameReadyToPresent = false;

	FrameSync &f = frames[currentFrame];
	VkSemaphore renderDone = renderFinishedSemaphores[currentImageIndex];

	VkSemaphoreSubmitInfo waitSem{};
	waitSem.sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
	waitSem.semaphore = f.imageAvailable;
	waitSem.stageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;

	VkCommandBufferSubmitInfo cmdInfo{};
	cmdInfo.sType         = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
	cmdInfo.commandBuffer = f.cmd;

	VkSemaphoreSubmitInfo sigSem{};
	sigSem.sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
	sigSem.semaphore = renderDone;
	sigSem.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

	VkSubmitInfo2 submit{};
	submit.sType                    = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
	submit.waitSemaphoreInfoCount   = 1;
	submit.pWaitSemaphoreInfos      = &waitSem;
	submit.commandBufferInfoCount   = 1;
	submit.pCommandBufferInfos      = &cmdInfo;
	submit.signalSemaphoreInfoCount = 1;
	submit.pSignalSemaphoreInfos    = &sigSem;

	VkResult subRes = vkQueueSubmit2(ctx->GraphicsQueue(), 1, &submit, f.inFlight);
	if (subRes != VK_SUCCESS) {
		VkLogErr("vkQueueSubmit2 failed (VkResult=%d)", (int)subRes);
		return false;
	}

	VkSwapchainKHR sc = swapchain.swapchain;
	VkPresentInfoKHR pi{};
	pi.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	pi.waitSemaphoreCount = 1;
	pi.pWaitSemaphores    = &renderDone;
	pi.swapchainCount     = 1;
	pi.pSwapchains        = &sc;
	pi.pImageIndices      = &currentImageIndex;

	VkResult presRes = vkQueuePresentKHR(ctx->PresentQueue(), &pi);
	currentFrame = (currentFrame + 1) % kMaxFramesInFlight;

	if (presRes == VK_ERROR_OUT_OF_DATE_KHR || presRes == VK_SUBOPTIMAL_KHR || needsRecreate) {
		RECT rc{};
		if (hWnd && !IsIconic(hWnd) && GetClientRect(hWnd, &rc) &&
			rc.right > rc.left && rc.bottom > rc.top)
		{
			Recreate((uint32_t)(rc.right - rc.left), (uint32_t)(rc.bottom - rc.top));
		}
	} else if (presRes != VK_SUCCESS) {
		VkLogErr("vkQueuePresentKHR failed (VkResult=%d)", (int)presRes);
		return false;
	}

	return true;
}
