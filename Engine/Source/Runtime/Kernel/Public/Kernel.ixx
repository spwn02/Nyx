export module Nyx.Kernel;

import Nyx.Core;
import Nyx.Platform;
import Nyx.RHI;

namespace Nyx {

static constexpr f32 defaultRed = 0.01F;
static constexpr f32 defaultGreen = 0.01F;
static constexpr f32 defaultBlue = 0.03F;
static constexpr f32 defaultAlpha = 1.0F;

export struct[[= debug::derive]] KernelDescriptor {
  WindowDescriptor window{};
  RHI::ClearColor clearColor{
      .red = defaultRed,
      .green = defaultGreen,
      .blue = defaultBlue,
      .alpha = defaultAlpha,
  };
  u32 framesInFlight{2};
};

export class Kernel final {
public:
  static auto create(const KernelDescriptor &desc = {}) -> Result<Kernel>;

  ~Kernel() noexcept = default;

  Kernel(const Kernel &) = delete ("A Kernel owns the complete platform and RHI lifetime.");
  auto operator=(const Kernel &)
      -> Kernel & = delete ("A Kernel owns the complete platform and RHI lifetime.");

  Kernel(Kernel &&) noexcept;
  auto operator=(Kernel &&) noexcept -> Kernel &;

  auto run() noexcept -> Result<void>;

private:
  auto processEvents() noexcept -> Result<void>;
  auto configureSwapchain() noexcept -> Result<void>;
  auto render() noexcept -> Result<void>;
  auto rebindRHIOwners() noexcept -> void;

  Kernel() = default;

  /// Declaration order is the ownership order. Destruction therefore runs as FrameScheduler -> Swapchain ->
  /// Device -> Adapter -> Surface -> Instance -> Backend -> Window -> Platform.
  Option<Platform> platform_;
  Option<Window> window_;
  Option<RHI::Backend> backend_;
  Option<RHI::Instance> instance_;
  Option<RHI::Surface> surface_;
  Option<RHI::Adapter> adapter_;
  Option<RHI::Device> device_;
  Option<RHI::Swapchain> swapchain_;
  Option<RHI::FrameScheduler> frameScheduler_;

  RHI::SwapchainDescriptor swapchainDescriptor_;
  RHI::ClearColor clearColor_;
  bool running_{};
  bool resizePending_{};
};

} // namespace Nyx
