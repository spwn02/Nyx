export module Nyx.Core:Error;

import std;
import :Types;

export namespace Nyx {

struct ErrorDisplayOptions {
  bool colours{};
  bool locations{};
};

struct Error {
  struct Message {
    String message;
    std::source_location location;

    Message(std::source_location loc = std::source_location::current())
        : location(loc) {
    }

    Message(const char *message, std::source_location loc = std::source_location::current())
        : message(message)
        , location(loc) {
    }
    Message(String message, std::source_location loc = std::source_location::current())
        : message(std::move(message))
        , location(loc) {
    }
  };

  Error();
  Error(Message message);
  template <typename... Args>
  Error(Message message, const Args &...args) {
    message.message = std::vformat(message.message, std::make_format_args(args...));

    messages.insert({message});
  }
  ~Error() noexcept = default;

  constexpr Error(const Error &other) = delete (
      "Error class contains a vector of strings, which is expensive to "
      "copy; use move instead");
  constexpr auto operator=(const Error &other)
      -> Error & = delete ("Error class contains a vector of strings, which is expensive to "
                           "copy; use move instead");

  Error(Error &&other) noexcept;
  auto operator=(Error &&other) noexcept -> Error &;

  [[nodiscard]]
  /// Renders Error into cout.
  auto display(ErrorDisplayOptions options = {}) const -> String;
  /// Renders Error into a String, that can later be displayed into cout.
  auto display(std::ostream &output, ErrorDisplayOptions options = {}) const -> void;

  /// Appends Error with another message (identical to `<<` operator).
  auto with(Message other) -> Error &;

  /// Transfers the ownership of the Error, nullifying previous instance.
  /// Similar to `std::move()`, uses `std::exchange` under the hood.
  auto release() noexcept -> Error;

  operator String() const;
  explicit operator bool() const;

  auto operator==(const Error &other) const -> bool;
  auto operator==(StringView other) const -> bool;
  auto operator<<(Message other) -> void;

  Hive<Message> messages;
};

template <typename T>
using Result = std::expected<T, Error>;

using bail = std::unexpected<Error>;
auto todo(std::source_location loc = std::source_location::current()) -> bail;

} // namespace Nyx

template <>
struct std::formatter<Nyx::Error> : std::formatter<Nyx::String> {
  constexpr auto format(Nyx::Error err, format_context &ctx) const -> std::format_context::iterator {
    return std::formatter<Nyx::String>::format(err.display(), ctx);
  }
};
