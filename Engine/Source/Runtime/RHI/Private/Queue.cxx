module;

#include <volk.h>
#include <vulkan/vulkan_core.h>

module Nyx.RHI;

import std;
import Nyx.Core;
import :Queue;

namespace Nyx::RHI {

Queue::Queue(Queue &&other) noexcept
    : state_(std::exchange(other.state_, {})) {
}

auto Queue::operator=(Queue &&other) noexcept -> Queue & {
  if (this == &other)
    return *this;

  state_ = std::exchange(other.state_, {});
  return *this;
}

auto Queue::info() const noexcept -> const QueueInfo & {
  return state_.info;
}

auto Queue::nativeHandle() const noexcept -> VkQueue {
  return state_.queue;
}

auto Queue::waitIdle() const noexcept -> Result<void> {
  if (state_.queue == VK_NULL_HANDLE)
    return bail({"Cannot wait on an invalid Vulkan queue."});

  if (vkQueueWaitIdle(state_.queue) != VK_SUCCESS)
    return bail({"Failed to wait for the Vulkan queue."});

  return {};
}

}
