module;

module Nyx.Kernel;

import std;
import Miracle;
import Nyx.Platform;
import Nyx.RHI;
import Nyx.Event;

using namespace Miracle;

namespace Nyx {

Kernel::Kernel(Kernel &&other) noexcept
    : platform_(std::exchange(other.platform_, {}))
    , window_(std::exchange(other.window_, {}))
    , backend_(std::exchange(other.backend_, {}))
    , instance_(std::exchange(other.instance_, {}))
    , surface_(std::exchange(other.surface_, {}))
    , adapter_(std::exchange(other.adapter_, {}))
    , device_(std::exchange(other.device_, {}))
    , swapchain_(std::exchange(other.swapchain_, {}))
    , frameScheduler_(std::exchange(other.frameScheduler_, {}))
    , swapchainDescriptor_(std::exchange(other.swapchainDescriptor_, {}))
    , clearColor_(std::exchange(other.clearColor_, {}))
    , running_(std::exchange(other.running_, false))
    , resizePending_(std::exchange(other.resizePending_, false)) {
  rebindRHIOwners();
}

auto Kernel::operator=(Kernel &&other) noexcept -> Kernel & {
  if (this == &other)
    return *this;

  platform_ = std::exchange(other.platform_, {});
  window_ = std::exchange(other.window_, {});
  backend_ = std::exchange(other.backend_, {});
  instance_ = std::exchange(other.instance_, {});
  surface_ = std::exchange(other.surface_, {});
  adapter_ = std::exchange(other.adapter_, {});
  device_ = std::exchange(other.device_, {});
  swapchain_ = std::exchange(other.swapchain_, {});
  frameScheduler_ = std::exchange(other.frameScheduler_, {});
  swapchainDescriptor_ = std::exchange(other.swapchainDescriptor_, {});
  clearColor_ = std::exchange(other.clearColor_, {});
  running_ = std::exchange(other.running_, false);
  resizePending_ = std::exchange(other.resizePending_, false);

  rebindRHIOwners();
  return *this;
}

auto Kernel::rebindRHIOwners() noexcept -> void {
  // NOLINTBEGIN(bugprone-unused-return-value)
  if (device_.has_value() and frameScheduler_.has_value())
    static_cast<void>(frameScheduler_->rebindDeviceOwner(*device_));
  // NOLINTEND(bugprone-unused-return-value)
}

auto Kernel::create(const KernelDescriptor &desc) -> Result<Kernel> {
  if (desc.framesInFlight == 0)
    return bail({"A Kernel requires at least one frame in flight."});

  Kernel kernel{};
  kernel.clearColor_ = desc.clearColor;

  Result<Platform> platformResult = Platform::create();
  if (not platformResult)
    return bail(platformResult.error().release());
  kernel.platform_ = std::move(*platformResult);

  Result<Window> windowResult = kernel.platform_->createWindow(desc.window);
  if (not windowResult)
    return bail(windowResult.error().release());
  kernel.window_ = std::move(*windowResult);

  Result<RHI::Backend> backendResult = RHI::Backend::create();
  if (not backendResult)
    return bail(backendResult.error().release());
  kernel.backend_ = std::move(*backendResult);

  Result<RHI::Instance> instanceResult = RHI::Instance::create(*kernel.backend_);
  if (not instanceResult)
    return bail(instanceResult.error().release());
  kernel.instance_ = std::move(*instanceResult);

  const RHI::SurfaceDescriptor surfaceDescriptor{
      .window = Ref<const Window>{*kernel.window_},
      .label = "Nyx Engine",
  };
  Result<RHI::Surface> surfaceResult = kernel.instance_->createSurface(surfaceDescriptor);
  if (not surfaceResult)
    return bail(surfaceResult.error().release());
  kernel.surface_ = std::move(*surfaceResult);

  const WindowInfo windowInfo = kernel.window_->info();
  kernel.swapchainDescriptor_ = {
      .label = "nyx-swapchain",
      .extent =
          {
              .width = windowInfo.pixelSize.width,
              .height = windowInfo.pixelSize.height,
          },
      .minImageCount = 2,
      .format = RHI::TextureFormat::Bgra8UnormSrgb,
      .colorSpace = RHI::SurfaceColorSpace::SrgbNonlinear,
      .presentMode = RHI::PresentMode::Fifo,
      .usage = RHI::TextureUsage::ColorAttachment,
      .clipped = true,
  };

  const RHI::QueueRequest queueRequest{
      .role = RHI::QueueRole::Graphics,
      .required = RHI::QueueCapabilities::Graphics,
      .count = 1,
      .priority = 1.0F,
      .presentSurface = Ref<const RHI::Surface>{*kernel.surface_},
  };
  const RHI::DeviceDescriptor deviceDescriptor{
      .label = "nyx-device",
      .queues = Span<const RHI::QueueRequest>{&queueRequest, 1},
  };

  Result<Vec<RHI::Adapter>> adaptersResult = kernel.instance_->enumerateAdapters(*kernel.surface_);
  if (not adaptersResult)
    return bail(adaptersResult.error().release());

  auto compatibleAdapter = std::ranges::find_if(
      *adaptersResult, [&deviceDescriptor](const RHI::Adapter &adapter) constexpr noexcept -> bool {
        return adapter.supports(deviceDescriptor);
      });
  if (compatibleAdapter == adaptersResult->end())
    return bail({"No Vulkan adapter satisfies the clear demo requirements."});
  kernel.adapter_ = std::move(*compatibleAdapter);

  Result<RHI::Device> deviceResult = RHI::Device::create(*kernel.adapter_, deviceDescriptor);
  if (not deviceResult)
    return bail(deviceResult.error().release());
  kernel.device_ = std::move(*deviceResult);

  Result<RHI::Swapchain> swapchainResult =
      RHI::Swapchain::create(*kernel.device_, *kernel.surface_, kernel.swapchainDescriptor_);
  if (not swapchainResult)
    return bail(swapchainResult.error().release());
  kernel.swapchain_ = std::move(*swapchainResult);

  const RHI::FrameSchedulerDescriptor schedulerDescriptor{
      .framesInFlight = desc.framesInFlight,
      .queueRole = RHI::QueueRole::Graphics,
  };
  Result<RHI::FrameScheduler> schedulerResult =
      RHI::FrameScheduler::create(*kernel.device_, schedulerDescriptor);
  if (not schedulerResult)
    return bail(schedulerResult.error().release());
  kernel.frameScheduler_ = std::move(*schedulerResult);

  kernel.running_ = true;
  return kernel;
}

auto Kernel::run() noexcept -> Result<void> {
  while (running_) {
    if (Result<void> result = processEvents(); not result)
      return bail(result.error().release());

    if (not running_)
      break;

    if (Result<void> result = configureSwapchain(); not result)
      return bail(result.error().release());

    if (Result<void> result = render(); not result)
      return bail(result.error().release());
  }

  return {};
}

auto Kernel::processEvents() noexcept -> Result<void> {
  if (not platform_.has_value() or not window_.has_value())
    return bail({"Cannot process events for an unitialized Kernel."});

  Result<void> pollResult = platform_->pollEvents();
  if (not pollResult)
    return bail(pollResult.error().release());

  for (const Event &event : platform_->eventBus().pending()) {
    if (std::get_if<QuitRequestedEvent>(&event) != nullptr) {
      running_ = false;
      continue;
    }

    if (const WindowCloseRequestedEvent *close = std::get_if<WindowCloseRequestedEvent>(&event);
        close != nullptr and close->windowId == window_->id()) {
      running_ = false;
      continue;
    }

    if (const WindowClosedEvent *closed = std::get_if<WindowClosedEvent>(&event);
        closed != nullptr and closed->windowId == window_->id()) {
      running_ = false;
      continue;
    }

    if (const WindowResizedEvent *resized = std::get_if<WindowResizedEvent>(&event);
        resized != nullptr and resized->windowId == window_->id()) {
      swapchainDescriptor_.extent = {
          .width = resized->pixelWidth,
          .height = resized->pixelHeight,
      };
      resizePending_ = true;
    }
  }

  platform_->eventBus().clear();
  return {};
}

auto Kernel::configureSwapchain() noexcept -> Result<void> {
  if (not resizePending_)
    return {};

  if (swapchainDescriptor_.extent.width == 0 or swapchainDescriptor_.extent.height == 0)
    return {};

  if (not swapchain_.has_value())
    return bail({"Cannot recreate an unitialized Kernel swapchain."});

  Result<void> result = swapchain_->configure(swapchainDescriptor_);
  if (not result)
    return bail(result.error().release());

  resizePending_ = false;
  return {};
}

auto Kernel::render() noexcept -> Result<void> {
  if (not frameScheduler_.has_value() or not swapchain_.has_value())
    return bail({"Cannot render without a frame scheduler and swapchain."});

  if (swapchainDescriptor_.extent.width == 0 or swapchainDescriptor_.extent.height == 0)
    return {};

  Result<RHI::Frame> frameResult = frameScheduler_->begin(*swapchain_);
  if (not frameResult)
    return bail(frameResult.error().release());

  RHI::Frame frame = std::move(*frameResult);
  RHI::CommandBuffer &commandBuffer = frame.commandBuffer();
  const RHI::ImageRef image = RHI::swapchainImage(*swapchain_, frame.imageIndex());
  const RHI::ImageViewRef imageView = RHI::swapchainImageView(*swapchain_, frame.imageIndex());

  const Array<RHI::ImageBarrier, 1> acquireBarriers{
      RHI::ImageBarrier{
          .image = image,
          .subresourceRange =
              {
                  .aspects = RHI::TextureAspect::Color,
                  .levelCount = 1,
                  .layerCount = 1,
              },
          .srcStage = RHI::PipelineStage::TopOfPipe,
          .srcAccess = RHI::Access::None,
          .oldLayout = RHI::TextureLayout::Undefined,
          .dstStage = RHI::PipelineStage::ColorAttachmentOutput,
          .dstAccess = RHI::Access::ColorAttachmentWrite,
          .newLayout = RHI::TextureLayout::ColorAttachment,
      },
  };

  if (Result<void> result = commandBuffer.pipelineBarrier(acquireBarriers); not result)
    return bail(result.error().release());

  const Array<RHI::RenderingAttachmentDescriptor, 1> colorAttachments{
      RHI::RenderingAttachmentDescriptor{
          .view = imageView,
          .layout = RHI::TextureLayout::ColorAttachment,
          .loadOp = RHI::LoadOp::Clear,
          .storeOp = RHI::StoreOp::Store,
          .clearColor = clearColor_,
      },
  };
  const RHI::RenderingDescriptor rendering{
      .renderArea =
          {
              .extent = swapchain_->info().extent,
          },
      .layerCount = 1,
      .colorAttachments = colorAttachments,
  };

  if (Result<void> result = commandBuffer.beginRendering(rendering); not result)
    return bail(result.error().release());

  if (Result<void> result = commandBuffer.endRendering(); not result)
    return bail(result.error().release());

  const Array<RHI::ImageBarrier, 1> presentBarriers{
      RHI::ImageBarrier{
          .image = image,
          .subresourceRange =
              {
                  .aspects = RHI::TextureAspect::Color,
                  .levelCount = 1,
                  .layerCount = 1,
              },
          .srcStage = RHI::PipelineStage::ColorAttachmentOutput,
          .srcAccess = RHI::Access::ColorAttachmentWrite,
          .oldLayout = RHI::TextureLayout::ColorAttachment,
          .dstStage = RHI::PipelineStage::BottomOfPipe,
          .dstAccess = RHI::Access::MemoryRead,
          .newLayout = RHI::TextureLayout::Present,
      },
  };

  if (Result<void> result = commandBuffer.pipelineBarrier(presentBarriers); not result)
    return bail(result.error().release());

  Result<RHI::FrameStatus> statusResult = frame.finish();
  if (not statusResult)
    return bail(statusResult.error().release());

  if (*statusResult == RHI::FrameStatus::NeedsRecreate)
    resizePending_ = true;

  return {};
}

} // namespace Nyx
