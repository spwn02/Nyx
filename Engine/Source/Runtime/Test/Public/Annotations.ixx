export module Nyx.Test:Annotations;

import std;
import Nyx.Core;

export namespace Nyx::diagnostic {

// NOLINTBEGIN(readability-identifier-naming, bugprone-reserved-identifier)
template <meta::StaticString Name>
struct Message final {
  [[nodiscard]] static constexpr auto apply() -> StringView {
    return Name.apply();
  }
};

template <meta::StaticString Name>
consteval auto message() -> Message<Name> {
  return Message<Name>{};
}

template <class>
inline constexpr bool is_message_v{};

template <meta::StaticString Name>
inline constexpr bool is_message_v<Message<Name>>{true};

template <std::meta::info Info>
consteval auto annotationMessage() -> StringView {
  template for (constexpr std::meta::info annotation : meta::annotations<Info>) {
    using Annotation = meta::TypeObject<annotation>;

    if constexpr (diagnostic::is_message_v<Annotation>) {
      return std::meta::extract<Annotation>(annotation).apply();
    }
  }

  return meta::identifier<Info>;
}

template <meta::StaticString Name>
struct Prefix final {
  [[nodiscard]] static constexpr auto apply() -> StringView {
    return Name.apply();
  }
};

template <meta::StaticString Name>
consteval auto prefix() -> Prefix<Name> {
  return Prefix<Name>{};
}

template <class>
inline constexpr bool is_prefix_v{};

template <meta::StaticString Name>
inline constexpr bool is_prefix_v<Prefix<Name>>{true};

template <std::meta::info Item>
consteval auto annotationPrefix() -> StringView {
  template for (constexpr std::meta::info annotation : meta::annotations<Item>) {
    using Annotation = meta::TypeObject<annotation>;

    if constexpr (diagnostic::is_prefix_v<Annotation>) {
      return std::meta::extract<Annotation>(annotation).apply();
    }
  }

  return "NYX";
}

} // namespace Nyx::diagnostic

export namespace Nyx::Test {

struct Test final {};

inline constexpr Test test{};

struct Fixture final {};

inline constexpr Fixture fixture{};

struct Once final {};

inline constexpr Once once{};

struct ContextParameter final {};

inline constexpr ContextParameter context{};

struct FromCase final {};

inline constexpr FromCase fromCase{};

struct Trace final {};

inline constexpr Trace trace{};

struct IncludeDotFiles final {};

inline constexpr IncludeDotFiles includeDotFiles{};

template <meta::StaticString Name>
struct ArgumentName final {
  [[nodiscard]] static constexpr auto apply() -> StringView {
    return Name.apply();
  }
};

template <class... Items>
struct AnnotationItems;

template <>
struct AnnotationItems<> final {};

template <class First, class... Rest>
struct AnnotationItems<First, Rest...> final {
  First first_;
  AnnotationItems<Rest...> rest_;

  constexpr explicit AnnotationItems(First first, Rest... rest)
      : first_(std::move(first))
      , rest_(std::move(rest)...) {
  }
};

template <usize Index, class First, class... Rest>
[[nodiscard]] constexpr auto annotationItem(const AnnotationItems<First, Rest...> &items) -> const auto & {
  if constexpr (Index == 0)
    return items.first_;
  else
    return annotationItem<Index - 1>(items.rest_);
}

template <class Function, class... Items, usize... Indices>
constexpr auto applyAnnotationItems(const AnnotationItems<Items...> &items,
    Function &&function,
    std::index_sequence<Indices...> /*unused*/) -> decltype(auto) {
  return std::invoke(std::forward<Function>(function), annotationItem<Indices>(items)...);
}

template <ArgumentName Name, class... Properties>
class Argument final {
public:
  constexpr explicit Argument(Properties... properties)
      : properties_(std::move(properties)...) {
  }

  [[nodiscard]] static constexpr auto name() -> StringView {
    return Name.apply();
  }

  template <usize Index>
  [[nodiscard]] constexpr auto property() const -> const auto & {
    return annotationItem<Index>(properties_);
  }

  template <class Function>
  constexpr auto apply(Function &&function) const -> decltype(auto) {
    return applyAnnotationItems(
        properties_, std::forward<Function>(function), std::index_sequence_for<Properties...>{});
  }

  AnnotationItems<Properties...> properties_;
};

template <class>
inline constexpr bool is_argument_v{};

template <ArgumentName Name, class... Properties>
inline constexpr bool is_argument_v<Argument<Name, Properties...>>{true};

template <ArgumentName Name>
class ArgumentBuilder final {
public:
  template <class... Properties>
  consteval auto operator()(Properties... properties) const -> Argument<Name, std::decay_t<Properties>...> {
    return Argument<Name, std::decay_t<Properties>...>{std::move(properties)...};
  }
};

template <meta::StaticString Name>
inline constexpr ArgumentBuilder<ArgumentName<Name>{}> arg{};

template <usize N>
consteval auto valueItem(const char (&item)[N]) -> meta::StaticString<N> { // NOLINT
  return meta::StaticString<N>{item};
}

template <class Item>
consteval auto valueItem(Item &&item) -> std::decay_t<Item> {
  return std::forward<Item>(item);
}

template <class... Items>
struct Values final {
  static constexpr usize size_{sizeof...(Items)};

