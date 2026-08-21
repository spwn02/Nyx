module;

#include <volk.h>

module Nyx.RHI;

import std;
import Miracle;

import :Adapter;

using namespace Miracle;

namespace Nyx::RHI {

auto Adapter::info() const noexcept -> const AdapterInfo & {
  return state_.info;
}

auto Adapter::queueFamilies() const noexcept -> Span<const QueueFamilyInfo> {
  return state_.queueFamilies;
}

auto Adapter::formatFeatures(TextureFormat format) const noexcept -> TextureFormatFeatures {
  const auto iterator = state_.formatFeatures.find(format);
  if (iterator == state_.formatFeatures.end())
    return {};

  return iterator->second;
}

auto Adapter::supports(const DeviceDescriptor &desc) const noexcept -> bool {
  if (state_.physicalDevice == VK_NULL_HANDLE)
    return false;

  const auto requiredFeatures = bits(desc.requiredFeatures);
  const auto supportedFeatures = bits(state_.supportedFeatures);

  if ((requiredFeatures & supportedFeatures) != requiredFeatures)
    return false;

  for (const StringView extension : desc.requiredExtensions)
    if (not state_.extensions.contains(String{extension}))
      return false;

  for (const QueueRequest &request : desc.queues) {
    if (request.count == 0)
      return false;

    const auto requestCapabilities = bits(request.required | detail::queueRoleCapabilities(request.role));

    const bool familyAvailable =
        std::ranges::any_of(state_.queueFamilies, [&](const QueueFamilyInfo &family) -> bool {
          const auto familyCapabilities = bits(family.capabilities);

          return family.queueCount >= request.count and
                 (familyCapabilities & requestCapabilities) == requestCapabilities and
                 (not request.presentSurface.has_value() or family.supportsPresent);
        });

    if (not familyAvailable)
      return false;
  }

  return true;
}

auto Adapter::nativeHandle() const noexcept -> VkPhysicalDevice {
  return state_.physicalDevice;
}

}
