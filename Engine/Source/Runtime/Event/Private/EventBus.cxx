module Nyx.Event;

import std;

import :EventBus;

namespace Nyx {

auto EventBus::publish(Event event) -> void {
  events_.push_back(std::move(event));
}

auto EventBus::empty() const noexcept -> bool {
  return events_.empty();
}

auto EventBus::size() const noexcept -> usize {
  return events_.size();
}

auto EventBus::pending() const noexcept -> Span<const Event> {
  return events_;
}

auto EventBus::drain() noexcept -> Vec<Event> {
  return std::exchange(events_, {});
}

auto EventBus::clear() noexcept -> void {
  events_.clear();
}

}
