module;

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan_core.h>

export module Nyx.RHI:Adapter;

import Nyx.Core;

import :Forward;
import :Types;

namespace Nyx::RHI {

export struct[[= debug::derive]] QueueFamilyInfo {
  QueueCapabilities capabilities{};
  u32 queueCount{};
  bool supportsPresent{};
};

export struct AdapterInfo {
  String name;
  String driverName;
  u32 vendorId{};
  u32 deviceId{};
  AdapterType type{AdapterType::Other};
  ApiVersion apiVersion{};
  u64 localMemory{};
};

namespace detail {

struct VulkanAdapterState {
  VkPhysicalDevice physicalDevice{};
  VkInstance instance{};
  AdapterInfo info;
  Vec<QueueFamilyInfo> queueFamilies;
  FlatSet<String> extensions;
  DeviceFeatures supportedFeatures{};
  FlatMap<TextureFormat, TextureFormatFeatures> formatFeatures;
};

}

export class Adapter final {
public:
  Adapter(const Adapter &) = default;
  auto operator=(const Adapter &) -> Adapter & = default;

  Adapter(Adapter &&) noexcept = default;
  auto operator=(Adapter &&) noexcept -> Adapter & = default;

  ~Adapter() = default;

  [[nodiscard]]
  auto info() const noexcept -> const AdapterInfo &;

  [[nodiscard]]
  auto queueFamilies() const noexcept -> Span<const QueueFamilyInfo>;

  [[nodiscard]]
  auto formatFeatures(TextureFormat format) const noexcept -> TextureFormatFeatures;

  [[nodiscard]]
  auto supports(const DeviceDescriptor &desc) const noexcept -> bool;

  [[nodiscard]]
  auto nativeHandle() const noexcept -> VkPhysicalDevice;

private:
  friend class Instance;
  friend class Surface;
  friend class Device;

  Adapter() = default;

  detail::VulkanAdapterState state_;
};

} // namespace Nyx::RHI
