export module Nyx.Event:EventBus;

import std;
import Miracle;

import :Events;

using namespace Miracle;

export namespace Nyx {

template <class T>
concept EventValue = std::constructible_from<Event, T>;

class EventBus final {
public:
  EventBus() = default;
  ~EventBus() noexcept = default;

  EventBus(const EventBus &) = delete ("An event bus owns queued events and cannot be copied.");
  auto operator=(const EventBus &)
      -> EventBus & = delete ("An event bus owns queued events and cannot be copied.");

  EventBus(EventBus &&) noexcept = default;
  auto operator=(EventBus &&) noexcept -> EventBus & = default;

  auto publish(Event event) -> void;

  template <EventValue T>
  auto publish(T &&event) -> void {
    events_.emplace_back(std::forward<T>(event));
  }

  [[nodiscard]]
  auto empty() const noexcept -> bool;

  [[nodiscard]]
  auto size() const noexcept -> usize;

  [[nodiscard]]
  auto pending() const noexcept -> Span<const Event>;

  auto drain() noexcept -> Vec<Event>;

  auto clear() noexcept -> void;

private:
  Vec<Event> events_;
};

}
