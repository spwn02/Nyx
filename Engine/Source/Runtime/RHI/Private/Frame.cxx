module;

#include <volk.h>
#include <vulkan/vulkan_core.h>

module Nyx.RHI;

import std;
import Nyx.Core;

import :Frame;

namespace Nyx::RHI {

namespace {

constexpr auto destroyFrameResources(VkDevice device, Vec<detail::VulkanFrameResources> &resources) noexcept
    -> void {
  if (device != VK_NULL_HANDLE) {
    for (detail::VulkanFrameResources &resource : resources) {
      if (resource.imageAcquiredSemaphore != VK_NULL_HANDLE)
        vkDestroySemaphore(device, resource.imageAcquiredSemaphore, nullptr);

      if (resource.commandPool != VK_NULL_HANDLE)
        vkDestroyCommandPool(device, resource.commandPool, nullptr);
    }
  }

  resources.clear();
}

} // namespace

auto FrameScheduler::create(const Device &device, const FrameSchedulerDescriptor &desc)
    -> Result<FrameScheduler> {
  if (device.nativeHandle() == VK_NULL_HANDLE)
    return bail({"Cannot create a frame scheduler for an invalid device."});

  constexpr DeviceFeatures requiredFeatures =
      DeviceFeature::TimelineSemaphore | DeviceFeature::Synchronization2;
  if (bits(device.features() & requiredFeatures) != bits(requiredFeatures))
    return bail({"A frame scheduler requires timeline semaphores and synchronization2."});

  if (desc.framesInFlight == 0)
    return bail({"A frame scheduler requires at least one frame in flight."});

  const Option<Ref<const Queue>> queue = device.queue(desc.queueRole);
  if (not queue)
    return bail({"The requested frame scheduler queue is unavailable."});

  if (queue->get().nativeHandle() == VK_NULL_HANDLE)
    return bail({"The requested frame scheduler queue is invalid."});

  if (not queue->get().info().supportsPresent)
    return bail({"The frame scheduler queue cannot present to a surface."});

  FrameScheduler scheduler{};
  scheduler.deviceOwner_ = &device;
  scheduler.device_ = device.nativeHandle();
  scheduler.queue_ = queue->get().nativeHandle();
  scheduler.queueFamilyIndex_ = queue->get().info().familyIndex;
  scheduler.frameResources_.resize(desc.framesInFlight);
  scheduler.nextSignalValue_ = desc.framesInFlight + 1;

  const VkSemaphoreTypeCreateInfo timelineTypeInfo{
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
      .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
      .initialValue = desc.framesInFlight,
  };
  const VkSemaphoreCreateInfo timelineCreateInfo{
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
      .pNext = &timelineTypeInfo,
  };

  if (vkCreateSemaphore(scheduler.device_, &timelineCreateInfo, nullptr, &scheduler.timelineSemaphore_) !=
      VK_SUCCESS)
    return bail({"Failed to create the frame timeline semaphore."});

  for (auto &resource : scheduler.frameResources_) {
    const VkSemaphoreCreateInfo semaphoreInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };
    if (vkCreateSemaphore(scheduler.device_, &semaphoreInfo, nullptr, &resource.imageAcquiredSemaphore) !=
        VK_SUCCESS)
      return bail({"Failed to create an image-acquire semaphore."});

    const VkCommandPoolCreateInfo poolInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = scheduler.queueFamilyIndex_,
    };
    if (vkCreateCommandPool(scheduler.device_, &poolInfo, nullptr, &resource.commandPool) != VK_SUCCESS)
      return bail({"Failed to create a frame command pool."});

    const VkCommandBufferAllocateInfo allocationInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = resource.commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    if (vkAllocateCommandBuffers(scheduler.device_, &allocationInfo, &resource.commandBuffer) != VK_SUCCESS)
      return bail({"Failed to allocate a frame command buffer."});
  }

  return scheduler;
}

