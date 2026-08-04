module;

#define VK_NO_PROTOTYPES
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan_core.h>

#include <volk.h>

module Nyx.RHI;

import std;
import Nyx.Core;
import Nyx.Log;

import :Adapter;
import :Backend;
import :Instance;
import :Vulkan;

namespace Nyx::RHI {

namespace {

VKAPI_ATTR constexpr auto VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT /*flags*/,
    const VkDebugUtilsMessengerCallbackDataEXT *callbackData,
    void * /*data*/) noexcept -> VkBool32 {
  if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
    NLogger::error("Validation layer: {}", callbackData->pMessage);

  return VK_FALSE;
}

constexpr auto makeApiVersion(u32 version) noexcept -> ApiVersion {
  return {
      .major = VK_API_VERSION_MAJOR(version),
      .minor = VK_API_VERSION_MINOR(version),
      .patch = VK_API_VERSION_PATCH(version),
  };
}

constexpr auto queryDeviceFeatures(VkPhysicalDevice physicalDevice) noexcept -> DeviceFeatures {
  DeviceFeatures result{};

  if (vkGetPhysicalDeviceFeatures2 != nullptr) {
    VkPhysicalDeviceVulkan14Features features14{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES,
    };
    VkPhysicalDeviceVulkan13Features features13{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext = &features14,
    };
    VkPhysicalDeviceVulkan12Features features12{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .pNext = &features13,
    };
    VkPhysicalDeviceFeatures2 features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &features12,
    };

    vkGetPhysicalDeviceFeatures2(physicalDevice, &features);

    if (features.features.samplerAnisotropy == VK_TRUE)
      result |= DeviceFeature::SamplerAnisotropy;

    if (features12.timelineSemaphore == VK_TRUE)
      result |= DeviceFeature::TimelineSemaphore;

    if (features12.descriptorIndexing == VK_TRUE)
      result |= DeviceFeature::DescriptorIndexing;

    if (features12.bufferDeviceAddress == VK_TRUE)
      result |= DeviceFeature::BufferDeviceAddress;

    if (features13.synchronization2 == VK_TRUE)
      result |= DeviceFeature::Synchronization2;

    if (features13.dynamicRendering == VK_TRUE)
      result |= DeviceFeature::DynamicRendering;

    return result;
  }

  VkPhysicalDeviceFeatures features{};
  vkGetPhysicalDeviceFeatures(physicalDevice, &features);
  if (features.samplerAnisotropy == VK_TRUE)
    result |= DeviceFeature::SamplerAnisotropy;

  return result;
}

constexpr auto supported(const FlatSet<String> &values, StringView value) noexcept -> bool {
  return values.contains(String{value});
}

constexpr auto collectExtensions(const BackendCapabilities &capabilities,
    const InstanceDescriptor &desc,
    Span<const char *const> platformExtensions,
    bool debugUtilsAvailable) -> Result<Vec<String>> {
  FlatSet<String> names;

  for (const char *extension : platformExtensions) {
    if (not supported(capabilities.instanceExtensions, extension))
      return bail({"A platform-required Vulkan instance extension is unavailable."});

    names.emplace(extension);
  }

  for (const StringView extension : desc.requiredExtensions) {
    if (not supported(capabilities.instanceExtensions, extension))
      return bail({"A required Vulkan instance extension is unavailable."});

    names.emplace(extension);
  }

  names.insert_range(
      desc.optionalExtensions |
      std::views::filter([&capabilities](const StringView extension) constexpr noexcept -> bool {
        return supported(capabilities.instanceExtensions, extension);
      }) |
      std::ranges::to<Vec<String>>());

  if (debugUtilsAvailable)
    names.emplace(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

  return names | std::ranges::to<Vec<String>>();
}

constexpr auto collectLayers(const BackendCapabilities &capabilities, const InstanceDescriptor &desc)
    -> Result<Vec<String>> {
  FlatSet<String> names;

  for (const StringView layer : desc.requiredLayers) {
    if (not supported(capabilities.layers, layer))
      return bail({"A required Vulkan instance layer is unavailable."});

    names.emplace(layer);
  }

  names.insert_range(desc.optionalLayers |
                     std::views::filter([&capabilities](const StringView layer) constexpr noexcept -> bool {
                       return supported(capabilities.layers, layer);
                     }) |
                     std::ranges::to<Vec<String>>());

  constexpr StringView validationLayer{"VK_LAYER_KHRONOS_validation"};
  const bool validationAvailable = supported(capabilities.layers, validationLayer);

  if (desc.validation == ValidationMode::Required and not validationAvailable)
    return bail({"The Vulkan validation layer is required but unavailable."});

  if (desc.validation != ValidationMode::Disabled and validationAvailable)
    names.emplace(validationLayer);

  return names | std::ranges::to<Vec<String>>();
}

constexpr auto destroyInstanceState(detail::VulkanInstanceState &state) noexcept -> void {
  if (state.debugMessenger != VK_NULL_HANDLE and state.destroyDebugUtilsMessenger != nullptr)
    state.destroyDebugUtilsMessenger(state.instance, state.debugMessenger, nullptr);

  if (state.instance != VK_NULL_HANDLE and state.destroyInstance != nullptr)
    state.destroyInstance(state.instance, nullptr);

  state = {};
}

} // namespace

