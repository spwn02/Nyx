module;

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan_core.h>

export module Nyx.RHI:Command;

import Nyx.Core;
import :Device;
import :Forward;
import :Resources;
import :Swapchain;
import :Types;

export namespace Nyx::RHI {

struct SwapchainImageRef {
  const Swapchain *swapchain{};
  u32 index{};

  [[nodiscard]]
  constexpr auto valid() const noexcept -> bool {
    return swapchain != nullptr;
  }
};

struct ImageRef {
  TextureHandle texture;
  Option<SwapchainImageRef> swapchainImage;

  [[nodiscard]]
  constexpr auto valid() const noexcept -> bool {
    if (texture.valid())
      return not swapchainImage.has_value();

    return swapchainImage.has_value() and swapchainImage->valid();
  }
};

struct ImageViewRef {
  TextureViewHandle textureView;
  Option<SwapchainImageRef> swapchainImage;

  [[nodiscard]]
  constexpr auto valid() const noexcept -> bool {
    if (textureView.valid())
      return not swapchainImage.has_value();

    return swapchainImage.has_value() and swapchainImage->valid();
  }
};

constexpr auto textureImage(TextureHandle texture) noexcept -> ImageRef {
  return ImageRef{.texture = texture};
}

constexpr auto swapchainImage(const Swapchain &swapchain, u32 index) noexcept -> ImageRef {
  return ImageRef{
      .swapchainImage =
          SwapchainImageRef{
              .swapchain = &swapchain,
              .index = index,
          },
  };
}

constexpr auto textureView(TextureViewHandle view) noexcept -> ImageViewRef {
  return ImageViewRef{.textureView = view};
}

constexpr auto swapchainImageView(const Swapchain &swapchain, u32 index) noexcept -> ImageViewRef {
  return ImageViewRef{
      .swapchainImage =
          SwapchainImageRef{
              .swapchain = &swapchain,
              .index = index,
          },
  };
}

struct ImageBarrier {
  ImageRef image;
  TextureSubresourceRange subresourceRange{};
  PipelineStage srcStage{};
  Access srcAccess{};
  TextureLayout oldLayout{TextureLayout::Undefined};
  PipelineStage dstStage{};
  Access dstAccess{};
  TextureLayout newLayout{TextureLayout::Undefined};
};

struct ClearColor {
  f32 red{}, green{}, blue{}, alpha{};
};

struct ClearDepthStencil {
  f32 depth{1.0F};
  u32 stencil{};
};

struct RenderingAttachmentDescriptor {
  ImageViewRef view;
  TextureLayout layout{TextureLayout::Undefined};
  LoadOp loadOp{LoadOp::Load};
  StoreOp storeOp{StoreOp::Store};
  Option<ClearColor> clearColor;
  Option<ClearDepthStencil> clearDepthStencil;
};

struct RenderingDescriptor {
  Rect2D renderArea{};
  u32 layerCount{1};
  u32 viewMask{};
  Span<const RenderingAttachmentDescriptor> colorAttachments;
  Option<RenderingAttachmentDescriptor> depthAttachment;
  Option<RenderingAttachmentDescriptor> stencilAttachment;
};

struct Viewport {
  f32 x{}, y{};
  f32 width{}, height{};
  f32 minDepth{}, maxDepth{1.0F};
};

struct Scissor {
  Offset2D offset{};
  Extent2D extent{};
};

struct PipelineBinding {
  PipelineHandle pipeline;
  PipelineBindPoint bindPoint{PipelineBindPoint::Graphics};
};

class CommandBuffer final {
public:
  [[nodiscard]]
  auto nativeHandle() const noexcept -> VkCommandBuffer;

  [[nodiscard]]
  auto recording() const noexcept -> bool;

  [[nodiscard]]
  auto rendering() const noexcept -> bool;

  auto pipelineBarrier(Span<const ImageBarrier> barriers) -> Result<void>;

  auto beginRendering(const RenderingDescriptor &desc) -> Result<void>;

  auto endRendering() noexcept -> Result<void>;

  auto setViewport(u32 firstViewport, const Viewport &viewport) -> Result<void>;

  auto setViewports(u32 firstViewport, Span<const Viewport> viewports) -> Result<void>;

  auto setScissor(u32 firstScissor, const Scissor &scissor) -> Result<void>;

  auto setScissors(u32 firstScissor, Span<const Scissor> scissors) -> Result<void>;

  auto bindPipeline(const PipelineBinding &binding) noexcept -> Result<void>;

private:
  friend class Frame;
  friend class FrameScheduler;

  CommandBuffer() = default;

  explicit CommandBuffer(VkCommandBuffer commandBuffer, const Device *device) noexcept
      : device_(device)
      , commandBuffer_(commandBuffer)
      , recording_(true) {
  }

  [[nodiscard]]
  auto resolveImage(const ImageRef &image) const noexcept -> Result<VkImage>;

  [[nodiscard]]
  auto resolveImageView(const ImageViewRef &view) const noexcept -> Result<VkImageView>;

  [[nodiscard]]
  auto makeRenderingAttachment(const RenderingAttachmentDescriptor &desc, bool depthStencil) const noexcept
      -> Result<VkRenderingAttachmentInfo>;

  const Device *device_{};
  VkCommandBuffer commandBuffer_{};
  bool recording_{};
  bool rendering_{};
};

} // namespace Nyx::RHI
