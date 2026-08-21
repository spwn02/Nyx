module;

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan_core.h>

export module Nyx.RHI:Surface;

import Miracle;
import Nyx.Platform;

import :Forward;
import :Types;

using namespace Miracle;

namespace Nyx::RHI {

export struct SurfaceDescriptor {
  Ref<const Window> window;
  StringView label;
};

export struct[[= debug::derive]] SurfaceCapabilities {
  u32 minImageCount{};
  u32 maxImageCount{};
  Extent2D currentExtent{};
  Extent2D minExtent{};
  Extent2D maxExtent{};
  TextureUsage supportedUsage{};
};

namespace detail {

struct VulkanSurfaceState {
  VkSurfaceKHR surface{};
  VkInstance instance{};
  PFN_vkDestroySurfaceKHR destroySurface{};
};

} // namespace detail

export class Surface final {
public:
  static auto create(const Instance &instance, const SurfaceDescriptor &desc) noexcept -> Result<Surface>;

  ~Surface() noexcept;

  Surface(const Surface &) = delete ("Surface owns a VkSurfaceKHR and cannot be copied.");
  auto operator=(const Surface &) -> Surface & = delete ("Surface owns a VkSurfaceKHR and cannot be copied.");

  Surface(Surface &&) noexcept;
  auto operator=(Surface &&) noexcept -> Surface &;

  [[nodiscard]]
  auto nativeHandle() const noexcept -> VkSurfaceKHR;

  [[nodiscard]]
  auto capabilities(const Adapter &adapter) const noexcept -> Result<SurfaceCapabilities>;

  [[nodiscard]]
  auto formats(const Adapter &adapter) const -> Result<Vec<SurfaceFormat>>;

  [[nodiscard]]
  auto presentModes(const Adapter &adapter) const -> Result<Vec<PresentMode>>;

private:
  friend class Instance;
  friend class Swapchain;

  Surface() = default;

  detail::VulkanSurfaceState state_;
};

} // namespace Nyx::RHI