FrameScheduler::FrameScheduler(FrameScheduler &&other) noexcept
    : deviceOwner_(std::exchange(other.deviceOwner_, nullptr))
    , device_(std::exchange(other.device_, {}))
    , queue_(std::exchange(other.queue_, {}))
    , queueFamilyIndex_(std::exchange(other.queueFamilyIndex_, {}))
    , timelineSemaphore_(std::exchange(other.timelineSemaphore_, {}))
    , frameResources_(std::exchange(other.frameResources_, {}))
    , renderCompleteSemaphores_(std::exchange(other.renderCompleteSemaphores_, {}))
    , frameIndex_(std::exchange(other.frameIndex_, {}))
    , nextSignalValue_(std::exchange(other.nextSignalValue_, {}))
    , active_(std::exchange(other.active_, {})) {
}

auto FrameScheduler::operator=(FrameScheduler &&other) noexcept -> FrameScheduler & {
  if (this == &other)
    return *this;

  if (device_ != VK_NULL_HANDLE)
    static_cast<void>(vkDeviceWaitIdle(device_));
  destroyState();

  deviceOwner_ = std::exchange(other.deviceOwner_, nullptr);
  device_ = std::exchange(other.device_, {});
  queue_ = std::exchange(other.queue_, {});
  queueFamilyIndex_ = std::exchange(other.queueFamilyIndex_, {});
  timelineSemaphore_ = std::exchange(other.timelineSemaphore_, {});
  frameResources_ = std::exchange(other.frameResources_, {});
  renderCompleteSemaphores_ = std::exchange(other.renderCompleteSemaphores_, {});
  frameIndex_ = std::exchange(other.frameIndex_, {});
  nextSignalValue_ = std::exchange(other.nextSignalValue_, {});
  active_ = std::exchange(other.active_, {});
  return *this;
}

auto FrameScheduler::rebindDeviceOwner(const Device &device) noexcept -> Result<void> {
  if (device.nativeHandle() != device_)
    return bail({"Cannot rebind a frame scheduler to a different device."});

  deviceOwner_ = &device;
  return {};
}

FrameScheduler::~FrameScheduler() noexcept {
  if (device_ != VK_NULL_HANDLE)
    static_cast<void>(vkDeviceWaitIdle(device_));

  destroyState();
}

auto FrameScheduler::destroyState() noexcept -> void {
  if (device_ != VK_NULL_HANDLE)
    for (const VkSemaphore semaphore : renderCompleteSemaphores_) {
      if (semaphore != VK_NULL_HANDLE)
        vkDestroySemaphore(device_, semaphore, nullptr);
    }
  renderCompleteSemaphores_.clear();

  destroyFrameResources(device_, frameResources_);

  if (device_ != VK_NULL_HANDLE and timelineSemaphore_ != VK_NULL_HANDLE)
    vkDestroySemaphore(device_, timelineSemaphore_, nullptr);

  deviceOwner_ = nullptr;
  device_ = {};
  queue_ = {};
  queueFamilyIndex_ = {};
  timelineSemaphore_ = {};
  frameIndex_ = {};
  nextSignalValue_ = {};
  active_ = {};
}

auto FrameScheduler::ensureRenderCompleteSemaphores(const Swapchain &swapchain) -> Result<void> {
  if (swapchain.nativeHandle() == VK_NULL_HANDLE)
    return bail({"Cannot synchronize an invalid swapchain."});

  const u32 imageCount = swapchain.imageCount();
  if (imageCount == 0)
    return bail({"Canot synchronize a swapchain without images."});

  if (renderCompleteSemaphores_.size() == imageCount)
    return {};

  if (vkDeviceWaitIdle(device_) != VK_SUCCESS)
    return bail({"Failed to idle the device while resizing frame synchronization."});

  for (const VkSemaphore semaphore : renderCompleteSemaphores_)
    vkDestroySemaphore(device_, semaphore, nullptr);
  renderCompleteSemaphores_.clear();
  renderCompleteSemaphores_.resize(imageCount);

  const VkSemaphoreCreateInfo semaphoreInfo{
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
  };
  for (VkSemaphore &semaphore : renderCompleteSemaphores_) {
    if (vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &semaphore) != VK_SUCCESS) {
      for (const VkSemaphore created : renderCompleteSemaphores_) {
        if (created != VK_NULL_HANDLE)
          vkDestroySemaphore(device_, created, nullptr);
      }
      renderCompleteSemaphores_.clear();
      return bail({"Failed to create a render-complete semaphore."});
    }
  }

  return {};
}

