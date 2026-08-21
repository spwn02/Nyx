module;

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan_core.h>

export module Nyx.RHI:Backend;

import Miracle;

import :Types;

using namespace Miracle;

namespace Nyx::RHI {

export struct BackendDescriptor {
  BackendType type{BackendType::Vulkan};
};

export struct BackendCapabilities {
  BackendType type{BackendType::Vulkan};
  ApiVersion apiVersion{};
  FlatSet<String> instanceExtensions;
  FlatSet<String> layers;
};

namespace detail {

struct VulkanBackendState {
  PFN_vkGetInstanceProcAddr getInstanceProcAddr{};
  BackendCapabilities capabilities;
  bool loaderInitialized{};
};

} // namespace detail

export class Backend final {
public:
  static auto create(const BackendDescriptor &desc = {}) -> Result<Backend>;

  ~Backend() noexcept;

  Backend(const Backend &) = delete ("Backend owns process-level graphics runtime state.");
  auto operator=(const Backend &)
      -> Backend & = delete ("Backend owns process-level graphics runtime state.");

  Backend(Backend &&) noexcept;
  auto operator=(Backend &&) noexcept -> Backend &;

  [[nodiscard]]
  auto type() const noexcept -> BackendType;

  [[nodiscard]]
  auto capabilities() const noexcept -> const BackendCapabilities &;

private:
  Backend() = default;

  detail::VulkanBackendState state_;
};

}
