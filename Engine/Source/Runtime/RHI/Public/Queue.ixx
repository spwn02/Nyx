module;

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan_core.h>

export module Nyx.RHI:Queue;

import Miracle;

import :Forward;
import :Types;

using namespace Miracle;

namespace Nyx::RHI {

export struct[[= debug::derive]] QueueInfo {
  QueueRole role{QueueRole::Graphics};
  QueueCapabilities capabilities{};
  u32 familyIndex{};
  u32 queueIndex{};
  bool supportsPresent{};
};

namespace detail {

struct VulkanQueueState {
  VkQueue queue{};
  VkDevice device{};
  QueueInfo info;
};

} // namespace detail

export class Queue final {
public:
  ~Queue() noexcept = default;

  Queue(const Queue &) = delete ("Queue contains mutable submission state and cannot be copied.");
  auto operator=(const Queue &)
      -> Queue & = delete ("Queue contains mutable submission state and cannot be copied.");

  Queue(Queue &&) noexcept;
  auto operator=(Queue &&) noexcept -> Queue &;

  [[nodiscard]]
  auto info() const noexcept -> const QueueInfo &;

  [[nodiscard]]
  auto nativeHandle() const noexcept -> VkQueue;

  [[nodiscard]]
  auto waitIdle() const noexcept -> Result<void>;

private:
  friend class Device;

  Queue() = default;

  detail::VulkanQueueState state_;
};

} // namespace Nyx::RHI
