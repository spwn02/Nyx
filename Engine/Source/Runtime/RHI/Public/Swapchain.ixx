module;

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan_core.h>

export module Nyx.RHI:Swapchain;

import Miracle;

import :Forward;
import :Types;
import :Surface;
import :Device;

using namespace Miracle;

namespace Nyx::RHI {

export struct SwapchainDescriptor {
  String label;
  Extent2D extent{};
  u32 minImageCount{2};
  TextureFormat format{TextureFormat::Bgra8UnormSrgb};
  SurfaceColorSpace colorSpace{SurfaceColorSpace::SrgbNonlinear};
  PresentMode presentMode{PresentMode::Fifo};
  TextureUsage usage{TextureUsage::ColorAttachment};
  bool clipped{true};
};

export struct[[= debug::derive]] SwapchainInfo {
  Extent2D extent{};
  SurfaceFormat format{};
  PresentMode presentMode{PresentMode::Fifo};
  u32 imageCount{};
};

namespace detail {

struct VulkanSwapchainState {
  VkPhysicalDevice physicalDevice{};
  VkDevice device{};
  VkSurfaceKHR surface{};
  VkSwapchainKHR swapchain{};
  SwapchainInfo info;
  Vec<VkImage> images;
  Vec<VkImageView> imageViews;
};

} // namespace detail

export class Swapchain final {
public:
  static auto create(const Device &device, const Surface &surface, const SwapchainDescriptor &desc = {})
      -> Result<Swapchain>;

  ~Swapchain() noexcept;

  Swapchain(const Swapchain &) = delete (
      "Swapchain owns Vulkan images and image views and cannot be copied.");
  auto operator=(const Swapchain &)
      -> Swapchain & = delete ("Swapchain owns Vulkan images and image views and cannot be copied.");

  Swapchain(Swapchain &&) noexcept;
  auto operator=(Swapchain &&) noexcept -> Swapchain &;

  auto configure(const SwapchainDescriptor &desc) -> Result<void>;

  [[nodiscard]]
  auto nativeHandle() const noexcept -> VkSwapchainKHR;

  [[nodiscard]]
  auto deviceHandle() const noexcept -> VkDevice;

  [[nodiscard]]
  auto info() const noexcept -> const SwapchainInfo &;

  [[nodiscard]]
  auto imageCount() const noexcept -> u32;

  [[nodiscard]]
  auto nativeImage(u32 index) const noexcept -> Option<VkImage>;

  [[nodiscard]]
  auto nativeImageView(u32 index) const noexcept -> Option<VkImageView>;

private:
  friend class Device;

  Swapchain() = default;

  detail::VulkanSwapchainState state_;
};

} // namespace Nyx::RHI
