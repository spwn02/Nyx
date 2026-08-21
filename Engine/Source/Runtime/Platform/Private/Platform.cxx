module;

#include <SDL3/SDL.h>

module Nyx.Platform;

import std;

namespace Nyx {

namespace {

constexpr auto makeResizeEvent(const SDL_WindowEvent &nativeEvent) noexcept -> WindowResizedEvent {
  auto *window = SDL_GetWindowFromID(nativeEvent.windowID);
  int width = nativeEvent.data1;
  int height = nativeEvent.data2;
  int pixelWidth = nativeEvent.data1;
  int pixelHeight = nativeEvent.data2;

  if (window != nullptr) {
    static_cast<void>(SDL_GetWindowSize(window, &width, &height));
    static_cast<void>(SDL_GetWindowSizeInPixels(window, &pixelWidth, &pixelHeight));
  }

  return {
      .windowId = static_cast<WindowId>(nativeEvent.windowID),
      .width = static_cast<u32>(std::max(width, 0)),
      .height = static_cast<u32>(std::max(height, 0)),
      .pixelWidth = static_cast<u32>(std::max(pixelWidth, 0)),
      .pixelHeight = static_cast<u32>(std::max(pixelHeight, 0)),
      .timestamp = nativeEvent.timestamp,
  };
}

} // namespace

auto Platform::create(const PlatformDescriptor & /*desc*/) noexcept -> Result<Platform> {
  if (not SDL_InitSubSystem(SDL_INIT_VIDEO))
    return bail({"Failed to initialize SDL video: {}", SDL_GetError()});

  Platform platform{};
  platform.videoInitialized_ = true;
  return platform;
}

Platform::Platform(Platform &&other) noexcept
    : videoInitialized_(std::exchange(other.videoInitialized_, false))
    , eventBus_(std::exchange(other.eventBus_, {})) {
}

auto Platform::operator=(Platform &&other) noexcept -> Platform & {
  if (this == &other)
    return *this;

  if (videoInitialized_)
    SDL_QuitSubSystem(SDL_INIT_VIDEO);

  videoInitialized_ = std::exchange(other.videoInitialized_, false);
  eventBus_ = std::exchange(other.eventBus_, {});
  return *this;
}

Platform::~Platform() noexcept {
  if (videoInitialized_)
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

auto Platform::createWindow(const WindowDescriptor &desc) noexcept -> Result<Window> {
  if (not videoInitialized_)
    return bail({"Cannot create a window without an initialized platform."});

  return Window::create(eventBus_, desc);
}

auto Platform::pollEvents() noexcept -> Result<void> {
  if (not videoInitialized_)
    return bail({"Cannot poll events without an initialized platform."});

  SDL_Event nativeEvent{};
  while (SDL_PollEvent(&nativeEvent)) {
    switch (nativeEvent.type) {
      case SDL_EVENT_QUIT:
        eventBus_.publish(Event{QuitRequestedEvent{
            .timestamp = nativeEvent.common.timestamp,
        }});
        break;

      case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        eventBus_.publish(Event{WindowCloseRequestedEvent{
            .windowId = static_cast<WindowId>(nativeEvent.window.windowID),
            .timestamp = nativeEvent.window.timestamp,
        }});
        break;

      case SDL_EVENT_WINDOW_DESTROYED:
        eventBus_.publish(Event{WindowClosedEvent{
            .windowId = static_cast<WindowId>(nativeEvent.window.windowID),
            .timestamp = nativeEvent.window.timestamp,
        }});
        break;

      case SDL_EVENT_WINDOW_RESIZED:
      case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        eventBus_.publish(Event{makeResizeEvent(nativeEvent.window)});
        break;

      case SDL_EVENT_KEY_DOWN:
      case SDL_EVENT_KEY_UP:
        eventBus_.publish(Event{KeyEvent{
            .windowId = static_cast<WindowId>(nativeEvent.key.windowID),
            .key = static_cast<i32>(nativeEvent.key.key),
            .scancode = static_cast<i32>(nativeEvent.key.scancode),
            .modifiers = static_cast<u16>(nativeEvent.key.mod),
            .rawScancode = nativeEvent.key.raw,
            .pressed = nativeEvent.key.down,
            .repeat = nativeEvent.key.repeat,
            .timestamp = nativeEvent.key.timestamp,
        }});
        break;

      case SDL_EVENT_TEXT_INPUT:
        eventBus_.publish(Event{TextInputEvent{
            .windowId = static_cast<WindowId>(nativeEvent.text.windowID),
            .text = nativeEvent.text.text == nullptr ? String{} : String{nativeEvent.text.text},
            .timestamp = nativeEvent.text.timestamp,
        }});
        break;

      case SDL_EVENT_MOUSE_MOTION:
        eventBus_.publish(Event{MouseMotionEvent{
            .windowId = static_cast<WindowId>(nativeEvent.motion.windowID),
            .x = nativeEvent.motion.x,
            .y = nativeEvent.motion.y,
            .deltaX = nativeEvent.motion.xrel,
            .deltaY = nativeEvent.motion.yrel,
            .buttons = static_cast<u32>(nativeEvent.motion.state),
            .timestamp = nativeEvent.motion.timestamp,
        }});
        break;

      case SDL_EVENT_MOUSE_BUTTON_DOWN:
      case SDL_EVENT_MOUSE_BUTTON_UP:
        eventBus_.publish(Event{MouseButtonEvent{
            .windowId = static_cast<WindowId>(nativeEvent.button.windowID),
            .button = nativeEvent.button.button,
            .clicks = nativeEvent.button.clicks,
            .x = nativeEvent.button.x,
            .y = nativeEvent.button.y,
            .pressed = nativeEvent.button.down,
            .timestamp = nativeEvent.button.timestamp,
        }});
        break;

      case SDL_EVENT_MOUSE_WHEEL: {
        auto posX = nativeEvent.wheel.x;
        auto posY = nativeEvent.wheel.y;
        if (nativeEvent.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
          posX = -posX;
          posY = -posY;
        }
        eventBus_.publish(Event{MouseWheelEvent{
            .windowId = static_cast<WindowId>(nativeEvent.wheel.windowID),
            .x = posX,
            .y = posY,
            .timestamp = nativeEvent.wheel.timestamp,
        }});
        break;
      }

      default: break;
    }
  }

  return {};
}

auto Platform::eventBus() noexcept -> EventBus & {
  return eventBus_;
}

auto Platform::eventBus() const noexcept -> const EventBus & {
  return eventBus_;
}

} // namespace Nyx
