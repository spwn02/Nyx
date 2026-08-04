export module Nyx.Core:Debug;

import std;
import :Types;
import :Meta;
import :Concepts;

// NOLINTBEGIN(readability-identifier-naming, bugprone-reserved-identifier)
namespace Nyx::debug {

struct DebugDerive final {};
export inline constexpr DebugDerive derive{};

struct DebugHide final {};
export inline constexpr DebugHide hide{};

struct DebugPrefer final {};
export inline constexpr DebugPrefer prefer{};

// template <usize N>
// struct DebugRename {
//   Array<char, N> name{};
//
//   /// String literal size deduction requires an array reference at this boundary.
//   // NOLINTBEGIN(cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
//   consteval DebugRename(const char (&str)[N])
//       : name(std::to_array(str)) {
//   }
//   // NOLINTEND(cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
//
//   [[nodiscard]]
//   constexpr auto apply() const -> StringView {
//     return StringView{std::define_static_string(StringView{name.data(), N - 1}), N - 1};
//   }
// };

template <meta::StaticString Name>
struct DebugRename final {
  [[nodiscard]] static constexpr auto apply() -> StringView {
    return Name.apply();
  }
};

export template <meta::StaticString Name>
consteval auto rename() -> DebugRename<Name> {
  return DebugRename<Name>{};
}

template <class>
inline constexpr bool is_debug_rename_v{};
template <meta::StaticString Name>
inline constexpr bool is_debug_rename_v<DebugRename<Name>>{true};

template <class T>
concept DebuggableAggr = Nyx::meta::has_annotation<DebugDerive, ^^T>() and not Enum<T>;

template <class T>
concept DebuggableEnum = Nyx::meta::has_annotation<DebugDerive, ^^T>() and Enum<T>;

export template <class T>
concept Debuggable = DebuggableAggr<T> or DebuggableEnum<T>;

consteval auto debug_annotations(std::meta::info info) -> Vec<std::meta::info> {
  Vec<std::meta::info> notes = std::meta::annotations_of(info);
  std::erase_if(notes, [](std::meta::info ann) -> bool {
    return std::meta::parent_of(std::meta::type_of(ann)) != ^^Nyx::debug;
  });
  return notes;
}

template <std::meta::info Info>
consteval auto debug_hidden() -> bool {
  return Nyx::meta::has_annotation<DebugHide, Info>();
}

template <std::meta::info Info>
consteval auto debug_name() -> StringView {
  template for (constexpr std::meta::info ann : meta::annotations<Info>) {
    using Ann = meta::TypeObject<ann>;

    if constexpr (is_debug_rename_v<Ann>)
      return std::meta::extract<Ann>(ann).apply();
  }

  return meta::identifier<Info>;
}

struct DebugMetadata {
  bool skipped{};
  StringView name;
};

template <std::meta::info info>
constexpr auto debug_metadata() -> DebugMetadata {
  DebugMetadata metadata{};

  if constexpr (debug_hidden<info>()) {
    metadata.skipped = true;
  }

  metadata.name = debug_name<info>();

  return metadata;
}

template <class Obj>
constexpr auto map_fields(const Obj &obj, bool pretty = false, usize level = 0) -> String {
  Vec<String> buf{};
  const String indent_outer(pretty ? (level) * 2 : 0, ' ');
  const String indent_inner(pretty ? (level + 1) * 2 : 0, ' ');

  template for (constexpr std::meta::info mem : meta::nsMembers<^^Obj, meta::AccessContext::unchecked()>) {
    constexpr DebugMetadata metadata = debug_metadata<mem>();
    if constexpr (metadata.skipped) {
      continue;
    }

    using Type = meta::TypeObject<mem>;
    const meta::Type<mem> &value = obj.[:mem:];

    String result{};
    if constexpr (DebuggableAggr<Type>) {
      result = std::format("{}: {}", metadata.name, map_fields(value, pretty, level + 1));
    } else if constexpr (DebuggableEnum<Type>) {
      result = std::format("{}: {}", metadata.name, map_enum(value));
    } else {
      static_assert(std::formattable<Type, char>,
          "Debug formatter error: reflected member is not std::formattable. Provide std::formatter for the "
          "member type or annotate that type with [[=Debug]].");

      if constexpr (StringLike<Type>) {
        result = std::format("{}: \"{}\"", metadata.name, value);
      } else {
        result = std::format("{}: {}", metadata.name, value);
      }
    }
    buf.push_back(std::move(result.insert(0, indent_inner)));
  }

  constexpr StringView name = meta::identifier<^^Obj>;
  if (buf.empty())
    return String{name};

  String fields = buf | std::views::join_with(StringView{pretty ? ",\n" : ", "}) | std::ranges::to<String>();
  String res{};

  if (pretty)
    res = std::format("{} {{\n{},\n{}}}", name, fields, indent_outer);
  else
    res = std::format("{} {{ {} }}", name, fields);

  return res;
}

template <class T>
constexpr auto map_enum(const T &value) -> String {
  Option<StringView> preferred{};

  template for (constexpr std::meta::info enumerator : meta::enumerators<^^T>) {
    constexpr T item = [:enumerator:];
    constexpr DebugMetadata metadata = debug_metadata<enumerator>();

    if constexpr (metadata.skipped)
      continue;

    if (item == value) {
      return String{metadata.name};
    }

    if constexpr (Nyx::meta::has_annotation<DebugPrefer, enumerator>())
      preferred = metadata.name;
  }

  if (preferred.has_value())
    return String{*preferred};

  return "<unnamed>";
}

} // namespace Nyx::debug

export template <Nyx::debug::DebuggableAggr T>
struct std::formatter<T> : std::formatter<Nyx::String> {
  bool pretty{};

  template <typename ParseContext>
  constexpr auto parse(ParseContext &ctx) -> typename ParseContext::iterator {
    auto iter = ctx.begin();

    if (iter == ctx.end())
      return iter;

    if (*iter == '#') {
      pretty = true;
      ++iter;
    }

    if (iter != ctx.end() && *iter != '}')
      throw std::format_error("Invalid format args for Debug.");

    return iter;
  }

  constexpr auto format(const T &obj, format_context &ctx) const -> std::format_context::iterator {
    return std::formatter<Nyx::String>::format(Nyx::debug::map_fields(obj, pretty), ctx);
  }
};

export template <Nyx::debug::DebuggableEnum T>
struct std::formatter<T> : std::formatter<Nyx::String> {
  constexpr auto format(const T &obj, format_context &ctx) const -> std::format_context::iterator {
    return std::formatter<Nyx::String>::format(Nyx::debug::map_enum(obj), ctx);
  }
};
// NOLINTEND(readability-identifier-naming, bugprone-reserved-identifier)