auto FrameScheduler::begin(Swapchain &swapchain) -> Result<Frame> {
  if (active_)
    return bail({"A frame is already active on this scheduler."});

  if (deviceOwner_ == nullptr or device_ == VK_NULL_HANDLE or queue_ == VK_NULL_HANDLE)
    return bail({"Cannot begin a frame on an invalid scheduler."});

  if (swapchain.deviceHandle() != device_)
    return bail({"The swapchain belongs to a different device."});

  Result<void> semaphoreResult = ensureRenderCompleteSemaphores(swapchain);
  if (not semaphoreResult)
    return bail(semaphoreResult.error().release());

  const u32 frameResourceIndex = static_cast<u32>(frameIndex_ % frameResources_.size());
  detail::VulkanFrameResources &resource = frameResources_[frameResourceIndex];
  const u64 signalValue = nextSignalValue_;
  const u64 waitValue = signalValue - static_cast<u64>(frameResources_.size());

  const VkSemaphoreWaitInfo waitInfo{
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
      .semaphoreCount = 1,
      .pSemaphores = &timelineSemaphore_,
      .pValues = &waitValue,
  };
  if (vkWaitSemaphores(device_, &waitInfo, std::numeric_limits<u64>::max()) != VK_SUCCESS)
    return bail({"Failed to wait for the frame timeline semaphore."});

  if (vkResetCommandPool(device_, resource.commandPool, 0) != VK_SUCCESS)
    return bail({"Failed to reset the frame command pool."});

  u32 imageIndex{};
  const auto acquireResult = vkAcquireNextImageKHR(device_,
      swapchain.nativeHandle(),
      std::numeric_limits<u64>::max(),
      resource.imageAcquiredSemaphore,
      VK_NULL_HANDLE,
      &imageIndex);

  if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR)
    return bail({"Swapchain is out of date and must be recreated."});

  if (acquireResult != VK_SUCCESS and acquireResult != VK_SUBOPTIMAL_KHR)
    return bail({"Failed to acquire a swapchain image."});

  const VkCommandBufferBeginInfo beginInfo{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
  };
  if (vkBeginCommandBuffer(resource.commandBuffer, &beginInfo) != VK_SUCCESS)
    return bail({"Failed to begin recording the frame command buffer."});

  Frame frame{};
  frame.scheduler_ = this;
  frame.swapchain_ = &swapchain;
  frame.commandBuffer_ = CommandBuffer{resource.commandBuffer, deviceOwner_};
  frame.frameIndex_ = frameIndex_;
  frame.signalValue_ = signalValue;
  frame.frameResourceIndex_ = frameResourceIndex;
  frame.imageIndex_ = imageIndex;
  frame.acquiredSuboptimal_ = acquireResult == VK_SUBOPTIMAL_KHR;
  frame.active_ = true;
  active_ = true;
  return frame;
}

