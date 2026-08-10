module Nyx.Core;

import :Profiling;

import std;

namespace Nyx::profiling {

namespace {

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables, readability-identifier-naming)
thread_local ProfileSink *currentSink_{};

} // namespace

auto ProfileSink::begin() noexcept -> usize {
  return activeScopes_++;
}

auto ProfileSink::record(String name,
    std::chrono::steady_clock::duration duration,
    std::source_location location,
    usize depth) -> void {
  duration_ += duration;
  events_.push_back(ProfileEvent{
      .name = std::move(name),
      .duration = duration,
      .location = location,
      .depth = depth,
      .kind = EventKind::Scope,
  });

  const auto found = aggregates_.find(events_.back().name);
  if (found == aggregates_.end()) {
    aggregates_.emplace(events_.back().name,
        ProfileAggregate{
            .count = 1,
            .total = duration,
            .minimum = duration,
            .maximum = duration,
        });
  } else {
    ProfileAggregate &aggregate = found->second;
    ++aggregate.count;
    aggregate.total += duration;
    aggregate.minimum = std::min(aggregate.minimum, duration);
    aggregate.maximum = std::max(aggregate.maximum, duration);
  }

  if (activeScopes_ != 0)
    --activeScopes_;
}

auto ProfileSink::snapshot() const -> ProfileSnapshot {
  return ProfileSnapshot{
      .duration = duration_,
      .events = events_,
      .aggregates = aggregates_,
  };
}

SinkBinding::SinkBinding(ProfileSink &sink) noexcept
    : previous_(currentSink_) {
  currentSink_ = std::addressof(sink);
}

SinkBinding::~SinkBinding() noexcept {
  currentSink_ = previous_;
}

auto currentSink() noexcept -> ProfileSink * {
  return currentSink_;
}

Scope::Scope(StringView name, std::source_location location)
    : sink_(build::profiling ? currentSink() : nullptr)
    , name_(name)
    , location_(location)
    , started_(std::chrono::steady_clock::now())
    , depth_(sink_ == nullptr ? 0 : sink_->begin()) {
}

Scope::~Scope() noexcept {
  if (sink_ == nullptr)
    return;

  try {
    sink_->record(std::move(name_), std::chrono::steady_clock::now() - started_, location_, depth_);
  } catch (...) { // NOLINT
    // Profiling is observational. A reporting allocation must never change the test result.
  }
}

auto profileScope(StringView name, std::source_location location) -> Scope {
  return Scope{name, location};
}

} // namespace Nyx::profiling
