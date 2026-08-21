module;

#include <volk.h>
#include <vulkan/vulkan_core.h>

module Nyx.RHI;

import std;
import Miracle;

import :Surface;
import :Device;
import :Swapchain;
import :Vulkan;

using namespace Miracle;

namespace Nyx::RHI {

namespace {

constexpr auto chooseCompositeAlpha(VkCompositeAlphaFlagsKHR supported) noexcept
    -> VkCompositeAlphaFlagBitsKHR {
  constexpr Array<VkCompositeAlphaFlagBitsKHR, 4> candidates{
      VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
      VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
      VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
      VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
  };

  for (const VkCompositeAlphaFlagBitsKHR candidate : candidates) {
    if ((supported & candidate) != 0)
      return candidate;
  }

  return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
}

constexpr auto chooseExtent(const VkSurfaceCapabilitiesKHR &capabilities, Extent2D requested) noexcept
    -> Extent2D {
  if (capabilities.currentExtent.width != std::numeric_limits<u32>::max())
    return {
        .width = capabilities.currentExtent.width,
        .height = capabilities.currentExtent.height,
    };

  const u32 width = requested.width == 0 ? capabilities.minImageExtent.width : requested.width;
  const u32 height = requested.height == 0 ? capabilities.minImageExtent.height : requested.height;

  return {
      .width = std::clamp(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
      .height = std::clamp(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height),
  };
}

constexpr auto destroySwapchainState(detail::VulkanSwapchainState &state) noexcept -> void {
  if (state.device != VK_NULL_HANDLE) {
    for (const VkImageView imageView : state.imageViews) {
      if (imageView != VK_NULL_HANDLE)
        vkDestroyImageView(state.device, imageView, nullptr);
    }
  }

  state.imageViews.clear();
  state.images.clear();

  if (state.device != VK_NULL_HANDLE and state.swapchain != VK_NULL_HANDLE)
    vkDestroySwapchainKHR(state.device, state.swapchain, nullptr);

  state = {};
}

// NOLINTNEXTLINE(readability-function-size, readability-function-cognitive-complexity)
constexpr auto createSwapchainState(VkPhysicalDevice physicalDevice,
    VkDevice device,
    VkSurfaceKHR surface,
    const SwapchainDescriptor &desc,
    detail::VulkanSwapchainState &state) -> Result<void> {
  if (physicalDevice == VK_NULL_HANDLE or device == VK_NULL_HANDLE or surface == VK_NULL_HANDLE)
    return bail({"Cannot create a swapchain from an invalid Vulkan object."});

  VkSurfaceCapabilitiesKHR capabilities{};
  if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities) != VK_SUCCESS)
    return bail({"Failed to query Vulkan swapchain capabilities."});

  const Option<VkFormat> nativeFormat = detail::toVkFormat(desc.format);
  if (not nativeFormat)
    return bail({"Swapchain format has no Vulkan mapping."});

  u32 formatCount{};
  if (vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr) != VK_SUCCESS)
    return bail({"Failed to query Vulkan swapchain formats."});

