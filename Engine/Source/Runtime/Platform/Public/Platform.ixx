export module Nyx.Platform;

import Nyx.Event;

export import :Window;

export namespace Nyx {

struct PlatformDescriptor {};

class Platform final {
public:
  static auto create(const PlatformDescriptor &desc = {}) noexcept -> Result<Platform>;

  ~Platform() noexcept;

  Platform(const Platform &) = delete ("Platform owns the SDL video subsystem and cannot be copied.");
  auto operator=(const Platform &)
      -> Platform & = delete ("Platform owns the SDL video subsystem and cannot be copied.");

  Platform(Platform &&) noexcept;
  auto operator=(Platform &&) noexcept -> Platform &;

  auto createWindow(const WindowDescriptor &desc = {}) noexcept -> Result<Window>;

  auto pollEvents() noexcept -> Result<void>;

  [[nodiscard]]
  auto eventBus() noexcept -> EventBus &;

  [[nodiscard]]
  auto eventBus() const noexcept -> const EventBus &;

private:
  Platform() = default;

  bool videoInitialized_{};
  EventBus eventBus_;
};

}
