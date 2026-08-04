module;

#include <SDL3/SDL_video.h>

export module Nyx.Platform:Window;

import Nyx.Core;

import Nyx.Event;

export namespace Nyx {

inline constexpr u32 defaultWidth = 1280;
inline constexpr u32 defaultHeight = 1280;

struct[[= debug::derive]] WindowDescriptor {
  String title{"Nyx Engine"};
  u32 width{defaultWidth}, height{defaultHeight};
  bool resizable{true}, highPixelDensity{true}, initiallyHidden{};
};

struct[[= debug::derive]] WindowSize {
  u32 width{};
  u32 height{};
};

struct[[= debug::derive]] WindowInfo {
  WindowId id{};
  WindowSize size{};
  WindowSize pixelSize{};
};

class Window final {
public:
  static auto create(EventBus &eventBus, const WindowDescriptor &desc = {}) noexcept -> Result<Window>;

  ~Window() noexcept;

  Window(const Window &) = delete ("A window owns an SDL window and cannot be copied.");
  auto operator=(const Window &) -> Window & = delete ("A window owns an SDL window and cannot be copied.");

  Window(Window &&) noexcept;
  auto operator=(Window &&) noexcept -> Window &;

  [[nodiscard]]
  auto nativeHandle() const noexcept -> SDL_Window *;

  [[nodiscard]]
  auto id() const noexcept -> WindowId;

  [[nodiscard]]
  auto info() const noexcept -> WindowInfo;

  [[nodiscard]]
  auto valid() const noexcept -> bool;

  [[nodiscard]]
  auto eventBus() noexcept -> EventBus &;
  [[nodiscard]]
  auto eventBus() const noexcept -> const EventBus &;

  auto startTextInput() noexcept -> Result<void>;
  auto stopTextInput() noexcept -> void;

private:
  Window() = default;

  SDL_Window *nativeWindow_{};
  EventBus *eventBus_{};
  WindowId id_{};
};

} // namespace Nyx
