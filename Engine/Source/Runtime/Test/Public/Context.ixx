export module Nyx.Test:Context;

import std;
import Nyx.Core;

export namespace Nyx::Test {

struct Context final {
  // NOLINTBEGIN(cppcoreguidelines-avoid-const-or-ref-data-members)
  const StringView name;
  const StringView description;
  const usize testCase;
  const std::source_location location;
  // NOLINTEND(cppcoreguidelines-avoid-const-or-ref-data-members)
};

/// Binds the immutable public execution context to the current thread.
///
/// Coroutine scheduling will establish this binding around every resume, alongside EnvironmentBinding.
class ContextBinding final {
public:
  explicit ContextBinding(const Context &context) noexcept;
  ~ContextBinding() noexcept;

  ContextBinding(const ContextBinding &) = delete (
      "ContextBinding owns a pointer to a previous thread-local context");
  auto operator=(const ContextBinding &)
      -> ContextBinding & = delete ("ContextBinding owns a pointer to a previous thread-local context");
  ContextBinding(ContextBinding &&) = delete (
      "ContextBinding owns a pointer to a previous thread-local context");
  auto operator=(ContextBinding &&)
      -> ContextBinding & = delete ("ContextBinding owns a pointer to a previous thread-local context");

private:
  const Context *previous_{};
};

[[nodiscard]] auto currentContext() noexcept -> Option<Ref<const Context>>;

} // namespace Nyx::Test
