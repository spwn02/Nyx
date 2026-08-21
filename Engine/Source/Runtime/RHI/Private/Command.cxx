module;

#include <volk.h>
#include <vulkan/vulkan_core.h>

module Nyx.RHI;

import std;
import Miracle;

import :Command;
import :Vulkan;

using namespace Miracle;

namespace Nyx::RHI {

auto CommandBuffer::resolveImage(const ImageRef &image) const noexcept -> Result<VkImage> {
  if (device_ == nullptr)
    return bail({"Cannot resolve an image without an owning device."});

  if (image.texture.valid()) {
    if (image.swapchainImage.has_value())
      return bail({"An image reference cannot contain both a texture and a swapchain image."});

    const Option<VkImage> nativeImage = device_->nativeImage(image.texture);
    if (not nativeImage)
      return bail({"Image reference contains an invalid texture handle."});

    return *nativeImage;
  }

  if (not image.swapchainImage.has_value() or not image.swapchainImage->valid())
    return bail({"Image reference does not identify a texture of swapchain image."});

  const SwapchainImageRef &swapchainImage = *image.swapchainImage;
  if (swapchainImage.swapchain->deviceHandle() != device_->nativeHandle())
    return bail({"Swapchain image belongs to a different device."});

  const Option<VkImage> nativeImage = swapchainImage.swapchain->nativeImage(swapchainImage.index);
  if (not nativeImage)
    return bail({"Swapchain image index is outside the swapchain."});

  return *nativeImage;
}

auto CommandBuffer::resolveImageView(const ImageViewRef &view) const noexcept -> Result<VkImageView> {
  if (device_ == nullptr)
    return bail({"Cannot resolve an image view without an owning device."});

  if (view.textureView.valid()) {
    if (view.swapchainImage.has_value())
      return bail({"An image-view reference cannot contain both a texture view and a swapchain image."});

    const Option<VkImageView> nativeImageView = device_->nativeImageView(view.textureView);
    if (not nativeImageView)
      return bail({"Image-view reference contains an invalid texture-view handle."});

    return *nativeImageView;
  }

  if (not view.swapchainImage.has_value() or not view.swapchainImage->valid())
    return bail({"Image-view reference does not identify a texture view or swapchain image."});

  const SwapchainImageRef &swapchainImage = *view.swapchainImage;
  if (swapchainImage.swapchain->deviceHandle() != device_->nativeHandle())
    return bail({"Swapchain image view belongs to a different device."});

  const Option<VkImageView> nativeImageView = swapchainImage.swapchain->nativeImageView(swapchainImage.index);
  if (not nativeImageView)
    return bail({"Swapchain  image-view index is outside the swapchain."});

  return *nativeImageView;
}

auto CommandBuffer::makeRenderingAttachment(const RenderingAttachmentDescriptor &desc,
    bool depthStencil) const noexcept -> Result<VkRenderingAttachmentInfo> {
  if (desc.layout == TextureLayout::Undefined)
    return bail({"Dynamic-rendering attachments require an explicit image layout."});

  Result<VkImageView> nativeImageView = resolveImageView(desc.view);
  if (not nativeImageView)
    return bail{nativeImageView.error().release()};

  if (desc.clearColor.has_value() and desc.clearDepthStencil.has_value())
    return bail({"A rendering attachment cannot contain both color and depth-stencil clear values."});

  if (depthStencil and desc.clearColor.has_value())
    return bail({"A depth-stencil attachment cannot contain a color clear value."});

  if (not depthStencil and desc.clearDepthStencil.has_value())
    return bail({"A color attachment cannot contain a depth-stencil clear value."});

  if (desc.loadOp == LoadOp::Clear) {
    if (depthStencil and not desc.clearDepthStencil.has_value())
      return bail({"A cleared depth-stencil attachment requires a clear value."});

    if (not depthStencil and not desc.clearColor.has_value())
      return bail({"A cleared color attachment requires a clear value."});
  }

  if (desc.loadOp != LoadOp::Clear and (desc.clearColor.has_value() or desc.clearDepthStencil.has_value()))
    return bail({"Clear values are only valid when the attachment load operation is Clear."});

  VkClearValue clearValue{};
  if (desc.clearColor.has_value()) {
    clearValue.color = {
        .float32 =
            {
                desc.clearColor->red,
                desc.clearColor->green,
                desc.clearColor->blue,
                desc.clearColor->alpha,
            },
    };
  }

  if (desc.clearDepthStencil.has_value()) {
    clearValue.depthStencil.depth = desc.clearDepthStencil->depth;
    clearValue.depthStencil.stencil = desc.clearDepthStencil->stencil;
  }

  return VkRenderingAttachmentInfo{
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = *nativeImageView,
      .imageLayout = detail::toVkImageLayout(desc.layout),
      .loadOp = detail::toVkLoadOp(desc.loadOp),
      .storeOp = detail::toVkStoreOp(desc.storeOp),
      .clearValue = clearValue,
  };
}

auto CommandBuffer::pipelineBarrier(Span<const ImageBarrier> barriers) -> Result<void> {
  if (not recording_)
    return bail({"Cannot record an image barrier on a finished command buffer."});

  if (device_ == nullptr or not has(device_->features(), DeviceFeature::Synchronization2))
    return bail({"Image barriers require Vulkan synchronization2."});

  if (rendering_)
    return bail({"Image barriers must be recorded outside dynamic rendering."});

  if (barriers.empty())
    return {};

  Vec<VkImageMemoryBarrier2> nativeBarriers;
  nativeBarriers.reserve(barriers.size());
  for (const ImageBarrier &barrier : barriers) {
    Result<VkImage> image = resolveImage(barrier.image);
    if (not image)
      return bail(image.error().release());

    const VkImageAspectFlags aspectMask = detail::toVkImageAspect(barrier.subresourceRange.aspects);
    if (aspectMask == 0)
      return bail({"Image barriers require at least one image aspect."});

    nativeBarriers.push_back({
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = detail::toVkPipelineStage(barrier.srcStage),
        .srcAccessMask = detail::toVkAccess(barrier.srcAccess),
        .dstStageMask = detail::toVkPipelineStage(barrier.dstStage),
        .dstAccessMask = detail::toVkAccess(barrier.dstAccess),
        .oldLayout = detail::toVkImageLayout(barrier.oldLayout),
        .newLayout = detail::toVkImageLayout(barrier.newLayout),
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = *image,
        .subresourceRange =
            {
                .aspectMask = aspectMask,
                .baseMipLevel = barrier.subresourceRange.baseMipLevel,
                .levelCount = barrier.subresourceRange.levelCount == 0 ? VK_REMAINING_MIP_LEVELS
                                                                       : barrier.subresourceRange.levelCount,
                .baseArrayLayer = barrier.subresourceRange.baseArrayLevel,
                .layerCount = barrier.subresourceRange.layerCount == 0 ? VK_REMAINING_ARRAY_LAYERS
                                                                       : barrier.subresourceRange.layerCount,
            },
    });
  }

  const VkDependencyInfo dependencyInfo{
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .imageMemoryBarrierCount = static_cast<u32>(nativeBarriers.size()),
      .pImageMemoryBarriers = nativeBarriers.data(),
  };
  vkCmdPipelineBarrier2(commandBuffer_, &dependencyInfo);
  return {};
}

auto CommandBuffer::beginRendering(const RenderingDescriptor &desc) -> Result<void> {
  if (not recording_)
    return bail({"Cannot begin rendering on a finished command buffer."});

  if (device_ == nullptr or not has(device_->features(), DeviceFeature::DynamicRendering))
    return bail({"Dynamic rendering is not enabled on the device."});

  if (rendering_)
    return bail({"Dynamic rendering is already active on this command buffer."});

  if (desc.renderArea.extent.width == 0 or desc.renderArea.extent.height == 0)
    return bail({"Dynamic rendering requires a non-empty render area."});

  if (desc.layerCount == 0)
    return bail({"Dynamic rendering requires at least one layer."});

  if (desc.colorAttachments.empty() and not desc.depthAttachment.has_value() and
      not desc.stencilAttachment.has_value())
    return bail({"Dynamic rendering requires at least one attachment."});

  Vec<VkRenderingAttachmentInfo> colorAttachments;
  colorAttachments.reserve(desc.colorAttachments.size());
  for (const RenderingAttachmentDescriptor &attachment : desc.colorAttachments) {
    Result<VkRenderingAttachmentInfo> nativeAttachment = makeRenderingAttachment(attachment, false);
    if (not nativeAttachment)
      return bail(nativeAttachment.error().release());

    colorAttachments.push_back(*nativeAttachment);
  }

  Option<VkRenderingAttachmentInfo> depthAttachment;
  if (desc.depthAttachment.has_value()) {
    Result<VkRenderingAttachmentInfo> nativeAttachment = makeRenderingAttachment(*desc.depthAttachment, true);
    if (not nativeAttachment)
      return bail(nativeAttachment.error().release());

    depthAttachment = *nativeAttachment;
  }

  Option<VkRenderingAttachmentInfo> stencilAttachment;
  if (desc.stencilAttachment.has_value()) {
    Result<VkRenderingAttachmentInfo> nativeAttachment =
        makeRenderingAttachment(*desc.stencilAttachment, true);
    if (not nativeAttachment)
      return bail(nativeAttachment.error().release());

    stencilAttachment = *nativeAttachment;
  }

  const VkRenderingInfo renderingInfo{
      .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .renderArea =
          {
              .offset =
                  {
                      .x = desc.renderArea.offset.x,
                      .y = desc.renderArea.offset.y,
                  },
              .extent =
                  {
                      .width = desc.renderArea.extent.width,
                      .height = desc.renderArea.extent.height,
                  },
          },
      .layerCount = desc.layerCount,
      .viewMask = desc.viewMask,
      .colorAttachmentCount = static_cast<u32>(colorAttachments.size()),
      .pColorAttachments = colorAttachments.data(),
      .pDepthAttachment = depthAttachment.has_value() ? &*depthAttachment : nullptr,
      .pStencilAttachment = stencilAttachment.has_value() ? &*stencilAttachment : nullptr,
  };

  vkCmdBeginRendering(commandBuffer_, &renderingInfo);
  rendering_ = true;
  return {};
}

auto CommandBuffer::endRendering() noexcept -> Result<void> {
  if (not recording_)
    return bail({"Cannot end rendering on a finished command buffer."});

  if (not rendering_)
    return bail({"Dynamic rendering is not active on this command buffer."});

  vkCmdEndRendering(commandBuffer_);
  rendering_ = false;
  return {};
}

auto CommandBuffer::setViewport(u32 firstViewport, const Viewport &viewport) -> Result<void> {
  return setViewports(firstViewport, Span<const Viewport>{&viewport, 1});
}

auto CommandBuffer::setViewports(u32 firstViewport, Span<const Viewport> viewports) -> Result<void> {
  if (not recording_)
    return bail({"Cannot record a viewport on a finished command buffer."});

  if (viewports.empty())
    return bail({"At least one viewport is required."});

  Vec<VkViewport> nativeViewports =
      viewports | std::views::transform([](const Viewport &viewport) constexpr noexcept -> VkViewport {
        return {
            .x = viewport.x,
            .y = viewport.y,
            .width = viewport.width,
            .height = viewport.height,
            .minDepth = viewport.minDepth,
            .maxDepth = viewport.maxDepth,
        };
      }) |
      std::ranges::to<Vec<VkViewport>>();

  vkCmdSetViewport(
      commandBuffer_, firstViewport, static_cast<u32>(nativeViewports.size()), nativeViewports.data());
  return {};
}

auto CommandBuffer::setScissor(u32 firstScissor, const Scissor &scissor) -> Result<void> {
  return setScissors(firstScissor, Span<const Scissor>{&scissor, 1});
}

auto CommandBuffer::setScissors(u32 firstScissor, Span<const Scissor> scissors) -> Result<void> {
  if (not recording_)
    return bail({"Cannot record a scissor on a finished command buffer."});

  if (scissors.empty())
    return bail({"At least one scissor is required."});

  constexpr auto toVkRect2D = [](const Scissor &scissor) constexpr noexcept -> VkRect2D {
    return {
        .offset =
            {
                .x = scissor.offset.x,
                .y = scissor.offset.y,
            },
        .extent =
            {
                .width = scissor.extent.width,
                .height = scissor.extent.height,
            },
    };
  };

  Vec<VkRect2D> nativeScissors =
      scissors | std::views::transform(toVkRect2D) | std::ranges::to<Vec<VkRect2D>>();

  vkCmdSetScissor(
      commandBuffer_, firstScissor, static_cast<u32>(nativeScissors.size()), nativeScissors.data());
  return {};
}

auto CommandBuffer::bindPipeline(const PipelineBinding &binding) noexcept -> Result<void> {
  if (not recording_)
    return bail({"Cannot bind a pipeline on a finished command buffer."});

  if (device_ == nullptr)
    return bail({"Cannot bind a pipeline without an owning device."});

  if (not binding.pipeline.valid())
    return bail({"Cannot bind an invalid pipeline handle."});

  Result<Ref<const Pipeline>> pipelineResult = device_->pipeline(binding.pipeline);
  if (not pipelineResult)
    return bail(pipelineResult.error().release());

  if (pipelineResult->get().descriptor().bindPoint != binding.bindPoint)
    return bail({"Pipeline binding point does not match the pipeline descriptor."});

  const Option<VkPipeline> nativePipeline = device_->nativePipeline(binding.pipeline);
  if (not nativePipeline)
    return bail({"Pipeline handle has no Vulkan pipeline state."});

  vkCmdBindPipeline(commandBuffer_, detail::toVkPipelineBindPoint(binding.bindPoint), *nativePipeline);
  return {};
}

auto CommandBuffer::nativeHandle() const noexcept -> VkCommandBuffer {
  return commandBuffer_;
}

auto CommandBuffer::recording() const noexcept -> bool {
  return recording_;
}

auto CommandBuffer::rendering() const noexcept -> bool {
  return rendering_;
}

} // namespace Nyx::RHI
