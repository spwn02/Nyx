module;

#define VK_NO_PROTOTYPES
#include <vk_mem_alloc.h>
#include <vulkan/vulkan_core.h>

export module Nyx.RHI:Device;

import Nyx.Core;
import :Adapter;
import :Forward;
import :Queue;
import :Resources;
import :Types;
import :ResourcePool;

namespace Nyx::RHI {

namespace detail {

struct VulkanDeviceState {
  VkDevice device{};
  VkPhysicalDevice physicalDevice{};
  VkInstance instance{};
  DeviceFeatures features{};
  VmaAllocator allocator{};
};

struct VulkanTextureState {
  VkImage image{};
  VmaAllocation allocation{};
};

struct VulkanTextureViewState {
  VkImageView imageView{};
};

struct VulkanPipelineState {
  VkPipeline pipeline{};
  VkPipelineLayout layout{};
};

} // namespace detail

export class Device final {
public:
  static auto create(const Adapter &adapter, const DeviceDescriptor &desc) -> Result<Device>;

  ~Device() noexcept;

  Device(const Device &) = delete ("Device owns a VkDevice, allocator, queues, and resources.");
  auto operator=(const Device &)
      -> Device & = delete ("Device owns a VkDevice, allocator, queues, and resources.");

  Device(Device &&) noexcept;
  auto operator=(Device &&) noexcept -> Device &;

  [[nodiscard]]
  auto queue(QueueRole role) noexcept -> Option<Ref<Queue>>;

  [[nodiscard]]
  auto queue(QueueRole role) const noexcept -> Option<Ref<const Queue>>;

  [[nodiscard]]
  auto createTexture(const TextureDescriptor &desc) -> Result<TextureHandle>;

  [[nodiscard]]
  auto createTextureView(const TextureViewDescriptor &desc) -> Result<TextureViewHandle>;

  [[nodiscard]]
  auto createGraphicsPipeline(const GraphicsPipelineDescriptor &desc) -> Result<PipelineHandle>;

  [[nodiscard]]
  auto createComputePipeline(const ComputePipelineDescriptor &desc) -> Result<PipelineHandle>;

  auto destroy(TextureHandle handle) -> Result<void>;

  auto destroy(TextureViewHandle handle) -> Result<void>;

  auto destroy(PipelineHandle handle) -> Result<void>;

  [[nodiscard]]
  auto texture(TextureHandle handle) noexcept -> Result<Ref<Texture>>;

  [[nodiscard]]
  auto texture(TextureHandle handle) const noexcept -> Result<Ref<const Texture>>;

  [[nodiscard]]
  auto textureView(TextureViewHandle handle) noexcept -> Result<Ref<TextureView>>;

  [[nodiscard]]
  auto textureView(TextureViewHandle handle) const noexcept -> Result<Ref<const TextureView>>;

  [[nodiscard]]
  auto pipeline(PipelineHandle handle) noexcept -> Result<Ref<Pipeline>>;

  [[nodiscard]]
  auto pipeline(PipelineHandle handle) const noexcept -> Result<Ref<const Pipeline>>;

  [[nodiscard]]
  auto nativeHandle() const noexcept -> VkDevice;

  [[nodiscard]]
  auto features() const noexcept -> DeviceFeatures;

private:
  friend class CommandBuffer;
  friend class Swapchain;

  auto destroyPipelines() noexcept -> void;
  auto destroyTextures() noexcept -> void;
  auto destroyState() noexcept -> void;

  [[nodiscard]]
  auto nativeImage(TextureHandle handle) const noexcept -> Option<VkImage>;

  [[nodiscard]]
  auto nativeImageView(TextureViewHandle handle) const noexcept -> Option<VkImageView>;

  [[nodiscard]]
  auto nativePipeline(PipelineHandle handle) const noexcept -> Option<VkPipeline>;

  Device() = default;

  static auto makeEnabledFeatures(const Adapter &adapter, const DeviceDescriptor &desc) noexcept
      -> Result<DeviceFeatures>;
  static auto collectExtensions(const Adapter &adapter,
      const DeviceDescriptor &desc,
      bool requiresSwapchain) noexcept -> Result<FlatSet<String>>;

  detail::VulkanDeviceState state_;
  detail::ResourcePool<TextureTag, Texture> textures_;
  FlatMap<TextureHandle, detail::VulkanTextureState> nativeTextures_;
  detail::ResourcePool<TextureViewTag, TextureView> textureViews_;
  FlatMap<TextureViewHandle, detail::VulkanTextureViewState> nativeTextureViews_;
  detail::ResourcePool<PipelineTag, Pipeline> pipelines_;
  FlatMap<PipelineHandle, detail::VulkanPipelineState> nativePipelines_;
  Vec<Queue> queues_;
};

} // namespace Nyx::RHI
