module;

#define VK_NO_PROTOTYPES
#define VOLK_IMPLEMENTATION
#include <volk.h>

#include <vulkan/vulkan_core.h>

module Nyx.RHI;

import std;
import Nyx.Core;

import :Backend;
import :Vulkan;

namespace Nyx::RHI {

namespace {

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables, readability-identifier-naming)
bool loaderOwned_{};

constexpr auto enumerateInstanceExtensions() -> Result<FlatSet<String>> {
  u32 count{};
  if (vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr) != VK_SUCCESS)
    return bail({"Failed to enumerate Vulkan instance extensions."});

  Vec<VkExtensionProperties> properties{count};
  if (vkEnumerateInstanceExtensionProperties(nullptr, &count, properties.data()) != VK_SUCCESS)
    return bail({"Failed to enumerate Vulkan instance extensions"});
  properties.resize(count);

  return properties |
         std::views::transform([](const VkExtensionProperties &props) constexpr noexcept -> const char * {
           return static_cast<const char *>(props.extensionName);
         }) |
         std::ranges::to<FlatSet<String>>();
}

constexpr auto enumerateInstanceLayers() -> Result<FlatSet<String>> {
  u32 count{};
  if (vkEnumerateInstanceLayerProperties(&count, nullptr) != VK_SUCCESS)
    return bail({"Failed to enumerate Vulkan instance layers."});

  Vec<VkLayerProperties> properties{count};
  if (vkEnumerateInstanceLayerProperties(&count, properties.data()) != VK_SUCCESS)
    return bail({"Failed to enumerate Vulkan instance layers."});
  properties.resize(count);

  return properties |
         std::views::transform([](const VkLayerProperties &props) constexpr noexcept -> const char * {
           return static_cast<const char *>(props.layerName);
         }) |
         std::ranges::to<FlatSet<String>>();
}

} // namespace

auto Backend::create(const BackendDescriptor &desc) -> Result<Backend> {
  if (desc.type != BackendType::Vulkan)
    return bail({"The requested graphics backend is not supported."});

  if (loaderOwned_)
    return bail({"A Vulkan backend is already active in this process."});

  if (volkInitialize() != VK_SUCCESS)
    return bail({"Failed to initialize the Vulkan loader."});

  Backend backend{};
  backend.state_.loaderInitialized = true;
  loaderOwned_ = true;

  auto finalizeOnFailure = std::scope_exit([&backend] -> void {
    if (backend.state_.loaderInitialized) {
      volkFinalize();
      loaderOwned_ = false;
    }
  });

  u32 apiVersion = volkGetInstanceVersion();
  if (apiVersion == 0)
    apiVersion = VK_VERSION_1_0;

  Result<FlatSet<String>> extensions = enumerateInstanceExtensions();
  if (not extensions)
    return bail(extensions.error().release());

  Result<FlatSet<String>> layers = enumerateInstanceLayers();
  if (not layers)
    return bail(layers.error().release());

  backend.state_.getInstanceProcAddr = vkGetInstanceProcAddr;
  backend.state_.capabilities = {
      .type = desc.type,
      .apiVersion = detail::fromVkApiVersion(apiVersion),
      .instanceExtensions = std::move(*extensions),
      .layers = std::move(*layers),
  };

  finalizeOnFailure.release();
  return backend;
}

Backend::Backend(Backend &&other) noexcept
    : state_(std::exchange(other.state_, {})) {
}

auto Backend::operator=(Backend &&other) noexcept -> Backend & {
  if (this == &other)
    return *this;

  if (state_.loaderInitialized) {
    volkFinalize();
    loaderOwned_ = false;
  }

  state_ = std::exchange(other.state_, {});
  return *this;
}

Backend::~Backend() noexcept {
  if (state_.loaderInitialized) {
    volkFinalize();
    loaderOwned_ = false;
  }
}

auto Backend::type() const noexcept -> BackendType {
  return state_.capabilities.type;
}

auto Backend::capabilities() const noexcept -> const BackendCapabilities & {
  return state_.capabilities;
}

}
