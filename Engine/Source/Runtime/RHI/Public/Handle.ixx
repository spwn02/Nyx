export module Nyx.RHI:Handle;

import std;
import Miracle;

using namespace Miracle;

namespace Nyx::RHI {

export using HandleIndex = u32;
export using HandleGeneration = u32;

export template <class Tag>
concept HandleTag = std::is_empty_v<Tag> && std::is_trivially_default_constructible_v<Tag>;

namespace detail {
export template <HandleTag, class>
class ResourcePool;
} // namespace detail

export template <HandleTag Tag>
class Handle final {
public:
  constexpr Handle() noexcept = default;

  [[nodiscard]]
  constexpr auto valid() const noexcept -> bool {
    return index_ != invalidIndex_ and generation_ != 0;
  }

  [[nodiscard]]
  constexpr explicit operator bool() const noexcept {
    return valid();
  }

  [[nodiscard]]
  constexpr auto index() const noexcept -> HandleIndex {
    return index_;
  }

  [[nodiscard]]
  constexpr auto generation() const noexcept -> HandleGeneration {
    return generation_;
  }

  friend constexpr auto operator==(const Handle &, const Handle &) noexcept -> bool = default;
  friend constexpr auto operator<=>(const Handle &, const Handle &) noexcept
      -> std::strong_ordering = default;

  constexpr Handle(HandleIndex index, HandleGeneration generation) noexcept
      : index_(index)
      , generation_(generation) {
  }

private:
  template <HandleTag, class>
  friend class detail::ResourcePool;

  static constexpr HandleIndex invalidIndex_ = std::numeric_limits<HandleIndex>::max();

  HandleIndex index_{invalidIndex_};
  HandleGeneration generation_{};
};

} // namespace Nyx::RHI
