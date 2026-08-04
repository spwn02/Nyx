#include "lyra.hpp"
#include <catch2/catch_test_macros.hpp>
#include <fmt/base.h>
#include <fmt/format.h>

import std;
import Nyx.Core;

using namespace Nyx;

template <typename E>
concept Enum = std::is_enum_v<E>;

template <Enum E>
constexpr auto enum_to_string(E value) -> String {
  template for (constexpr std::meta::info e : std::define_static_array(std::meta::enumerators_of(^^E))) {
    if (value == [:e:]) {
      return String(std::meta::identifier_of(e));
    }
  }
  return "<unnamed>";
}

enum Color : u8 { red, green, blue };

TEST_CASE("Enum to string") {
  REQUIRE(enum_to_string(Color::red) == "red");
  REQUIRE(enum_to_string(Color(42)) == "<unnamed>");
}

template <typename T>
concept Struct = std::is_class_v<T>;

template <typename T>
constexpr auto list_members(const T &val) -> String {
  constexpr auto ctx = std::meta::access_context::current();
  Vec<String> buf{};

  template for (constexpr auto mem :
      std::define_static_array(std::meta::nonstatic_data_members_of(^^T, ctx))) {
    buf.push_back(std::format("{}: {}", std::meta::identifier_of(mem), val.[:mem:]));
  }

  return buf | std::views::join_with(StringView{"; "}) | std::ranges::to<String>();
}

struct Vec2 {
  u32 x{}, y{};
};

TEST_CASE("List members") {
  REQUIRE(list_members<Vec2>({34, 35}) == "x: 34; y: 35");
}

namespace clap {

struct ShortArg {
  bool engaged = false;
  char value = 0;
  constexpr auto operator()(char chr) const -> ShortArg {
    return {.engaged = true, .value = chr};
  }
  void apply_annotation(lyra::opt &opt, const String &name) const {
    char first = engaged ? value : name[0];
    opt[String("-") + first];
  }
};

struct LongArg {
  static void apply_annotation(lyra::opt &opt, const String &name) {
    opt[String("--") + name];
  }
};

template <usize N>
struct HelpArg {
  Array<char, N> msg{};

  // String literal size deduction requires an array reference at this boundary.
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
  consteval HelpArg(const char (&str)[N]) {
    std::ranges::copy(str, msg.data());
  }
  void apply_annotation(lyra::opt &opt, const String & /*name*/) const {
    opt.help(msg.data());
  }
};

static consteval auto clap_annotations_of(std::meta::info info) {
  auto notes = std::meta::annotations_of(info);
  std::erase_if(notes, [](std::meta::info ann) constexpr noexcept -> bool {
    return std::meta::parent_of(std::meta::type_of(ann)) != ^^clap;
  });
  return notes;
}

template <typename Args, typename Parser>
void configure(Args *args, Parser *parser) {
  template for (constexpr auto mem : std::define_static_array(
                    std::meta::nonstatic_data_members_of(^^Args, std::meta::access_context::current()))) {
    String name{std::meta::identifier_of(mem)};
    lyra::opt opt_parser(args->[:mem:], name);
    template for (constexpr auto ann : std::define_static_array(clap_annotations_of(mem))) {
      using Ann = [:std::meta::type_of(ann):];
      std::meta::extract<Ann>(ann).apply_annotation(opt_parser, name);
    }
    parser->add_argument(opt_parser);
  }
}

static constexpr auto Short = ShortArg();
static constexpr auto Long = LongArg();
// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
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

TEST_CASE("Clap") {
  {
    Array<const char *, 5> argcv{"NyxEngine", "-n", "Spawn", "-c", "3"};

    auto args = clap::parse<Args>(argcv.size(), const_cast<char **>(argcv.data()));
    REQUIRE(args);
    // for (auto i = 0; i < args->count; ++i) {
    //   std::println("Hello, {}!", args->name);
    // }
  }
}