  Vec<VkSurfaceFormatKHR> formats{formatCount};
  if (vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats.data()) !=
      VK_SUCCESS)
    return bail({"Failed to query Vulkan swapchain formats."});
  formats.resize(formatCount);

  auto filtered = formats | std::views::filter([&desc](VkSurfaceFormatKHR format) constexpr noexcept -> bool {
    return format.colorSpace == detail::toVkColorSpace(desc.colorSpace);
  });
  const auto selectedFormat =
      std::ranges::find_if(filtered, [nativeFormat](VkSurfaceFormatKHR format) constexpr noexcept -> bool {
        return format.format == VK_FORMAT_UNDEFINED or format.format == *nativeFormat;
      });

  if (selectedFormat == filtered.end())
    return bail({"Requested swapchain format is not supported by the surface."});

  u32 presentModeCount{};
  if (vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr) !=
      VK_SUCCESS)
    return bail({"Failed to query Vulkan present modes."});

  Vec<VkPresentModeKHR> presentModes{presentModeCount};
  if (vkGetPhysicalDeviceSurfacePresentModesKHR(
          physicalDevice, surface, &presentModeCount, presentModes.data()) != VK_SUCCESS)
    return bail({"Failed to query Vulkan present modes."});
  presentModes.resize(presentModeCount);

  const VkPresentModeKHR requestedPresentMode = detail::toVkPresentMode(desc.presentMode);
  auto findPresentMode = std::ranges::find_if(
      presentModes, [requestedPresentMode](const VkPresentModeKHR presentMode) constexpr noexcept -> bool {
        return presentMode == requestedPresentMode;
      });

  VkPresentModeKHR selectedPresentMode = VK_PRESENT_MODE_MAX_ENUM_KHR;
  if (findPresentMode != presentModes.end())
    selectedPresentMode = *findPresentMode;
  else {
    if (desc.presentMode != PresentMode::Fifo)
      selectedPresentMode = VK_PRESENT_MODE_FIFO_KHR;

    if (not std::ranges::any_of(presentModes,
            [selectedPresentMode](const VkPresentModeKHR presentMode) constexpr noexcept -> bool {
              return presentMode == selectedPresentMode;
            }))
      return bail({"Requested and fallback swapchain present modes are unavailable."});
  }

  const VkImageUsageFlags imageUsage = detail::toVkImageUsage(desc.usage);
  if (imageUsage == 0 or (capabilities.supportedUsageFlags & imageUsage) != imageUsage)
    return bail({"Requested swapchain image usage is not supported."});

  u32 imageCount = std::max(desc.minImageCount, capabilities.minImageCount);
  if (capabilities.maxImageCount != 0)
    imageCount = std::min(imageCount, capabilities.maxImageCount);

  const auto extent = chooseExtent(capabilities, desc.extent);
  const auto compositeAlpha = chooseCompositeAlpha(capabilities.supportedCompositeAlpha);

  VkSwapchainCreateInfoKHR createInfo{
      .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
      .surface = surface,
      .minImageCount = imageCount,
      .imageFormat = selectedFormat->format,
      .imageColorSpace = selectedFormat->colorSpace,
      .imageExtent =
          {
              .width = extent.width,
              .height = extent.height,
          },
      .imageArrayLayers = 1,
      .imageUsage = imageUsage,
      .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .preTransform = capabilities.currentTransform,
      .compositeAlpha = compositeAlpha,
      .presentMode = selectedPresentMode,
      .clipped = desc.clipped ? VK_TRUE : VK_FALSE,
  };

  state.physicalDevice = physicalDevice;
  state.device = device;
  state.surface = surface;

  if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &state.swapchain) != VK_SUCCESS)
    return bail({"Failed to create Vulkan swapchain."});

  u32 nativeImageCount{};
  if (vkGetSwapchainImagesKHR(device, state.swapchain, &nativeImageCount, nullptr) != VK_SUCCESS)
    return bail({"Failed to query Vulkan swapchain images."});

  state.images.resize(nativeImageCount);
  if (vkGetSwapchainImagesKHR(device, state.swapchain, &nativeImageCount, state.images.data()) != VK_SUCCESS)
    return bail({"Failed to query Vulkan swapchain images."});
  state.images.resize(nativeImageCount);

  state.imageViews.resize(state.images.size());
  for (usize index{}; index < state.images.size(); ++index) {
    const VkImageViewCreateInfo imageViewInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = state.images[index],
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = selectedFormat->format,
        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .layerCount = 1,
            },
    };

    if (vkCreateImageView(device, &imageViewInfo, nullptr, &state.imageViews[index]) != VK_SUCCESS)
      return bail({"Failed to create a Vulkan swapchain image view."});
  }

  state.info = {
      .extent = extent,
      .format =
          {
              .format = *detail::fromVkFormat(selectedFormat->format),
              .colorSpace = desc.colorSpace,
          },
      .presentMode = *detail::fromVkPresentMode(selectedPresentMode),
      .imageCount = static_cast<u32>(state.images.size()),
  };

  return {};
}

} // namespace

auto Swapchain::create(const Device &device, const Surface &surface, const SwapchainDescriptor &desc)
    -> Result<Swapchain> {
  if (device.state_.instance != surface.state_.instance)
    return bail({"Swapchain device and surface belong to different instances."});

  Swapchain swapchain{};
  Result<void> result = createSwapchainState(
      device.state_.physicalDevice, device.state_.device, surface.state_.surface, desc, swapchain.state_);
  if (not result)
    return bail(result.error().release());

  return swapchain;
}

Swapchain::Swapchain(Swapchain &&other) noexcept
    : state_(std::exchange(other.state_, {})) {
}

auto Swapchain::operator=(Swapchain &&other) noexcept -> Swapchain & {
  if (this == &other)
    return *this;

  if (state_.device != VK_NULL_HANDLE)
    static_cast<void>(vkDeviceWaitIdle(state_.device));
  destroySwapchainState(state_);
  state_ = std::exchange(other.state_, {});
  return *this;
}

Swapchain::~Swapchain() noexcept {
  if (state_.device != VK_NULL_HANDLE)
    static_cast<void>(vkDeviceWaitIdle(state_.device));
  destroySwapchainState(state_);
}

auto Swapchain::configure(const SwapchainDescriptor &desc) -> Result<void> {
  if (state_.device == VK_NULL_HANDLE or state_.physicalDevice == VK_NULL_HANDLE or
      state_.surface == VK_NULL_HANDLE)
    return bail({"Cannot configure an invalid Vulkan swapchain."});

  if (vkDeviceWaitIdle(state_.device) != VK_SUCCESS)
    return bail({"Failed to idle the device before recreating the swapchain."});

  const VkPhysicalDevice physicalDevice = state_.physicalDevice;
  const VkDevice device = state_.device;
  const VkSurfaceKHR surface = state_.surface;
  destroySwapchainState(state_);

  return createSwapchainState(physicalDevice, device, surface, desc, state_);
}

auto Swapchain::nativeHandle() const noexcept -> VkSwapchainKHR {
  return state_.swapchain;
}

auto Swapchain::deviceHandle() const noexcept -> VkDevice {
  return state_.device;
}

auto Swapchain::info() const noexcept -> const SwapchainInfo & {
  return state_.info;
}

auto Swapchain::imageCount() const noexcept -> u32 {
  return static_cast<u32>(state_.images.size());
}

auto Swapchain::nativeImage(u32 index) const noexcept -> Option<VkImage> {
  if (index >= state_.images.size())
    return None;

  return state_.images[index];
}

auto Swapchain::nativeImageView(u32 index) const noexcept -> Option<VkImageView> {
  if (index >= state_.imageViews.size())
    return None;

  return state_.imageViews[index];
}

} // namespace Nyx::RHI
