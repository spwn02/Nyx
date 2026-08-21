module;

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan_core.h>

export module Nyx.RHI:Frame;

import Miracle;

import :Forward;
import :Types;
import :Device;
import :Swapchain;
import :Command;

using namespace Miracle;

namespace Nyx::RHI {

export enum class[[= debug::derive]] FrameStatus : u8 {
  Presented,
  NeedsRecreate,
};

export struct FrameSchedulerDescriptor {
  u32 framesInFlight{2};
  QueueRole queueRole{QueueRole::Graphics};
};

namespace detail {

struct VulkanFrameResources {
  VkCommandPool commandPool{};
  VkCommandBuffer commandBuffer{};
  VkSemaphore imageAcquiredSemaphore{};
};

} // namespace detail

export class Frame final {
public:
  ~Frame() noexcept;

  Frame(const Frame &) = delete ("A frame is a single-use command submission token.");
  auto operator=(const Frame &) -> Frame & = delete ("A frame is a single-use command submission token.");

  Frame(Frame &&) noexcept;
  auto operator=(Frame &&) noexcept -> Frame &;

  [[nodiscard]]
  auto frameIndex() const noexcept -> u64;

  [[nodiscard]]
  auto imageIndex() const noexcept -> u32;

  auto commandBuffer() noexcept -> CommandBuffer &;

  [[nodiscard]]
  auto commandBuffer() const noexcept -> const CommandBuffer &;

  auto finish() noexcept -> Result<FrameStatus>;

private:
  friend class FrameScheduler;

  Frame() = default;

  FrameScheduler *scheduler_{};
  Swapchain *swapchain_{};
  CommandBuffer commandBuffer_;
  u64 frameIndex_{};
  u64 signalValue_{};
  u32 frameResourceIndex_{};
  u32 imageIndex_{};
  bool acquiredSuboptimal_{};
  bool active_{};
};

export class FrameScheduler final {
public:
  static auto create(const Device &device, const FrameSchedulerDescriptor &desc = {})
      -> Result<FrameScheduler>;

  ~FrameScheduler() noexcept;

  FrameScheduler(const FrameScheduler &) = delete (
      "A frame scheduler owns a command pools and synchronization objects.");
  auto operator=(const FrameScheduler &)
      -> FrameScheduler & = delete ("A frame scheduler owns a command pools and synchronization objects.");

  FrameScheduler(FrameScheduler &&) noexcept;
  auto operator=(FrameScheduler &&) noexcept -> FrameScheduler &;

  auto rebindDeviceOwner(const Device &device) noexcept -> Result<void>;

  auto begin(Swapchain &swapchain) -> Result<Frame>;

private:
  friend class Frame;

  auto finish(Frame &frame) noexcept -> Result<FrameStatus>;
  auto abort(Frame &frame) noexcept -> void;
  auto ensureRenderCompleteSemaphores(const Swapchain &swapchain) -> Result<void>;
  auto destroyState() noexcept -> void;

  FrameScheduler() = default;

  const Device *deviceOwner_{};
  VkDevice device_{};
  VkQueue queue_{};
  u32 queueFamilyIndex_{};
  VkSemaphore timelineSemaphore_{};
  Vec<detail::VulkanFrameResources> frameResources_;
  Vec<VkSemaphore> renderCompleteSemaphores_;
  u64 frameIndex_{};
  u64 nextSignalValue_{};
  bool active_{};
};

} // namespace Nyx::RHI
