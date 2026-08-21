module;

#define VK_NO_PROTOTYPES
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0
#include <volk.h>

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
#include <vulkan/vulkan_core.h>

module Nyx.RHI;

import std;
import Miracle;

import :ResourcePool;
import :Adapter;
import :Device;
import :Vulkan;

using namespace Miracle;

namespace Nyx::RHI {

namespace {

struct QueueSelection {
  QueueRole role{};
  QueueCapabilities capabilities{};
  u32 familyIndex{};
  u32 queueIndex{};
};

struct QueuePlan {
  FlatMap<u32, Vec<f32>> priorities;
  Vec<QueueSelection> selections;
};

constexpr auto apiSupports(ApiVersion api, DeviceFeature feature) noexcept -> bool {
  switch (feature) {
    case DeviceFeature::TimelineSemaphore:
    case DeviceFeature::DescriptorIndexing:
    case DeviceFeature::BufferDeviceAddress: return api >= ApiVersion{.major = 1, .minor = 2, .patch = 0};
    case DeviceFeature::Synchronization2:
    case DeviceFeature::DynamicRendering: return api >= ApiVersion{.major = 1, .minor = 3, .patch = 0};
    case DeviceFeatures::SamplerAnisotropy: return api >= ApiVersion{.major = 1, .minor = 0, .patch = 0};
  }

  return false;
}

constexpr auto validateRequiredFeatures(DeviceFeatures features, ApiVersion api) noexcept -> Result<void> {
  for (u64 bit{1}; bit <= bits(all<DeviceFeatures>()); bit <<= 1) {
    const auto feature = static_cast<DeviceFeature>(bit);
    if (has(features, feature) and not apiSupports(api, feature))
      return bail({"Required device feature is unavailable at adapter API version."});
  }

  return {};
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
constexpr auto makeQueuePlan(const Adapter &adapter, Span<const QueueRequest> requests) -> Result<QueuePlan> {
  QueuePlan plan;
  plan.selections.reserve(requests.size());

  const Span<const QueueFamilyInfo> families = adapter.queueFamilies();

  for (const QueueRequest &request : requests) {
    if (request.count == 0)
      return bail({"A queue request must ask for at least one queue."});

    if (not std::isfinite(request.priority) or request.priority < 0.0F or request.priority > 1.0F)
      return bail({"Queue priority must be finite and within [0, 1]."});

    const QueueCapabilities required = request.required | detail::queueRoleCapabilities(request.role);
    const auto requiredBits = bits(required);

    u32 selectedFamily{};
    u32 selectedQueueCount{std::numeric_limits<u32>::max()};
    u32 selectedScore{std::numeric_limits<u32>::max()};
    bool found{};

    for (usize familyIndex{}; familyIndex < families.size(); ++familyIndex) {
      const QueueFamilyInfo &family = families[familyIndex];
      const auto familyBits = bits(family.capabilities);

      if ((familyBits & requiredBits) != requiredBits or
          (request.presentSurface.has_value() and not family.supportsPresent))
        continue;

      const auto familyKey = static_cast<u32>(familyIndex);
      const auto priorityIterator = plan.priorities.find(familyKey);
      const u32 assigned =
          priorityIterator == plan.priorities.end() ? 0U : static_cast<u32>(priorityIterator->second.size());

      if (assigned + request.count > family.queueCount)
        continue;

      u32 score{};
      if (request.role == QueueRole::Compute and has(family.capabilities, QueueCapabilities::Graphics))
        score += 2;

      if (request.role == QueueRole::Transfer and
          has(family.capabilities, QueueCapabilities::Graphics | QueueCapabilities::Compute))
        score += 2;

      if (request.role == QueueRole::Present and has(family.capabilities, QueueCapabilities::Graphics))
        score += 1;

      if (not found or score < selectedScore or (score == selectedScore and assigned < selectedQueueCount)) {
        selectedFamily = familyKey;
        selectedQueueCount = assigned;
        selectedScore = score;
        found = true;
      }
    }

    if (not found)
      return bail({"No queue family satisfies the requested queue."});

    const f32 priority = request.priority == 0.0F ? 0.0F : request.priority;

    auto priorityIterator = plan.priorities.try_emplace(selectedFamily).first;
    const auto firstQueue = static_cast<u32>(priorityIterator->second.size());
    priorityIterator->second.insert(priorityIterator->second.end(), request.count, priority);

    const QueueCapabilities capabilities = families[selectedFamily].capabilities;
    for (u32 queueIndex{}; queueIndex < request.count; ++queueIndex) {
      plan.selections.push_back({
          .role = request.role,
          .capabilities = capabilities,
          .familyIndex = selectedFamily,
          .queueIndex = firstQueue + queueIndex,
      });
    }
  }

  return plan;
}

struct FeatureChain {
  VkPhysicalDeviceFeatures legacy{};
  VkPhysicalDeviceFeatures2 core{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
  };
  VkPhysicalDeviceVulkan12Features vulkan12{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
  };
  VkPhysicalDeviceVulkan13Features vulkan13{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
  };
  VkPhysicalDeviceVulkan14Features vulkan14{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES,
  };
  bool usesFeatures2{};
};

constexpr auto makeFeatureChain(FeatureChain &chain, DeviceFeatures enabled, ApiVersion api) noexcept
    -> void {
  chain.usesFeatures2 = api >= ApiVersion{.major = 1, .minor = 1, .patch = 0};

  if (has(enabled, DeviceFeature::SamplerAnisotropy)) {
    chain.legacy.samplerAnisotropy = VK_TRUE;
    chain.core.features.samplerAnisotropy = VK_TRUE;
  }

  if (api >= ApiVersion{.major = 1, .minor = 2, .patch = 0}) {
    chain.core.pNext = &chain.vulkan12;

    if (has(enabled, DeviceFeatures::TimelineSemaphore))
      chain.vulkan12.timelineSemaphore = VK_TRUE;

    if (has(enabled, DeviceFeatures::DescriptorIndexing))
      chain.vulkan12.descriptorIndexing = VK_TRUE;

    if (has(enabled, DeviceFeatures::BufferDeviceAddress))
      chain.vulkan12.bufferDeviceAddress = VK_TRUE;
  }

  if (api >= ApiVersion{.major = 1, .minor = 3, .patch = 0}) {
    chain.core.pNext = &chain.vulkan13;
    chain.vulkan13.pNext = &chain.vulkan12;

    if (has(enabled, DeviceFeatures::Synchronization2))
      chain.vulkan13.synchronization2 = VK_TRUE;

    if (has(enabled, DeviceFeatures::DynamicRendering))
      chain.vulkan13.dynamicRendering = VK_TRUE;
  }
}

constexpr auto validateShaderStage(const ShaderStageDescriptor &desc) noexcept -> Result<void> {
  if (desc.code.empty())
    return bail({"A shader stage must contain SPIR-V code."});

  if (desc.entryPoint.empty())
    return bail({"A shader stage must specify an entry point."});

  if (not detail::toVkShaderStage(desc.stage).has_value())
    return bail({"The shader stage is not supported by the RHI."});

  return {};
}

constexpr auto createShaderModule(VkDevice device, const ShaderStageDescriptor &desc) noexcept
    -> Result<VkShaderModule> {
  const VkShaderModuleCreateInfo createInfo{
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = desc.code.size() * sizeof(u32),
      .pCode = desc.code.data(),
  };

  VkShaderModule shaderModule{};
  if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
    return bail({"Failed to create a Vulkan shader module."});

  return shaderModule;
}

constexpr auto createEmptyPipelineLayout(VkDevice device) noexcept -> Result<VkPipelineLayout> {
  const VkPipelineLayoutCreateInfo createInfo{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
  };

  VkPipelineLayout layout{};
  if (vkCreatePipelineLayout(device, &createInfo, nullptr, &layout) != VK_SUCCESS)
    return bail({"Failed to create a Vulkan pipeline layout."});

  return layout;
}

} // namespace

auto Device::makeEnabledFeatures(const Adapter &adapter, const DeviceDescriptor &desc) noexcept
    -> Result<DeviceFeatures> {
  if (auto result = validateRequiredFeatures(desc.requiredFeatures, adapter.info().apiVersion); not result)
    return bail(std::move(result.error()));

  const DeviceFeatures supported = adapter.state_.supportedFeatures;
  const DeviceFeatures supportedOptional = desc.optionalFeatures & supported;
  DeviceFeatures enabled = desc.requiredFeatures;

  for (u64 bit{1}; bit <= bits(all<DeviceFeatures>()); bit <<= 1) {
    const auto feature = static_cast<DeviceFeature>(bit);
    if (has(supportedOptional, feature) and apiSupports(adapter.info().apiVersion, feature))
      enabled |= feature;
  }

  return enabled;
}

auto Device::collectExtensions(const Adapter &adapter,
    const DeviceDescriptor &desc,
    bool requiresSwapchain) noexcept -> Result<FlatSet<String>> {
  FlatSet<String> extensions;

  for (const StringView extension : desc.requiredExtensions) {
    if (not adapter.state_.extensions.contains(String{extension}))
      return bail({"Required device extension is unavailable: {}", extension});

    extensions.insert(String{extension});
  }

  for (const StringView extension : desc.optionalExtensions) {
    if (adapter.state_.extensions.contains(String{extension}))
      extensions.insert(String{extension});
  }

  if (requiresSwapchain) {
    if (not adapter.state_.extensions.contains(String{VK_KHR_SWAPCHAIN_EXTENSION_NAME}))
      return bail({"The adapter does not support VK_KHR_swapchain"});

    extensions.insert(String{VK_KHR_SWAPCHAIN_EXTENSION_NAME});
  }

  return extensions;
}

// NOLINTNEXTLINE(readability-function-size)
auto Device::create(const Adapter &adapter, const DeviceDescriptor &desc) -> Result<Device> {
  if (adapter.nativeHandle() == VK_NULL_HANDLE)
    return bail({"Cannot create a device from an invalid adapter."});

  if (desc.queues.empty())
    return bail({"A device must request at least one queue."});

  if (not adapter.supports(desc))
    return bail({"The adapter does not satisfy the device descriptor."});

  Result<QueuePlan> planResult = makeQueuePlan(adapter, desc.queues);
  if (not planResult)
    return bail(planResult.error().release());

  QueuePlan plan = std::move(*planResult);

  bool requiresSwapchain = std::ranges::any_of(desc.queues,
      [](const QueueRequest &request) noexcept -> bool { return request.presentSurface.has_value(); });

  Result<FlatSet<String>> extensionResult = collectExtensions(adapter, desc, requiresSwapchain);
  if (not extensionResult)
    return bail{extensionResult.error().release()};

  FlatSet<String> extensions = std::move(*extensionResult);
  Vec<const char *> extensionNames =
      extensions |
      std::views::transform([](const String &str) noexcept -> const char * { return str.c_str(); }) |
      std::ranges::to<Vec<const char *>>();

  auto featureResult = makeEnabledFeatures(adapter, desc);
  if (not featureResult)
    return bail(featureResult.error().release());

  const DeviceFeatures enabledFeatures = *featureResult;
  FeatureChain featureChain;
  makeFeatureChain(featureChain, enabledFeatures, adapter.info().apiVersion);

  Vec<Vec<f32>> queuePriorities;
  queuePriorities.reserve(plan.priorities.size());

  Vec<VkDeviceQueueCreateInfo> queueCreateInfos;
  queueCreateInfos.reserve(plan.priorities.size());
  for (const auto &[familyIndex, priorities] : plan.priorities) {
    queuePriorities.push_back(priorities);
    const Vec<f32> &storedPriorities = queuePriorities.back();

    queueCreateInfos.push_back({
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = familyIndex,
        .queueCount = static_cast<u32>(storedPriorities.size()),
        .pQueuePriorities = storedPriorities.data(),
    });
  }

  VkDeviceCreateInfo createInfo{
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pNext = featureChain.usesFeatures2 ? &featureChain.core : nullptr,
      .queueCreateInfoCount = static_cast<u32>(queueCreateInfos.size()),
      .pQueueCreateInfos = queueCreateInfos.data(),
      .enabledExtensionCount = static_cast<u32>(extensionNames.size()),
      .ppEnabledExtensionNames = extensionNames.data(),
      .pEnabledFeatures = featureChain.usesFeatures2 ? nullptr : &featureChain.legacy,
  };

  Device device{};
  device.state_.physicalDevice = adapter.nativeHandle();
  device.state_.instance = adapter.state_.instance;
  device.state_.features = enabledFeatures;

  if (VkResult deviceResult = deviceResult =
          vkCreateDevice(adapter.nativeHandle(), &createInfo, nullptr, &device.state_.device);
      deviceResult != VK_SUCCESS)
    return bail({
        "Failed to create Vulkan device: {}",
        static_cast<i32>(deviceResult),
    });

  volkLoadDevice(device.state_.device);

  VmaAllocatorCreateInfo allocatorInfo{};
  allocatorInfo.instance = device.state_.instance;
  allocatorInfo.physicalDevice = device.state_.physicalDevice;
  allocatorInfo.device = device.state_.device;
  allocatorInfo.vulkanApiVersion = VK_MAKE_API_VERSION(
      0, adapter.info().apiVersion.major, adapter.info().apiVersion.minor, adapter.info().apiVersion.patch);
  allocatorInfo.flags = 0;

  VmaVulkanFunctions vmaFunctions{};
  if (VkResult importResult = vmaImportVulkanFunctionsFromVolk(&allocatorInfo, &vmaFunctions);
      importResult != VK_SUCCESS)
    return bail({
        "Failed to import Vulkan functions into VMA: {}",
        static_cast<i32>(importResult),
    });
  allocatorInfo.pVulkanFunctions = &vmaFunctions;

  if (VkResult allocatorResult = vmaCreateAllocator(&allocatorInfo, &device.state_.allocator);
      allocatorResult != VK_SUCCESS)
    return bail({
        "Failed to create the VMA allocator: {}",
        static_cast<i32>(allocatorResult),
    });

  device.queues_ =
      plan.selections | std::views::transform([&](const QueueSelection &selection) noexcept -> Queue {
        Queue queue{};
        vkGetDeviceQueue(
            device.state_.device, selection.familyIndex, selection.queueIndex, &queue.state_.queue);
        queue.state_.device = device.state_.device;
        queue.state_.info = {
            .role = selection.role,
            .capabilities = selection.capabilities,
            .familyIndex = selection.familyIndex,
            .queueIndex = selection.queueIndex,
            .supportsPresent = adapter.state_.queueFamilies[selection.familyIndex].supportsPresent,
        };
        return queue;
      }) |
      std::ranges::to<Vec<Queue>>();

  return device;
}

Device::Device(Device &&other) noexcept
    : state_(std::exchange(other.state_, {}))
    , textures_(std::exchange(other.textures_, {}))
    , nativeTextures_(std::exchange(other.nativeTextures_, {}))
    , textureViews_(std::exchange(other.textureViews_, {}))
    , nativeTextureViews_(std::exchange(other.nativeTextureViews_, {}))
    , pipelines_(std::exchange(other.pipelines_, {}))
    , nativePipelines_(std::exchange(other.nativePipelines_, {}))
    , queues_(std::exchange(other.queues_, {})) {
}

auto Device::operator=(Device &&other) noexcept -> Device & {
  if (this == &other)
    return *this;

  if (state_.device != VK_NULL_HANDLE)
    static_cast<void>(vkDeviceWaitIdle(state_.device));

  destroyPipelines();
  destroyTextures();
  destroyState();

  state_ = std::exchange(other.state_, {});
  textures_ = std::exchange(other.textures_, {});
  nativeTextures_ = std::exchange(other.nativeTextures_, {});
  textureViews_ = std::exchange(other.textureViews_, {});
  nativeTextureViews_ = std::exchange(other.nativeTextureViews_, {});
  pipelines_ = std::exchange(other.pipelines_, {});
  nativePipelines_ = std::exchange(other.nativePipelines_, {});
  queues_ = std::exchange(other.queues_, {});
  return *this;
}

Device::~Device() noexcept {
  if (state_.device != VK_NULL_HANDLE)
    static_cast<void>(vkDeviceWaitIdle(state_.device));

  destroyPipelines();
  destroyTextures();
  destroyState();
}

auto Device::destroyPipelines() noexcept -> void {
  if (state_.device != VK_NULL_HANDLE) {
    for (const auto &entry : nativePipelines_) {
      const VkPipeline &pipeline = entry.second.pipeline;
      if (pipeline != VK_NULL_HANDLE)
        vkDestroyPipeline(state_.device, pipeline, nullptr);

      const VkPipelineLayout layout = entry.second.layout;
      if (layout != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(state_.device, layout, nullptr);
    }
  }

  nativePipelines_.clear();
}

auto Device::destroyTextures() noexcept -> void {
  if (state_.device != VK_NULL_HANDLE) {
    for (const auto &entry : nativeTextureViews_) {
      const VkImageView imageView = entry.second.imageView;
      if (imageView != VK_NULL_HANDLE)
        vkDestroyImageView(state_.device, imageView, nullptr);
    }
  }

  nativeTextureViews_.clear();

  if (state_.allocator != VK_NULL_HANDLE) {
    for (const auto &entry : nativeTextures_) {
      const detail::VulkanTextureState &texture = entry.second;
      if (texture.image != VK_NULL_HANDLE and texture.allocation != VK_NULL_HANDLE)
        vmaDestroyImage(state_.allocator, texture.image, texture.allocation);
    }
  }

  nativeTextures_.clear();
}

auto Device::destroyState() noexcept -> void {
  if (state_.allocator != VK_NULL_HANDLE) {
    vmaDestroyAllocator(state_.allocator);
    state_.allocator = {};
  }

  if (state_.device != VK_NULL_HANDLE) {
    vkDestroyDevice(state_.device, nullptr);
    state_.device = {};
  }

  state_.physicalDevice = {};
  state_.instance = {};
  state_.features = {};
}

auto Device::queue(QueueRole role) noexcept -> Option<Ref<Queue>> {
  for (Queue &queue : queues_)
    if (queue.info().role == role)
      return Ref<Queue>{queue};

  return None;
}

auto Device::queue(QueueRole role) const noexcept -> Option<Ref<const Queue>> {
  for (const Queue &queue : queues_)
    if (queue.info().role == role)
      return Ref<const Queue>{queue};

  return None;
}

auto Device::createTexture(const TextureDescriptor &desc) -> Result<TextureHandle> {
  if (state_.device == VK_NULL_HANDLE or state_.allocator == VK_NULL_HANDLE)
    return bail({"Cannot create a texture on an invalid Vulkan device."});

  if (desc.initialLayout != TextureLayout::Undefined)
    return bail({"Non-undefined texture initial layouts require a command context."});

  if (Result<void> result = detail::validateTextureDescriptor(desc); not result)
    return bail(result.error().release());

  const Option<VkFormat> nativeFormat = detail::toVkFormat(desc.format);
  if (not nativeFormat)
    return bail({"Texture format has no Vulkan mapping."});

  const TextureFormatInfo formatInfo = textureFormatInfo(desc.format);
  const TextureAspect aspects = formatInfo.aspects;
  const TextureUsage usage = desc.usage;

  if (has(usage, TextureUsage::ColorAttachment) and not has(aspects, TextureAspect::Color))
    return bail({"Color attachment usage requires a color texture format."});

  if (has(usage, TextureUsage::DepthStencilAttachment) and not has(aspects, TextureAspect::Depth))
    return bail({"Depth-stencil attachment usage requires a depth texture format"});

  const VkFormatFeatureFlags requiredFormatFeatures = detail::makeFormatFeatureMask(usage);
  if (requiredFormatFeatures != 0) {
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(state_.physicalDevice, *nativeFormat, &properties);

    if ((properties.optimalTilingFeatures & requiredFormatFeatures) != requiredFormatFeatures)
      return bail({"The adapter does not support the requested texture usage."});
  }

  const VkImageUsageFlags imageUsage = detail::toVkImageUsage(desc.usage);
  if (imageUsage == 0)
    return bail({"Texture usage contains no Vulkan image usage bits."});

  Result<TextureHandle> handleResult = textures_.emplace(desc);
  if (not handleResult)
    return bail(handleResult.error().release());

  const TextureHandle handle = *handleResult;
  auto eraseRecord = std::scope_exit([&] -> void {
    // NOLINTNEXTLINE(bugprone-unused-return-value)
    static_cast<void>(textures_.erase(handle));
  });

  VkImageCreateInfo imageInfo{
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType = detail::toVkImageType(desc.dimension),
      .format = *nativeFormat,
      .extent =
          {
              .width = desc.extent.width,
              .height = desc.extent.height,
              .depth = desc.extent.depth,
          },
      .mipLevels = desc.mipLevels,
      .arrayLayers = desc.arrayLayers,
      .samples = detail::toVkSampleCount(desc.sampleCount),
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = imageUsage,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
  };

  VmaAllocationCreateInfo allocationInfo{};
  allocationInfo.usage = VMA_MEMORY_USAGE_AUTO;

  VkImage image{};
  VmaAllocation allocation{};
  const VkResult imageResult =
      vmaCreateImage(state_.allocator, &imageInfo, &allocationInfo, &image, &allocation, nullptr);
  if (imageResult != VK_SUCCESS)
    return bail({"Failed to create Vulkan texture: {}", static_cast<i32>(imageResult)});

  auto destroyImage = std::scope_exit([&] -> void { vmaDestroyImage(state_.allocator, image, allocation); });

  nativeTextures_.emplace(handle,
      detail::VulkanTextureState{
          .image = image,
          .allocation = allocation,
      });

  destroyImage.release();
  eraseRecord.release();
  return handle;
}

auto Device::createTextureView(const TextureViewDescriptor &desc) -> Result<TextureViewHandle> {
  if (state_.device == VK_NULL_HANDLE)
    return bail({"Cannot create a texture view on an invalid Vulkan device."});

  auto textureResult = textures_.get(desc.texture);
  if (not textureResult)
    return bail(textureResult.error().release());

  Texture &texture = textureResult->get();
  const TextureDescriptor &textureDescriptor = texture.descriptor();
  const TextureFormatInfo formatInfo = textureFormatInfo(textureDescriptor.format);
  const auto requestedAspects = bits(desc.aspects);
  const auto availableAspects = bits(formatInfo.aspects);

  if (requestedAspects == 0 or (requestedAspects & availableAspects) != requestedAspects)
    return bail({"Texture view aspects are incompatible with the texture format."});

  if (desc.baseMipLevel >= textureDescriptor.mipLevels)
    return bail({"Texture view base mip level is outside the texture."});

  if (desc.baseArrayLayer >= textureDescriptor.arrayLayers)
    return bail({"Texture view base array layer is outside the texture."});

  TextureViewDescriptor resolvedDescriptor = desc;
  resolvedDescriptor.mipLevelCount =
      desc.mipLevelCount == 0 ? textureDescriptor.mipLevels - desc.baseMipLevel : desc.mipLevelCount;
  resolvedDescriptor.arrayLayerCount =
      desc.arrayLayerCount == 0 ? textureDescriptor.arrayLayers - desc.baseArrayLayer : desc.arrayLayerCount;

  if (resolvedDescriptor.mipLevelCount == 0 or
      resolvedDescriptor.mipLevelCount > textureDescriptor.mipLevels - resolvedDescriptor.baseMipLevel)
    return bail({"Texture view mip range is outside the texture."});

  if (resolvedDescriptor.arrayLayerCount == 0 or
      resolvedDescriptor.arrayLayerCount > textureDescriptor.arrayLayers - resolvedDescriptor.baseArrayLayer)
    return bail({"Texture view array-layer range is outside the texture."});

  if (textureDescriptor.dimension == TextureDimension::D3 and
      (resolvedDescriptor.baseArrayLayer != 0 or resolvedDescriptor.arrayLayerCount != 1))
    return bail({"Three-dimensional texture views must address one layer."});

  const Option<VkFormat> nativeFormat = detail::toVkFormat(textureDescriptor.format);
  if (not nativeFormat)
    return bail({"Texture format has no Vulkan mapping."});

  const auto nativeImageIterator = nativeTextures_.find(desc.texture);
  if (nativeImageIterator == nativeTextures_.end() or nativeImageIterator->second.image == VK_NULL_HANDLE)
    return bail({"Texture handle has no Vulkan image state."});

  auto handleResult = textureViews_.emplace(resolvedDescriptor);
  if (not handleResult)
    return bail(handleResult.error().release());

  const auto handle = *handleResult;
  auto eraseRecord = std::scope_exit([&] -> void {
    // NOLINTNEXTLINE(bugprone-unused-return-value)
    static_cast<void>(textureViews_.erase(handle));
  });

  const VkImageViewCreateInfo imageViewInfo{
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = nativeImageIterator->second.image,
      .viewType = detail::toVkImageViewType(textureDescriptor.dimension, textureDescriptor.arrayLayers),
      .format = *nativeFormat,
      .subresourceRange =
          {
              .aspectMask = detail::toVkImageAspect(desc.aspects),
              .baseMipLevel = resolvedDescriptor.baseMipLevel,
              .levelCount = resolvedDescriptor.mipLevelCount,
              .baseArrayLayer = resolvedDescriptor.baseArrayLayer,
              .layerCount = resolvedDescriptor.arrayLayerCount,
          },
  };

  VkImageView imageView{};
  if (vkCreateImageView(state_.device, &imageViewInfo, nullptr, &imageView) != VK_SUCCESS)
    return bail({"Failed to create a Vulkan texture view."});

  auto destroyImageView =
      std::scope_exit([&] -> void { vkDestroyImageView(state_.device, imageView, nullptr); });

  nativeTextureViews_.emplace(handle, detail::VulkanTextureViewState{.imageView = imageView});
  ++texture.viewCount_;

  destroyImageView.release();
  eraseRecord.release();
  return handle;
}

// NOLINTNEXTLINE(readability-function-size, readability-function-cognitive-complexity)
auto Device::createGraphicsPipeline(const GraphicsPipelineDescriptor &desc) -> Result<PipelineHandle> {
  if (state_.device == VK_NULL_HANDLE)
    return bail({"Cannot create a graphics pipeline on an invalid device."});

  if (not has(state_.features, DeviceFeature::DynamicRendering))
    return bail({"Graphics pipelines require Vulkan dynamic rendering."});

  if (desc.stages.empty())
    return bail({"A graphics pipeline requires shader stages."});

  if (desc.colorFormats.empty() and desc.depthFormat == TextureFormat::Undefined and
      desc.stencilFormat == TextureFormat::Undefined)
    return bail({"A graphics pipeline requires at least one attachment format."});

  bool hasVertexStage{};
  bool hasFragmentStage{};
  for (const ShaderStageDescriptor &stage : desc.stages) {
    if (stage.stage == ShaderStage::Compute)
      return bail({"A graphics pipeline cannot contain a compute shader stage."});

    if (Result<void> result = validateShaderStage(stage); not result)
      return bail(result.error().release());

    if (stage.stage == ShaderStage::Vertex) {
      if (hasVertexStage)
        return bail({"A graphics pipeline cannot contain two vertex stages."});
      hasVertexStage = true;
    }

    if (stage.stage == ShaderStage::Fragment)
      if (hasFragmentStage)
        return bail({"A graphics pipeline requires vertex and fragment stages."});
  }
  if (not hasVertexStage or not hasFragmentStage)
    return bail({"A graphics pipeline requires vertex and fragment stages."});

  Vec<VkFormat> colorFormats;
  colorFormats.reserve(desc.colorFormats.size());
  for (const TextureFormat format : desc.colorFormats) {
    const Option<VkFormat> nativeFormat = detail::toVkFormat(format);
    if (not nativeFormat)
      return bail({"A graphics pipeline color format has no Vulkan mapping."});

    const TextureFormatInfo info = textureFormatInfo(format);
    if (not has(info.aspects, TextureAspect::Color))
      return bail({"Graphics pipeline color formats must be color formats."});

    colorFormats.push_back(*nativeFormat);
  }

  VkFormat depthFormat{VK_FORMAT_UNDEFINED};
  if (desc.depthFormat != TextureFormat::Undefined) {
    const Option<VkFormat> nativeFormat = detail::toVkFormat(desc.depthFormat);
    const TextureFormatInfo info = textureFormatInfo(desc.depthFormat);
    if (not nativeFormat or not has(info.aspects, TextureAspect::Depth))
      return bail({"Graphics pipeline depth format is invalid."});

    depthFormat = *nativeFormat;
  }

  VkFormat stencilFormat{VK_FORMAT_UNDEFINED};
  if (desc.stencilFormat != TextureFormat::Undefined) {
    const Option<VkFormat> nativeFormat = detail::toVkFormat(desc.stencilFormat);
    const TextureFormatInfo info = textureFormatInfo(desc.stencilFormat);
    if (not nativeFormat or not has(info.aspects, TextureAspect::Stencil))
      return bail({"Graphics pipeline stencil format is invalid."});

    stencilFormat = *nativeFormat;
  }

  Vec<VkShaderModule> shaderModules;
  shaderModules.reserve(desc.stages.size());
  auto destroyShaderModules = std::scope_exit([&] -> void {
    for (const VkShaderModule shaderModule : shaderModules)
      vkDestroyShaderModule(state_.device, shaderModule, nullptr);
  });

  Vec<String> entryPoints;
  entryPoints.reserve(desc.stages.size());
  Vec<VkPipelineShaderStageCreateInfo> shaderStages;
  shaderStages.reserve(desc.stages.size());

  for (const ShaderStageDescriptor &stage : desc.stages) {
    Result<VkShaderModule> shaderModuleResult = createShaderModule(state_.device, stage);
    if (not shaderModuleResult)
      return bail(shaderModuleResult.error().release());

    shaderModules.push_back(*shaderModuleResult);
    entryPoints.emplace_back(stage.entryPoint);
    shaderStages.push_back({
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = *detail::toVkShaderStage(stage.stage),
        .module = shaderModules.back(),
        .pName = entryPoints.back().c_str(),
    });
  }

  Result<VkPipelineLayout> layoutResult = createEmptyPipelineLayout(state_.device);
  if (not layoutResult)
    return bail(layoutResult.error().release());

  const VkPipelineLayout layout = *layoutResult;
  auto destroyLayout =
      std::scope_exit([&] -> void { vkDestroyPipelineLayout(state_.device, layout, nullptr); });

  Vec<VkPipelineColorBlendAttachmentState> colorBlendAttachments{colorFormats.size(),
      VkPipelineColorBlendAttachmentState{
          .blendEnable = VK_FALSE,
          .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                            VK_COLOR_COMPONENT_A_BIT,
      }};
  colorBlendAttachments.reserve(colorFormats.size());

  const VkPipelineRenderingCreateInfo renderingInfo{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
      .colorAttachmentCount = static_cast<u32>(colorFormats.size()),
      .pColorAttachmentFormats = colorFormats.data(),
      .depthAttachmentFormat = depthFormat,
      .stencilAttachmentFormat = stencilFormat,
  };
  const VkPipelineVertexInputStateCreateInfo vertexInputInfo{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
  };
  const VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
  };
  const VkPipelineViewportStateCreateInfo viewportInfo{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount = 1,
      .scissorCount = 1,
  };
  const VkPipelineRasterizationStateCreateInfo rasterizationInfo{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .polygonMode = VK_POLYGON_MODE_FILL,
      .cullMode = VK_CULL_MODE_BACK_BIT,
      .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
      .lineWidth = 1.0F,
  };
  const VkPipelineMultisampleStateCreateInfo multisampleInfo{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
  };

  VkPipelineDepthStencilStateCreateInfo depthStencilInfo{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .depthTestEnable = depthFormat == VK_FORMAT_UNDEFINED ? VK_FALSE : VK_TRUE,
      .depthWriteEnable = depthFormat == VK_FORMAT_UNDEFINED ? VK_FALSE : VK_TRUE,
      .depthCompareOp = VK_COMPARE_OP_LESS,
      .stencilTestEnable = stencilFormat == VK_FORMAT_UNDEFINED ? VK_FALSE : VK_TRUE,
  };
  depthStencilInfo.front.compareOp = VK_COMPARE_OP_ALWAYS;
  depthStencilInfo.back.compareOp = VK_COMPARE_OP_ALWAYS;

  const VkPipelineColorBlendStateCreateInfo colorBlendInfo{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .attachmentCount = static_cast<u32>(colorBlendAttachments.size()),
      .pAttachments = colorBlendAttachments.data(),
  };
  const Array<VkDynamicState, 2> dynamicStates{
      VK_DYNAMIC_STATE_VIEWPORT,
      VK_DYNAMIC_STATE_SCISSOR,
  };
  const VkPipelineDynamicStateCreateInfo dynamicStateInfo{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
      .dynamicStateCount = static_cast<u32>(dynamicStates.size()),
      .pDynamicStates = dynamicStates.data(),
  };
  const VkGraphicsPipelineCreateInfo createInfo{
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .pNext = &renderingInfo,
      .stageCount = static_cast<u32>(shaderStages.size()),
      .pStages = shaderStages.data(),
      .pVertexInputState = &vertexInputInfo,
      .pInputAssemblyState = &inputAssemblyInfo,
      .pViewportState = &viewportInfo,
      .pRasterizationState = &rasterizationInfo,
      .pMultisampleState = &multisampleInfo,
      .pDepthStencilState = &depthStencilInfo,
      .pColorBlendState = &colorBlendInfo,
      .pDynamicState = &dynamicStateInfo,
      .layout = layout,
  };

  VkPipeline pipeline{};
  if (vkCreateGraphicsPipelines(state_.device, VK_NULL_HANDLE, 1, &createInfo, nullptr, &pipeline) !=
      VK_SUCCESS)
    return bail({"Failed to create a Vulkan graphics pipeline."});

  auto destroyPipeline =
      std::scope_exit([&] -> void { vkDestroyPipeline(state_.device, pipeline, nullptr); });

  Result<PipelineHandle> handleResult = pipelines_.emplace(PipelineDescriptor{
      .label = desc.label,
      .bindPoint = PipelineBindPoint::Graphics,
  });
  if (not handleResult)
    return bail(handleResult.error().release());

  const PipelineHandle handle = *handleResult;
  auto eraseRecord = std::scope_exit([&] -> void {
    // NOLINTNEXTLINE(bugprone-unused-return-value)
    static_cast<void>(pipelines_.erase(handle));
  });
  nativePipelines_.emplace(handle,
      detail::VulkanPipelineState{
          .pipeline = pipeline,
          .layout = layout,
      });

  destroyPipeline.release();
  destroyLayout.release();
  destroyShaderModules.release();
  eraseRecord.release();
  return handle;
}

auto Device::createComputePipeline(const ComputePipelineDescriptor &desc) -> Result<PipelineHandle> {
  if (state_.device == VK_NULL_HANDLE)
    return bail({"Cannot create a compute pipeline on an invalid device."});

  if (desc.stage.stage != ShaderStage::Compute)
    return bail({"A compute pipeline requires a compute shader stage."});

  if (Result<void> result = validateShaderStage(desc.stage); not result)
    return bail(result.error().release());

  Result<VkShaderModule> shaderModuleResult = createShaderModule(state_.device, desc.stage);
  if (not shaderModuleResult)
    return bail(shaderModuleResult.error().release());

  const VkShaderModule shaderModule = *shaderModuleResult;
  auto destroyShaderModule =
      std::scope_exit([&] -> void { vkDestroyShaderModule(state_.device, shaderModule, nullptr); });

  Result<VkPipelineLayout> layoutResult = createEmptyPipelineLayout(state_.device);
  if (not layoutResult)
    return bail(layoutResult.error().release());

  const VkPipelineLayout layout = *layoutResult;
  auto destroyLayout =
      std::scope_exit([&] -> void { vkDestroyPipelineLayout(state_.device, layout, nullptr); });

  String entryPoint{desc.stage.entryPoint};
  const VkPipelineShaderStageCreateInfo shaderStageInfo{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_COMPUTE_BIT,
      .module = shaderModule,
      .pName = entryPoint.c_str(),
  };
  const VkComputePipelineCreateInfo createInfo{
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage = shaderStageInfo,
      .layout = layout,
  };

  VkPipeline pipeline{};
  if (vkCreateComputePipelines(state_.device, VK_NULL_HANDLE, 1, &createInfo, nullptr, &pipeline) !=
      VK_SUCCESS)
    return bail({"Failed to create a Vulkan compute pipeline."});

  auto destroyPipeline =
      std::scope_exit([&] -> void { vkDestroyPipeline(state_.device, pipeline, nullptr); });

  Result<PipelineHandle> handleResult = pipelines_.emplace(PipelineDescriptor{
      .label = desc.label,
      .bindPoint = PipelineBindPoint::Compute,
  });
  if (not handleResult)
    return bail(handleResult.error().release());

  const PipelineHandle handle = *handleResult;
  auto eraseRecord = std::scope_exit([&] -> void {
    // NOLINTNEXTLINE(bugprone-unused-return-value)
    static_cast<void>(pipelines_.erase(handle));
  });
  nativePipelines_.emplace(handle,
      detail::VulkanPipelineState{
          .pipeline = pipeline,
          .layout = layout,
      });

  destroyPipeline.release();
  destroyLayout.release();
  destroyShaderModule.release();
  eraseRecord.release();
  return handle;
}

auto Device::destroy(TextureHandle handle) -> Result<void> {
  if (not textures_.contains(handle))
    return bail({"Attempted to destroy an invalid texture handle."});

  Result<Ref<Texture>> textureResult = textures_.get(handle);
  if (not textureResult)
    return bail(textureResult.error().release());

  if (textureResult->get().viewCount_ != 0)
    return bail({"Cannot destroy a texture while texture views reference it."});

  const auto iterator = nativeTextures_.find(handle);
  if (iterator == nativeTextures_.end())
    return bail({"Texture handle has no Vulkan resource state."});

  if (state_.allocator != VK_NULL_HANDLE and iterator->second.image != VK_NULL_HANDLE and
      iterator->second.allocation != VK_NULL_HANDLE)
    vmaDestroyImage(state_.allocator, iterator->second.image, iterator->second.allocation);

  nativeTextures_.erase(iterator);
  return textures_.erase(handle);
}

auto Device::destroy(TextureViewHandle handle) -> Result<void> {
  Result<Ref<TextureView>> viewResult = textureViews_.get(handle);
  if (not viewResult)
    return bail(viewResult.error().release());

  TextureView &view = viewResult->get();
  Result<Ref<Texture>> textureResult = textures_.get(view.descriptor().texture);
  if (not textureResult)
    return bail({"Texture view refers to an invalid texture record"});

  const auto iterator = nativeTextureViews_.find(handle);
  if (iterator == nativeTextureViews_.end())
    return bail({"Texture view handle has no Vulkan image-view state."});

  if (state_.device != VK_NULL_HANDLE and iterator->second.imageView != VK_NULL_HANDLE)
    vkDestroyImageView(state_.device, iterator->second.imageView, nullptr);

  nativeTextureViews_.erase(iterator);
  if (textureResult->get().viewCount_ != 0)
    --textureResult->get().viewCount_;

  return textureViews_.erase(handle);
}

auto Device::destroy(PipelineHandle handle) -> Result<void> {
  if (not pipelines_.contains(handle))
    return bail({"Attempted to destroy an invalid pipeline handle."});

  const auto iterator = nativePipelines_.find(handle);
  if (iterator == nativePipelines_.end())
    return bail({"Pipeline handle has no Vulkan pipeline state."});

  if (state_.device != VK_NULL_HANDLE and iterator->second.pipeline != VK_NULL_HANDLE)
    vkDestroyPipeline(state_.device, iterator->second.pipeline, nullptr);

  if (state_.device != VK_NULL_HANDLE and iterator->second.layout != VK_NULL_HANDLE)
    vkDestroyPipelineLayout(state_.device, iterator->second.layout, nullptr);

  nativePipelines_.erase(iterator);
  return pipelines_.erase(handle);
}

auto Device::texture(TextureHandle handle) noexcept -> Result<Ref<Texture>> {
  return textures_.get(handle);
}

auto Device::texture(TextureHandle handle) const noexcept -> Result<Ref<const Texture>> {
  return textures_.get(handle);
}

auto Device::textureView(TextureViewHandle handle) noexcept -> Result<Ref<TextureView>> {
  return textureViews_.get(handle);
}

auto Device::textureView(TextureViewHandle handle) const noexcept -> Result<Ref<const TextureView>> {
  return textureViews_.get(handle);
}

auto Device::pipeline(PipelineHandle handle) noexcept -> Result<Ref<Pipeline>> {
  return pipelines_.get(handle);
}

auto Device::pipeline(PipelineHandle handle) const noexcept -> Result<Ref<const Pipeline>> {
  return pipelines_.get(handle);
}

auto Device::nativeImage(TextureHandle handle) const noexcept -> Option<VkImage> {
  const auto iterator = nativeTextures_.find(handle);
  if (iterator == nativeTextures_.end() or iterator->second.image == VK_NULL_HANDLE)
    return None;

  return iterator->second.image;
}

auto Device::nativeImageView(TextureViewHandle handle) const noexcept -> Option<VkImageView> {
  const auto iterator = nativeTextureViews_.find(handle);
  if (iterator == nativeTextureViews_.end() or iterator->second.imageView == VK_NULL_HANDLE)
    return None;

  return iterator->second.imageView;
}

auto Device::nativePipeline(PipelineHandle handle) const noexcept -> Option<VkPipeline> {
  const auto iterator = nativePipelines_.find(handle);
  if (iterator == nativePipelines_.end() or iterator->second.pipeline == VK_NULL_HANDLE)
    return None;

  return iterator->second.pipeline;
}

auto Device::nativeHandle() const noexcept -> VkDevice {
  return state_.device;
}

auto Device::features() const noexcept -> DeviceFeatures {
  return state_.features;
}

} // namespace Nyx::RHI
