module;

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan_core.h>

export module Nyx.RHI:Instance;

import Nyx.Core;
import Nyx.Platform;

import :Adapter;
import :Backend;
import :Forward;
import :Surface;
import :Types;

import :Types;

namespace Nyx::RHI {

export enum class[[= debug::derive]] ValidationMode : u8 {
  Disabled,
  Optional,
  Required,
};

export struct InstanceDescriptor {
  StringView applicationName{"Nyx Engine"};
  StringView engineName{"Nyx Engine"};

  ApiVersion applicationVersion{};
  ApiVersion engineVersion{};
  ApiVersion requestedApiVersion{.major = 1, .minor = 4, .patch = 0};

  ValidationMode validation{ValidationMode::Optional};
  DebugMessageSeverity debugSeverity{DebugMessageSeverity::Warning | DebugMessageSeverity::Error};

  Span<const StringView> requiredExtensions;
  Span<const StringView> optionalExtensions;
  Span<const StringView> requiredLayers;
  Span<const StringView> optionalLayers;
};

namespace detail {

struct VulkanInstanceState {
  VkInstance instance{};
  VkDebugUtilsMessengerEXT debugMessenger{};
  PFN_vkDestroyInstance destroyInstance{};
  PFN_vkDestroyDebugUtilsMessengerEXT destroyDebugUtilsMessenger{};
  PFN_vkDestroySurfaceKHR destroySurface{};
};

} // namespace detail

class Instance final {
public:
  static auto create(const Backend &backend, const InstanceDescriptor &desc = {}) -> Result<Instance>;

  ~Instance() noexcept;

  Instance(const Instance &) = delete ("Instance owns a VkInstance and cannot be copied.");
  auto operator=(const Instance &)
      -> Instance & = delete ("Instance owns a VkInstance and cannot be copied.");

  Instance(Instance &&) noexcept;
  auto operator=(Instance &&) noexcept -> Instance &;

  [[nodiscard]]
  auto nativeHandle() const noexcept -> VkInstance;

  [[nodiscard]]
  auto createSurface(const SurfaceDescriptor &desc) const noexcept -> Result<Surface>;

  [[nodiscard]]
  auto enumerateAdapters(Option<std::reference_wrapper<const Surface>> surface = None) const
      -> Result<Vec<Adapter>>;

private:
  friend class Surface;

  Instance() = default;

  detail::VulkanInstanceState state_;
};

} // namespace Nyx::RHI
