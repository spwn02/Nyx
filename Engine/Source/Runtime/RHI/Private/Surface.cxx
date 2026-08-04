module;

#define VK_NO_PROTOTYPES
#include <SDL3/SDL_vulkan.h>

#include <volk.h>
#include <vulkan/vulkan_core.h>

module Nyx.RHI;

import std;
import Nyx.Core;

import :Adapter;
import :Instance;
import :Surface;
import :Vulkan;

namespace Nyx::RHI {

namespace {

constexpr auto destroySurfaceState(detail::VulkanSurfaceState &state) noexcept -> void {
  if (state.surface != VK_NULL_HANDLE and state.destroySurface != nullptr)
    state.destroySurface(state.instance, state.surface, nullptr);

  state = {};
}

} // namespace

auto Surface::create(const Instance &instance, const SurfaceDescriptor &desc) noexcept -> Result<Surface> {
  if (instance.nativeHandle() == VK_NULL_HANDLE)
    return bail({"Cannot create a surface from an invalid Vulkan instance."});

  if (desc.window.get().nativeHandle() == nullptr)
    return bail({"Cannot create a Vulkan surface from a null window."});

  if (instance.state_.destroySurface == nullptr)
    return bail({"Vulkan surface destruction is unavailable."});

  Surface surface{};
  surface.state_.instance = instance.state_.instance;
  surface.state_.destroySurface = instance.state_.destroySurface;

  if (not SDL_Vulkan_CreateSurface(
          desc.window.get().nativeHandle(), instance.nativeHandle(), nullptr, &surface.state_.surface))
    return bail({"Failed to create Vulkan surface through SDL."});

  return surface;
}

Surface::Surface(Surface &&other) noexcept
    : state_(std::exchange(other.state_, {})) {
}

auto Surface::operator=(Surface &&other) noexcept -> Surface & {
  if (this == &other)
    return *this;

  destroySurfaceState(state_);
  state_ = std::exchange(other.state_, {});
  return *this;
}

Surface::~Surface() noexcept {
  destroySurfaceState(state_);
}

auto Surface::nativeHandle() const noexcept -> VkSurfaceKHR {
  return state_.surface;
}

auto Surface::capabilities(const Adapter &adapter) const noexcept -> Result<SurfaceCapabilities> {
  if (state_.surface == VK_NULL_HANDLE)
    return bail({"Cannot query capabilities from an invalid surface."});

  if (adapter.nativeHandle() == VK_NULL_HANDLE)
    return bail({"Cannot query surface capabilities from an invalid adapter."});

  if (adapter.state_.instance != state_.instance)
    return bail({"Surface and adapter belong to a different Vulkan instances."});

  VkSurfaceCapabilitiesKHR capabilities{};
  if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(adapter.nativeHandle(), state_.surface, &capabilities) !=
      VK_SUCCESS)
    return bail({"Failed to query Vulkan surface capabilities."});

  return SurfaceCapabilities{
      .minImageCount = capabilities.minImageCount,
      .maxImageCount = capabilities.maxImageCount,
      .currentExtent =
          {
              .width = capabilities.currentExtent.width,
              .height = capabilities.currentExtent.height,
          },
      .minExtent =
          {
              .width = capabilities.minImageExtent.width,
              .height = capabilities.minImageExtent.height,
          },
      .maxExtent =
          {
              .width = capabilities.maxImageExtent.width,
              .height = capabilities.maxImageExtent.height,
          },
      .supportedUsage = detail::makeTextureUsage(capabilities.supportedUsageFlags),
  };
}

auto Surface::formats(const Adapter &adapter) const -> Result<Vec<SurfaceFormat>> {
  if (state_.surface == VK_NULL_HANDLE)
    return bail({"Cannot query formats from an invalid surface."});

  if (adapter.nativeHandle() == VK_NULL_HANDLE)
    return bail({"Cannot query surface formats from an invalid adapter."});

  if (adapter.state_.instance != state_.instance)
    return bail({"Surface and adapter belong to different Vulkan instances."});

  u32 count{};
  if (vkGetPhysicalDeviceSurfaceFormatsKHR(adapter.nativeHandle(), state_.surface, &count, nullptr) !=
      VK_SUCCESS)
    return bail({"Failed to query Vulkan surface formats."});

  Vec<VkSurfaceFormatKHR> nativeFormats{count};
  if (vkGetPhysicalDeviceSurfaceFormatsKHR(
          adapter.nativeHandle(), state_.surface, &count, nativeFormats.data()) != VK_SUCCESS)
    return bail({"Failed to query Vulkan surface formats."});
  nativeFormats.resize(count);

  Vec<SurfaceFormat> result;
  result.reserve(nativeFormats.size());

  for (const VkSurfaceFormatKHR &nativeFormat : nativeFormats) {
    const auto colorSpace = detail::fromVkColorSpace(nativeFormat.colorSpace);
    if (not colorSpace)
      continue;

    const Option<TextureFormat> format = nativeFormat.format == VK_FORMAT_UNDEFINED
                                             ? Option<TextureFormat>{TextureFormat::Bgra8UnormSrgb}
                                             : detail::fromVkFormat(nativeFormat.format);

    if (format)
      result.push_back({
          .format = *format,
          .colorSpace = *colorSpace,
      });
  }

  if (result.empty())
    return bail({"The surface exposes no formats supported by the RHI."});

  return result;
}

auto Surface::presentModes(const Adapter &adapter) const -> Result<Vec<PresentMode>> {
  if (state_.surface == VK_NULL_HANDLE)
    return bail({"Cannot query present modes from an invalid surface."});

  if (adapter.nativeHandle() == VK_NULL_HANDLE)
    return bail({"Cannot query present modes from an invalid adapter."});

  if (adapter.state_.instance != state_.instance)
    return bail({"Surface and adapter belong to different Vulkan instances."});

  u32 count{};
  if (vkGetPhysicalDeviceSurfacePresentModesKHR(adapter.nativeHandle(), state_.surface, &count, nullptr) !=
      VK_SUCCESS)
    return bail({"Failed to query Vulkan present modes."});

  Vec<VkPresentModeKHR> nativeModes{count};
  if (vkGetPhysicalDeviceSurfacePresentModesKHR(
          adapter.nativeHandle(), state_.surface, &count, nativeModes.data()) != VK_SUCCESS)
    return bail({"Failed to query Vulkan present modes."});
  nativeModes.resize(count);

  Vec<PresentMode> result =
      nativeModes |
      std::views::transform([](VkPresentModeKHR mode) constexpr noexcept -> Option<PresentMode> {
        return detail::fromVkPresentMode(mode);
      }) |
      std::views::filter(
          [](const Option<PresentMode> &mode) constexpr noexcept -> bool { return mode.has_value(); }) |
      std::views::transform(
          [](Option<PresentMode> mode) constexpr noexcept -> PresentMode { return *mode; }) |
      std::ranges::to<Vec<PresentMode>>();

  if (result.empty())
    return bail({"The surface exposes no present modes supported by the RHI."});

  return result;
}

} // namespace Nyx::RHI
