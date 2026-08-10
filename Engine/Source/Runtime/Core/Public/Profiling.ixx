export module Nyx.Core:Profiling;

import std;
import :Build;
import :Debug;
import :Types;

export namespace Nyx::profiling {

/// Identifies the kind of event collected by a profile sink.
enum class[[= debug::derive]] EventKind : u8 {
  Scope,
};

/// Records one completed profile scope in source order.
struct ProfileEvent final {
  String name;
  std::chrono::steady_clock::duration duration{};
  std::source_location location;
  usize depth{};
  EventKind kind{EventKind::Scope};
};

/// Aggregates repeated scopes with the same name.
struct ProfileAggregate final {
  usize count{};
  std::chrono::steady_clock::duration total{};
  std::chrono::steady_clock::duration minimum{};
  std::chrono::steady_clock::duration maximum{};
};

/// Immutable copy of a sink's ordered events and deterministic named aggregates.
struct ProfileSnapshot final {
  std::chrono::steady_clock::duration duration{};
  Vec<ProfileEvent> events;
  FlatMap<String, ProfileAggregate> aggregates;
};

/// Collects profile scopes for one owner, such as a test environment or an engine frame.
class ProfileSink final {
public:
  ProfileSink() = default;
  ~ProfileSink() noexcept = default;

  ProfileSink(const ProfileSink &) = delete ("ProfileSink owns ordered profiling storage.");
  auto operator=(const ProfileSink &)
      -> ProfileSink & = delete ("ProfileSink owns ordered profiling storage.");
  ProfileSink(ProfileSink &&) noexcept = delete ("ProfileSink owns ordered profiling storage.");
  auto operator=(ProfileSink &&) noexcept
      -> ProfileSink & = delete ("ProfileSink owns ordered profiling storage.");

  /// Begins a nested scope and returns its deterministic nesting depth.
  [[nodiscard]] auto begin() noexcept -> usize;

  /// Records a completed scope and closes one nesting level.
  auto record(String name,
      std::chrono::steady_clock::duration duration,
      std::source_location location,
      usize depth) -> void;

  /// Returns a stable copy suitable for a TestExecution or a machine report.
  [[nodiscard]] auto snapshot() const -> ProfileSnapshot;

private:
  usize activeScopes_{};
  std::chrono::steady_clock::duration duration_{};
  Vec<ProfileEvent> events_;
  FlatMap<String, ProfileAggregate> aggregates_;
};

/// Binds a sink to the current execution thread for generic engine code.
class SinkBinding final {
public:
  explicit SinkBinding(ProfileSink &sink) noexcept;
  ~SinkBinding() noexcept;

  SinkBinding(const SinkBinding &) = delete ("SinkBinding owns a previous thread-local sink pointer.");
  auto operator=(const SinkBinding &)
      -> SinkBinding & = delete ("SinkBinding owns a previous thread-local sink pointer.");
  SinkBinding(SinkBinding &&) noexcept = delete ("SinkBinding owns a previous thread-local sink pointer.");
  auto operator=(SinkBinding &&) noexcept
      -> SinkBinding & = delete ("SinkBinding owns a previous thread-local sink pointer.");

private:
  ProfileSink *previous_{};
};

/// Returns the sink bound to the current thread, or nullptr outside a profiled scope.
[[nodiscard]] auto currentSink() noexcept -> ProfileSink *;

/// Owns one named profile interval and records it when leaving scope, including early exists.
class Scope final {
public:
  explicit Scope(StringView name, std::source_location location = std::source_location::current());
  ~Scope() noexcept;

  Scope(const Scope &) = delete ("Scope owns one profile interval.");
  auto operator=(const Scope &) -> Scope & = delete ("Scope owns one profile interval.");
  Scope(Scope &&) noexcept = delete ("Scope owns one profile interval.");
  auto operator=(Scope &&) noexcept -> Scope & = delete ("Scope owns one profile interval.");

private:
  ProfileSink *sink_{};
  String name_;
  std::source_location location_;
  std::chrono::steady_clock::time_point started_;
  usize depth_{};
};

/// Creates a generic named profile scope for Nyx.Core and Nyx.Test clients.
[[nodiscard]] auto profileScope(StringView name,
    std::source_location location = std::source_location::current()) -> Scope;

} // namespace Nyx::profiling
