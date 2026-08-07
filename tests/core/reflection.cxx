#include <lyra/lyra.hpp>

import std;
import Nyx.Core;
import Nyx.Test;

using namespace Nyx;
using namespace Nyx::Test;

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)
namespace Tests::reflection {

template <typename E>
concept Enum = std::is_enum_v<E>;

template <Enum E>
constexpr auto enum_to_string(E value) -> String { // NOLINT
  // NOLINTNEXTLINE(bugprone-reserved-identifier)
  template for (constexpr std::meta::info enumerator : meta::enumerators<^^E>) {
    if (value == [:enumerator:]) {
      return String(meta::identifier<enumerator>);
    }
  }
  return "<unnamed>";
}

enum Color : u8 { red, green, blue };

[[
  = test,
  = Case{Color::red, "red"},
  = Case{Color::green, "green"},
  = Case{Color::blue, "blue"},
  = Case{Color{69}, "<unnamed>"}
]] auto enumToString(Color value, StringView expected) -> Expression {
  return eq(enum_to_string(value), expected);
}

template <typename T>
concept Struct = std::is_class_v<T>;

template <Struct T>
constexpr auto list_members(const T &val) -> String { // NOLINT
  Vec<String> buf{};

  // NOLINTNEXTLINE(bugprone-reserved-identifier)
  template for (constexpr auto mem : meta::nsMembers<^^T, meta::AccessContext::current()>) {
    buf.push_back(std::format("{}: {}", meta::identifier<mem>, val.[:mem:]));
  }

  return buf | std::views::join_with(StringView{"; "}) | std::ranges::to<String>();
}

struct[[= debug::derive]] Vec2 {
  u32 x{}, y{};
};

[[
  = test,
  = Case{Vec2{}, "x: 0; y: 0"},
  = Case{Vec2{.x = 69}, "x: 69; y: 0"},
  = Case{Vec2{.x = 34, .y = 35}, "x: 34; y: 35"}
]] auto listMembers(Vec2 vec2, StringView expected) -> Expression {
  return eq(list_members(vec2), expected);
}

namespace clap {

struct ShortArg {
  bool engaged = false;
  char value = 0;
  constexpr auto operator()(char chr) const -> ShortArg {
    return {.engaged = true, .value = chr};
  }
  void apply_annotation(lyra::opt &opt, const String &name) const { // NOLINT
    char first = engaged ? value : name[0];
    opt[String("-") + first];
  }
};

struct LongArg {
  static void apply_annotation(lyra::opt &opt, const String &name) { // NOLINT
    opt[String("--") + name];
  }
};

template <usize N>
struct HelpArg {
  Array<char, N> msg{};

  // String literal size deduction requires an array reference at this boundary.
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
  consteval HelpArg(const char (&str)[N])
      : msg(std::to_array(str)) {
  }
  void apply_annotation(lyra::opt &opt, const String & /*name*/) const {
    opt.help(msg.data());
  }
};

static consteval auto clap_annotations_of(std::meta::info info) -> Vec<std::meta::info> {
  Vec<std::meta::info> notes = std::meta::annotations_of(info);
  std::erase_if(notes, [](std::meta::info ann) constexpr noexcept -> bool {
    return std::meta::parent_of(std::meta::type_of(ann)) != ^^clap;
  });
  return notes;
}

template <typename Args, typename Parser>
void configure(Args *args, Parser *parser) {
  template for (constexpr auto mem : meta::nsMembers<^^Args, meta::AccessContext::current()>) {
    String name{meta::identifier<mem>};
    lyra::opt opt_parser(args->[:mem:], name);
    template for (constexpr auto ann : std::define_static_array(clap_annotations_of(mem))) {
      using Ann = meta::TypeObject<ann>;
      std::meta::extract<Ann>(ann).apply_annotation(opt_parser, name);
    }
    parser->add_argument(opt_parser);
  }
}

inline constexpr ShortArg Short{};
inline constexpr LongArg Long{};

template <usize N>
consteval auto Help(const char (&str)[N]) -> HelpArg<N> {
  return HelpArg<N>{str};
}

template <typename Args>
auto parse(int argc, char **argv) -> Result<Args> {
  Args args;
  bool show_help = false;
  auto cli_parser = lyra::cli() | lyra::help(show_help);
  configure(&args, &cli_parser);
  auto result = cli_parser.parse({argc, argv});
  if (not result)
    return bail({result.message()});
  if (show_help || not result) {
    std::stringstream stream;
    stream << cli_parser;
    return bail({"Failed to parse: {}", stream.str()});
  }
  return args;
}

} // namespace clap

struct Args {
  [[= clap::Short]] String name;
  [[= clap::Help("Number of times to greet")]][[ = clap::Short, = clap::Long ]] int count{};
};

using Argv = Vec<const char *>;

[[
  = test,
  = Case{container<Argv>("NyxEngine")},
  = Case{container<Argv>("NyxEngine", "-n", "Spawn")},
  = Case{container<Argv>("NyxEngine", "-n", "Spawn", "-c", "3")}
]] auto cli(Argv args) -> bool {
  return clap::parse<Args>(static_cast<int>(args.size()), const_cast<char **>(args.data())).has_value();
}

auto chopArgument(Error &&error) -> Error {
  String &message = error.messages.begin()->message;
  message.erase(message.end() - 3, message.end());
  return error;
}

[[
  = test,
  = Case{container<Argv>("NyxEngine", "-n")},
  = Case{container<Argv>("NyxEngine", "-c")},
  = shouldPanic<"Expected argument following">()
]] auto cliFail(Argv args) -> Result<void> {
  return clap::parse<Args>(static_cast<int>(args.size()), const_cast<char **>(args.data()))
      .transform([](Args &&args) -> void { static_cast<void>(args); })
      .transform_error(chopArgument);
}

} // namespace Tests::reflection
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)

consteval {
  discover<^^Tests::reflection>(recursive);
}
