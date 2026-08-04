export module Nyx.Event:Events;

import std;
import Nyx.Core;

export namespace Nyx {

using WindowId = u32;

enum class[[= debug::derive]] EventType : u8 {
  QuitRequested,
  WindowCloseRequested,
  WindowClosed,
  WindowResized,
  Key,
  TextInput,
  MouseMotion,
  MouseButton,
  MouseWheel,
};

struct[[= debug::derive]] QuitRequestedEvent {
  u64 timestamp{};
};

struct[[= debug::derive]] WindowCloseRequestedEvent {
  WindowId windowId{};
  u64 timestamp{};
};

struct[[= debug::derive]] WindowClosedEvent {
  WindowId windowId{};
  u64 timestamp{};
};

struct[[= debug::derive]] WindowResizedEvent {
  WindowId windowId{};
  u32 width{};
  u32 height{};
  u32 pixelWidth{};
  u32 pixelHeight{};
  u64 timestamp{};
};

struct[[= debug::derive]] KeyEvent {
  WindowId windowId{};
  i32 key{};
  i32 scancode{};
  u16 modifiers{};
  u16 rawScancode{};
  bool pressed{};
  bool repeat{};
  u64 timestamp{};
};

struct[[= debug::derive]] TextInputEvent {
  WindowId windowId{};
  String text;
  u64 timestamp{};
};

struct[[= debug::derive]] MouseMotionEvent {
  WindowId windowId{};
  f32 x{};
  f32 y{};
  f32 deltaX{};
  f32 deltaY{};
  u32 buttons{};
  u64 timestamp{};
};

struct[[= debug::derive]] MouseButtonEvent {
  WindowId windowId{};
  u8 button{};
  u8 clicks{};
  f32 x{};
  f32 y{};
  bool pressed{};
  u64 timestamp{};
};

struct[[= debug::derive]] MouseWheelEvent {
  WindowId windowId{};
  f32 x{};
  f32 y{};
  u64 timestamp{};
};

using Event = std::variant<QuitRequestedEvent,
    WindowCloseRequestedEvent,
    WindowClosedEvent,
    WindowResizedEvent,
    KeyEvent,
    TextInputEvent,
    MouseMotionEvent,
    MouseButtonEvent,
    MouseWheelEvent>;

constexpr auto eventType(const Event &event) noexcept -> EventType {
  if (std::holds_alternative<QuitRequestedEvent>(event))
    return EventType::QuitRequested;
  if (std::holds_alternative<WindowCloseRequestedEvent>(event))
    return EventType::WindowCloseRequested;
  if (std::holds_alternative<WindowClosedEvent>(event))
    return EventType::WindowClosed;
  if (std::holds_alternative<WindowResizedEvent>(event))
    return EventType::WindowResized;
  if (std::holds_alternative<KeyEvent>(event))
    return EventType::Key;
  if (std::holds_alternative<TextInputEvent>(event))
    return EventType::TextInput;
  if (std::holds_alternative<MouseMotionEvent>(event))
    return EventType::MouseMotion;
  if (std::holds_alternative<MouseButtonEvent>(event))
    return EventType::MouseButton;

  return EventType::MouseWheel;
}

}