auto Instance::create(const Backend &backend, const InstanceDescriptor &desc) -> Result<Instance> {
  if (backend.type() != BackendType::Vulkan)
    return bail({"Cannot create a Vulkan instance from a non-Vulkan backend."});

  if (backend.capabilities().apiVersion < desc.requestedApiVersion)
    return bail({"The requested Vulkan API version is unavailable."});

  u32 platformExtensionCount{};
  const char *const *platformExtensionNames = SDL_Vulkan_GetInstanceExtensions(&platformExtensionCount);

  if (platformExtensionNames == nullptr)
    return bail({"Failed to query SDL Vulkan instance extensions."});

  const Span<const char *const> platformExtensions{platformExtensionNames, platformExtensionCount};

  const bool debugUtilsAvailable =
      supported(backend.capabilities().instanceExtensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  const bool debugEnabled = debugUtilsAvailable and desc.debugSeverity != DebugMessageSeverity{};

  Result<Vec<String>> extensionNames =
      collectExtensions(backend.capabilities(), desc, platformExtensions, debugEnabled);
  if (not extensionNames)
    return bail(extensionNames.error().release());

  Result<Vec<String>> layerNames = collectLayers(backend.capabilities(), desc);
  if (not layerNames)
    return bail(layerNames.error().release());

  auto toCString = [](const String &str) constexpr noexcept -> const char * { return str.c_str(); };

  Vec<const char *> extensionPointers =
      *extensionNames | std::views::transform(toCString) | std::ranges::to<Vec<const char *>>();

  Vec<const char *> layerPointers =
      *layerNames | std::views::transform(toCString) | std::ranges::to<Vec<const char *>>();

  String applicationName{desc.applicationName};
  String engineName{desc.engineName};

  const VkApplicationInfo applicationInfo{
      .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pApplicationName = applicationName.c_str(),
      .applicationVersion = VK_MAKE_VERSION(
          desc.applicationVersion.major, desc.applicationVersion.minor, desc.applicationVersion.patch),
      .pEngineName = engineName.c_str(),
      .engineVersion =
          VK_MAKE_VERSION(desc.engineVersion.major, desc.engineVersion.minor, desc.engineVersion.patch),
      .apiVersion = VK_MAKE_API_VERSION(
          0, desc.requestedApiVersion.major, desc.requestedApiVersion.minor, desc.requestedApiVersion.patch),
  };

  const VkDebugUtilsMessengerCreateInfoEXT debugInfo{
      .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
      .messageSeverity = detail::toVkSeverity(desc.debugSeverity),
      .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
      .pfnUserCallback = debugCallback,
  };

  Instance instance{};

  const VkInstanceCreateInfo createInfo{
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pNext = debugEnabled ? &debugInfo : nullptr,
      .pApplicationInfo = &applicationInfo,
      .enabledLayerCount = static_cast<u32>(layerPointers.size()),
      .ppEnabledLayerNames = layerPointers.data(),
      .enabledExtensionCount = static_cast<u32>(extensionPointers.size()),
      .ppEnabledExtensionNames = extensionPointers.data(),
  };

  if (vkCreateInstance(&createInfo, nullptr, &instance.state_.instance) != VK_SUCCESS)
    return bail({"Failed to create Vulkan instance."});

  volkLoadInstance(instance.state_.instance);

  instance.state_.destroyInstance = vkDestroyInstance;
  instance.state_.destroyDebugUtilsMessenger = vkDestroyDebugUtilsMessengerEXT;
  instance.state_.destroySurface = vkDestroySurfaceKHR;

  if (debugEnabled and vkCreateDebugUtilsMessengerEXT == nullptr)
    return bail({"Vulkan debug messenger creation is unavailable"});

  if (debugEnabled and
      vkCreateDebugUtilsMessengerEXT(
          instance.state_.instance, &debugInfo, nullptr, &instance.state_.debugMessenger) != VK_SUCCESS)
    return bail({"Failed to create Vulkan debug messenger."});

  return instance;
}

Instance::Instance(Instance &&other) noexcept
    : state_(std::exchange(other.state_, {})) {
}

auto Instance::operator=(Instance &&other) noexcept -> Instance & {
  if (this == &other)
    return *this;

  destroyInstanceState(state_);
  state_ = std::exchange(other.state_, {});
  return *this;
}

Instance::~Instance() noexcept {
  destroyInstanceState(state_);
}

auto Instance::nativeHandle() const noexcept -> VkInstance {
  return state_.instance;
}

auto Instance::createSurface(const SurfaceDescriptor &desc) const noexcept -> Result<Surface> {
  return Surface::create(*this, desc);
}

// NOLINTNEXTLINE(readability-function-size, readability-function-cognitive-complexity)
auto Instance::enumerateAdapters(Option<std::reference_wrapper<const Surface>> surface) const
    -> Result<Vec<Adapter>> {
  if (state_.instance == VK_NULL_HANDLE)
    return bail({"Cannot enumerate adapters from an invalid Vulkan instance."});

  if (surface != None and surface->get().state_.surface == VK_NULL_HANDLE)
    return bail({"Cannot enumerate adapters with an invalid surface."});

  if (surface != None and surface->get().state_.instance != state_.instance)
    return bail({"Surface and instance belong to different Vulkan instances."});

  u32 deviceCount{};
  if (vkEnumeratePhysicalDevices(state_.instance, &deviceCount, nullptr) != VK_SUCCESS)
    return bail({"Failed to enumerate Vulkan physical devices."});

  Vec<VkPhysicalDevice> physicalDevices{deviceCount};
  if (vkEnumeratePhysicalDevices(state_.instance, &deviceCount, physicalDevices.data()) != VK_SUCCESS)
    return bail({"Failed to enumerate Vulkan physical devices."});
  physicalDevices.resize(deviceCount);

  Vec<Adapter> adapters;
  adapters.reserve(physicalDevices.size());

  for (const VkPhysicalDevice physicalDevice : physicalDevices) {
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physicalDevice, &properties);

    String driverName;
    if (vkGetPhysicalDeviceProperties2 != nullptr) {
      VkPhysicalDeviceDriverProperties driverProperties{
          .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES,
      };
      VkPhysicalDeviceProperties2 properties2{
          .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
          .pNext = &driverProperties,
      };
      vkGetPhysicalDeviceProperties2(physicalDevice, &properties2);
      driverName = static_cast<char *>(driverProperties.driverName);
    }

    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);

    u64 localMemory = std::ranges::fold_left(
        Span<VkMemoryHeap>{
            static_cast<VkMemoryHeap *>(memoryProperties.memoryHeaps), memoryProperties.memoryHeapCount} |
            std::views::filter([](VkMemoryHeap heap) constexpr noexcept -> bool {
              return (heap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0;
            }) |
            std::views::transform([](VkMemoryHeap heap) constexpr noexcept -> u32 { return heap.size; }),
        0,
        std::plus<>{});

    Adapter adapter{};
    adapter.state_.physicalDevice = physicalDevice;
    adapter.state_.instance = state_.instance;
    adapter.state_.info = {
        .name = static_cast<const char *>(properties.deviceName),
        .driverName = std::move(driverName),
        .vendorId = properties.vendorID,
        .deviceId = properties.deviceID,
        .type = detail::makeAdapterType(properties.deviceType),
        .apiVersion = makeApiVersion(properties.apiVersion),
        .localMemory = localMemory,
    };
    adapter.state_.supportedFeatures = queryDeviceFeatures(physicalDevice);

    u32 queueFamilyCount{};
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);

    Vec<VkQueueFamilyProperties> queueProperties{queueFamilyCount};
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueProperties.data());
    queueProperties.resize(queueFamilyCount);

    adapter.state_.queueFamilies.reserve(queueProperties.size());
    bool presentQueueAvailable{};

    for (const auto &[index, queue] :
        Span<VkQueueFamilyProperties>{queueProperties.data(), queueProperties.size()} |
            std::views::enumerate) {
      bool supportsPresent{};

      if (surface != None) {
        VkBool32 present{};
        if (vkGetPhysicalDeviceSurfaceSupportKHR(
                physicalDevice, static_cast<u32>(index), surface->get().nativeHandle(), &present) !=
            VK_SUCCESS)
          return bail({"Failed to query physical-device surface support."});

        supportsPresent = present == VK_TRUE;
        presentQueueAvailable |= supportsPresent;
      }

      adapter.state_.queueFamilies.push_back({
          .capabilities = detail::makeQueueCapabilities(queue.queueFlags),
          .queueCount = queue.queueCount,
          .supportsPresent = supportsPresent,
      });
    }

    if (surface != None and not presentQueueAvailable)
      continue;

    u32 extensionCount{};
    if (vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, nullptr) != VK_SUCCESS)
      return bail({"Failed to enumerate physical-device extensions."});

    Vec<VkExtensionProperties> extensionProperties{extensionCount};
    if (vkEnumerateDeviceExtensionProperties(
            physicalDevice, nullptr, &extensionCount, extensionProperties.data()) != VK_SUCCESS)
      return bail({"Failed to enumerate physical-device extensions."});
    extensionProperties.resize(extensionCount);

    adapter.state_.extensions =
        extensionProperties |
        std::views::transform([](const VkExtensionProperties &extension) constexpr -> String {
          return static_cast<const char *>(extension.extensionName);
        }) |
        std::ranges::to<FlatSet<String>>();

    adapter.state_.formatFeatures =
        textureFormatMetadata | std::views::enumerate |
        std::views::transform(
            [](const Pair<usize, TextureFormatInfo> &metadata) -> Pair<TextureFormat, Option<VkFormat>> {
              auto format = static_cast<TextureFormat>(metadata.first);
              return {format, detail::toVkFormat(format)};
            }) |
        std::views::filter([](const Pair<TextureFormat, Option<VkFormat>> &formats) -> bool {
          return formats.second.has_value();
        }) |
        std::views::transform([&physicalDevice](const Pair<TextureFormat, Option<VkFormat>> &formats)
                                  -> Pair<TextureFormat, TextureFormatFeatures> {
          const auto &[format, nativeFormat] = formats;

          VkFormatProperties formatProperties{};
          vkGetPhysicalDeviceFormatProperties(physicalDevice, *nativeFormat, &formatProperties);

          return {format, detail::makeFormatFeatures(formatProperties.optimalTilingFeatures)};
        }) |
        std::ranges::to<FlatMap<TextureFormat, TextureFormatFeatures>>();

    adapters.push_back(std::move(adapter));
  }

  return adapters;
}

} // namespace Nyx::RHI
