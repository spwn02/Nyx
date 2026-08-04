module;

#include <SDL3/SDL.h>

module Nyx.Platform;

import std;
import :Window;

namespace Nyx {

namespace {

constexpr auto queryWindowInfo(SDL_Window *window, WindowId windowId) noexcept -> WindowInfo {
  WindowInfo result{.id = windowId};
  if (window == nullptr)
    return result;

  int width{};
  int height{};
  if (SDL_GetWindowSize(window, &width, &height))
    result.size = {
        .width = static_cast<u32>(std::max(width, 0)),
        .height = static_cast<u32>(std::max(height, 0)),
    };

  if (SDL_GetWindowSizeInPixels(window, &width, &height))
    result.pixelSize = {
        .width = static_cast<u32>(std::max(width, 0)),
        .height = static_cast<u32>(std::max(height, 0)),
    };

  return result;
}

} // namespace

auto Window::create(EventBus &eventBus, const WindowDescriptor &desc) noexcept -> Result<Window> {
  if (desc.width == 0 or desc.height == 0)
    return bail({"A window must have a non-zero size."});

  if (desc.width > static_cast<u32>(std::numeric_limits<int>::max()) or
      desc.height > static_cast<u32>(std::numeric_limits<int>::max()))
    return bail({"A window size exceeds SDL's integer range."});

  SDL_WindowFlags flags = SDL_WINDOW_VULKAN;
  if (desc.resizable)
    flags |= SDL_WINDOW_RESIZABLE;
  if (desc.highPixelDensity)
    flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
  if (desc.initiallyHidden)
    flags |= SDL_WINDOW_HIDDEN;

  String title{desc.title};
  auto *nativeWindow =
      SDL_CreateWindow(title.c_str(), static_cast<int>(desc.width), static_cast<int>(desc.height), flags);
  if (nativeWindow == nullptr)
    return bail({"Failed to create an SDL window: {}", SDL_GetError()});

  Window window{};
  window.nativeWindow_ = nativeWindow;
  window.eventBus_ = &eventBus;
  window.id_ = static_cast<WindowId>(SDL_GetWindowID(nativeWindow));
  if (window.id_ == 0) {
    SDL_DestroyWindow(nativeWindow);
    return bail({"SDL created a window without a valid window id."});
  }

  return window;
}

Window::Window(Window &&other) noexcept
    : nativeWindow_(std::exchange(other.nativeWindow_, nullptr))
    , eventBus_(std::exchange(other.eventBus_, nullptr))
    , id_(std::exchange(other.id_, {})) {
}

auto Window::operator=(Window &&other) noexcept -> Window & {
  if (this == &other)
    return *this;

  if (nativeWindow_ != nullptr)
    SDL_DestroyWindow(nativeWindow_);

  nativeWindow_ = std::exchange(other.nativeWindow_, nullptr);
  eventBus_ = std::exchange(other.eventBus_, nullptr);
  id_ = std::exchange(other.id_, {});
  return *this;
}

Window::~Window() noexcept {
  if (nativeWindow_ != nullptr)
    SDL_DestroyWindow(nativeWindow_);
}

auto Window::nativeHandle() const noexcept -> SDL_Window * {
  return nativeWindow_;
}

auto Window::id() const noexcept -> WindowId {
  return id_;
}

auto Window::info() const noexcept -> WindowInfo {
  return queryWindowInfo(nativeWindow_, id_);
}

auto Window::valid() const noexcept -> bool {
  return nativeWindow_ != nullptr and eventBus_ != nullptr;
}

auto Window::eventBus() noexcept -> EventBus & {
  return *eventBus_;
}

auto Window::eventBus() const noexcept -> const EventBus & {
  return *eventBus_;
}

auto Window::startTextInput() noexcept -> Result<void> {
  if (nativeWindow_ == nullptr)
    return bail({"Cannot start text input on an invalid window."});

  if (not SDL_StartTextInput(nativeWindow_))
    return bail({"Failed to start SDL text input: {}", SDL_GetError()});

  return {};
}

auto Window::stopTextInput() noexcept -> void {
  if (nativeWindow_ != nullptr)
    static_cast<void>(SDL_StopTextInput(nativeWindow_));
}

} // namespace Nyx