  constexpr explicit Values(Items... items)
      : items_(std::move(items)...) {
  }

  template <class Function>
  constexpr auto apply(Function &&function) const -> decltype(auto) {
    return applyAnnotationItems(
        items_, std::forward<Function>(function), std::index_sequence_for<Items...>{});
  }

  AnnotationItems<Items...> items_;
};

template <class... Items>
consteval auto values(Items &&...items) -> Values<decltype(valueItem(std::forward<Items>(items)))...> {
  return Values<decltype(valueItem(std::forward<Items>(items)))...>{valueItem(std::forward<Items>(items))...};
}

// NOLINTBEGIN(readability-identifier-naming)
template <class>
inline constexpr bool is_values_v{};

template <class... Items>
inline constexpr bool is_values_v<Values<Items...>>{true};
// NOLINTEND(readability-identifier-naming)

template <meta::StaticString Name>
struct Files final {
  [[nodiscard]] static constexpr auto apply() -> StringView {
    return Name.apply();
  }
};

template <meta::StaticString Name>
consteval auto files() -> Files<Name> {
  return Files<Name>{};
}

template <class>
inline constexpr bool is_files_v{};

template <meta::StaticString Name>
inline constexpr bool is_files_v<ArgumentName<Name>>{true};

template <meta::StaticString Name>
struct Exclude final {
  [[nodiscard]] static constexpr auto apply() -> StringView {
    return Name.apply();
  }
};

template <meta::StaticString Name>
consteval auto exclude() -> Exclude<Name> {
  return Exclude<Name>{};
}

template <class>
inline constexpr bool is_exclude_v{};

template <meta::StaticString Name>
inline constexpr bool is_exclude_v<Exclude<Name>>{true};

template <meta::StaticString Name>
struct ShouldPanic final {
  [[nodiscard]] static constexpr auto apply() -> StringView {
    return Name.apply();
  }
};

template <meta::StaticString Name>
consteval auto shouldPanic() -> ShouldPanic<Name> {
  return ShouldPanic<Name>{};
}

consteval auto shouldPanic() -> ShouldPanic<""> {
  return ShouldPanic<"">{};
}

template <class>
inline constexpr bool is_should_panic_v{};

template <meta::StaticString Name>
inline constexpr bool is_should_panic_v<ShouldPanic<Name>>{true};

struct Timeout final {
  // std::chrono::duration is not a structural type, so store the raw count.
  long long nanoseconds{};
};

template <class Rep, class Period>
consteval auto timeout(std::chrono::duration<Rep, Period> duration) -> Timeout {
  return Timeout{
      .nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count(),
  };
}

template <meta::StaticString Name>
struct Description final {
  [[nodiscard]] static constexpr auto apply() -> StringView {
    return Name.apply();
  }
};

template <meta::StaticString Name>
consteval auto description() -> Description<Name> {
  return Description<Name>{};
}

template <class>
inline constexpr bool is_description_v{};

template <meta::StaticString Name>
inline constexpr bool is_description_v<Description<Name>>{true};

template <usize Count, class Function>
constexpr auto withIndices(Function &&function) -> decltype(auto) {
  return [&]<usize... indices>(std::index_sequence<indices...>) -> decltype(auto) {
    return std::forward<Function>(function)(std::integral_constant<usize, indices>{}...);
  }(std::make_index_sequence<Count>{});
}

template <class... Values>
struct Case final {
  struct Storage;

  consteval {
    std::meta::define_aggregate(^^Storage,
        {
            std::meta::data_member_spec(^^Values)...});
  }

  static constexpr auto members_ = meta::nsMembers<^^Storage, meta::AccessContext::unchecked()>;

  Storage storage_;

  constexpr explicit Case(Values... values)
      : storage_{std::move(values)...} {
  }

  template <class Function>
  constexpr auto apply(Function &&function) const -> decltype(auto) {
    return withIndices<sizeof...(Values)>([this, &function](auto... indices) -> decltype(auto) {
      return std::invoke(std::forward<Function>(function), storage_.[:members_[indices]:]...);
    });
  }

  [[nodiscard]]
  auto describe() const -> String {
    String result{};
    bool first{true};

    apply([&]<class... Types>(const Types &...values) -> void {
      ((result.append(first ? "" : ", "), result.append(std::format("{}", values)), first = false), ...);
    });
    return result;
  }
};

template <class... Values>
Case(Values...) -> Case<Values...>;

} // namespace Nyx::Test
// NOLINTEND(readability-identifier-naming, bugprone-reserved-identifier)