auto FrameScheduler::finish(Frame &frame) noexcept -> Result<FrameStatus> {
  if (not frame.active_ or frame.scheduler_ != this or frame.swapchain_ == nullptr)
    return bail({"The frame does not belong to this scheduler."});

  detail::VulkanFrameResources &resource = frameResources_[frame.frameResourceIndex_];
  if (frame.commandBuffer_.rendering_)
    return bail({"Cannot finish a frame while dynamic rendering is active."});

  frame.commandBuffer_.recording_ = false;
  frame.commandBuffer_.rendering_ = false;

  if (vkEndCommandBuffer(resource.commandBuffer) != VK_SUCCESS) {
    abort(frame);
    return bail({"Failed to finish recording the frame command buffer."});
  }

  const VkSemaphoreSubmitInfo waitInfo{
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
      .semaphore = resource.imageAcquiredSemaphore,
      .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
  };
  const Array<VkSemaphoreSubmitInfo, 2> signalInfos{
      VkSemaphoreSubmitInfo{
          .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
          .semaphore = renderCompleteSemaphores_[frame.imageIndex_],
          .stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
      },
      VkSemaphoreSubmitInfo{
          .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
          .semaphore = timelineSemaphore_,
          .value = frame.signalValue_,
          .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
      },
  };
  const VkCommandBufferSubmitInfo commandInfo{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
      .commandBuffer = resource.commandBuffer,
  };
  const VkSubmitInfo2 submitInfo{
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
      .waitSemaphoreInfoCount = 1,
      .pWaitSemaphoreInfos = &waitInfo,
      .commandBufferInfoCount = 1,
      .pCommandBufferInfos = &commandInfo,
      .signalSemaphoreInfoCount = static_cast<u32>(signalInfos.size()),
      .pSignalSemaphoreInfos = signalInfos.data(),
  };

  if (vkQueueSubmit2(queue_, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
    abort(frame);
    return bail({"Failed to submit the frame command buffer."});
  }

  ++nextSignalValue_;
  ++frameIndex_;
  frame.active_ = false;
  active_ = false;

  const VkSwapchainKHR nativeSwapchain = frame.swapchain_->nativeHandle();
  const VkPresentInfoKHR presentInfo{
      .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
      .waitSemaphoreCount = 1,
      .pWaitSemaphores = &renderCompleteSemaphores_[frame.imageIndex_],
      .swapchainCount = 1,
      .pSwapchains = &nativeSwapchain,
      .pImageIndices = &frame.imageIndex_,
  };
  const VkResult presentResult = vkQueuePresentKHR(queue_, &presentInfo);
  if (presentResult == VK_SUCCESS and not frame.acquiredSuboptimal_)
    return FrameStatus::Presented;

  if (presentResult == VK_SUCCESS or presentResult == VK_SUBOPTIMAL_KHR or
      presentResult == VK_ERROR_OUT_OF_DATE_KHR)
    return FrameStatus::NeedsRecreate;

  return bail({"Failed to present the swapchain image."});
}

auto FrameScheduler::abort(Frame &frame) noexcept -> void {
  if (not frame.active_ or frame.scheduler_ != this)
    return;

  detail::VulkanFrameResources &resource = frameResources_[frame.frameResourceIndex_];
  static_cast<void>(vkResetCommandPool(device_, resource.commandPool, 0));
  frame.commandBuffer_.rendering_ = false;
  frame.commandBuffer_.recording_ = false;
  frame.active_ = false;
  active_ = false;
}

Frame::~Frame() noexcept {
  if (active_ and scheduler_ != nullptr)
    scheduler_->abort(*this);
}

Frame::Frame(Frame &&other) noexcept
    : scheduler_(std::exchange(other.scheduler_, nullptr))
    , swapchain_(std::exchange(other.swapchain_, nullptr))
    , commandBuffer_(std::exchange(other.commandBuffer_, {}))
    , frameIndex_(std::exchange(other.frameIndex_, {}))
    , signalValue_(std::exchange(other.signalValue_, {}))
    , frameResourceIndex_(std::exchange(other.frameResourceIndex_, {}))
    , imageIndex_(std::exchange(other.imageIndex_, {}))
    , acquiredSuboptimal_(std::exchange(other.acquiredSuboptimal_, {}))
    , active_(std::exchange(other.active_, {})) {
}

auto Frame::operator=(Frame &&other) noexcept -> Frame & {
  if (this == &other)
    return *this;

  if (active_ and scheduler_ != nullptr)
    scheduler_->abort(*this);

  scheduler_ = std::exchange(other.scheduler_, nullptr);
  swapchain_ = std::exchange(other.swapchain_, nullptr);
  commandBuffer_ = std::exchange(other.commandBuffer_, {});
  frameIndex_ = std::exchange(other.frameIndex_, {});
  signalValue_ = std::exchange(other.signalValue_, {});
  frameResourceIndex_ = std::exchange(other.frameResourceIndex_, {});
  imageIndex_ = std::exchange(other.imageIndex_, {});
  acquiredSuboptimal_ = std::exchange(other.acquiredSuboptimal_, {});
  active_ = std::exchange(other.active_, {});
  return *this;
}

auto Frame::frameIndex() const noexcept -> u64 {
  return frameIndex_;
}

auto Frame::imageIndex() const noexcept -> u32 {
  return imageIndex_;
}

auto Frame::commandBuffer() noexcept -> CommandBuffer & {
  return commandBuffer_;
}

auto Frame::commandBuffer() const noexcept -> const CommandBuffer & {
  return commandBuffer_;
}

auto Frame::finish() noexcept -> Result<FrameStatus> {
  if (scheduler_ == nullptr)
    return bail({"Cannot finish a frame without a scheduler."});

  return scheduler_->finish(*this);
}

} // namespace Nyx::RHI
