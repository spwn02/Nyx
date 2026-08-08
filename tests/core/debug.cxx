import std;
import Nyx.Core;
import Nyx.Test;

using namespace Nyx;
using namespace Nyx::Test;

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)
namespace Tests::debug {

struct[[= Nyx::debug::derive]] Vec2 {
  f32 x{}, y{};
};

struct[[= Nyx::debug::derive]] A {
  f32 x{};
};

struct[[= Nyx::debug::derive]] Player {
  Vec2 pos{}, vel{};
  A a{};
  f32 accelaration{};
};

struct[[= Nyx::debug::derive]] Group {
  Player p1{}, p2{};
};

static constexpr f32 posX = 10;
static constexpr f32 posY = 20;
static constexpr f32 acceleration = 45;

[[ = test, = trace ]] auto debug() -> void {
  traceEvent("formatting Vec2 ...");

  Vec2 vec2{
      .x = posX,
      .y = posY,
  };
  check(std::format("{}", vec2) == "Vec2 { x: 10, y: 20 }"_exp);
  check(std::format("{:#}", vec2) == R"(Vec2 {
  x: 10,
  y: 20,
})"_exp);

  traceEvent("formatting Group ...");

  Group group{};
  group.p1.accelaration = acceleration;
  group.p2.pos = {.x = posX * 2, .y = posY * 2};
  check(std::format("{}", group) == "Group { p1: Player { pos: Vec2 { x: 0, y: 0 }, vel: Vec2 { x: "
                                    "0, y: 0 }, a: A { x: 0 }, accelaration: 45 }, p2: Player { "
                                    "pos: Vec2 { x: 20, y: 40 }, vel: Vec2 { x: 0, y: 0 }, a: A { "
                                    "x: 0 }, accelaration: 0 } }"_exp);
  check(std::format("{:#}", group) == R"(Group {
  p1: Player {
    pos: Vec2 {
      x: 0,
      y: 0,
    },
    vel: Vec2 {
      x: 0,
      y: 0,
    },
    a: A {
      x: 0,
    },
    accelaration: 45,
  },
  p2: Player {
    pos: Vec2 {
      x: 20,
      y: 40,
    },
    vel: Vec2 {
      x: 0,
      y: 0,
    },
    a: A {
      x: 0,
    },
    accelaration: 0,
  },
})"_exp);
}

struct[[= Nyx::debug::derive]] Vec3 {
  f32 x{}, y{}, z{};
  [[= Nyx::debug::hide]] f32 acc{};
};

[[= test]] auto structsWithHiddenMembers() -> void {
  check(std::format("{}", Vec3{}) == R"(Vec3 { x: 0, y: 0, z: 0 })"_exp);
}

struct[[= Nyx::debug::derive]] DataChunk {
  u32 fourcc{};
  u32 version{};
};

constexpr inline u32 signature = 0x56494430; // VID0

struct[[= Nyx::debug::derive]] VideoChunk : DataChunk {
  [[= Nyx::debug::rename<"signature">()]] u32 fourcc{signature};
  [[= Nyx::debug::hide]] u32 version{1};
};

[[= test]] auto structWithRenamedMembers() -> void {
  check(std::format("{}", VideoChunk{}) == "VideoChunk { signature: 1447642160 }"_exp);
}

struct[[= Nyx::debug::derive]] Empty {};

[[= test]] auto emptyStruct() -> void {
  check(std::format("{}", Empty{}) == "Empty"_exp);
}

struct[[= Nyx::debug::derive]] EmptyNested {
  u32 a{}, b{}, c{};
  Empty empty;
};

[[= test]] auto emptyNestedStruct() -> void {
  check(std::format("{}", EmptyNested{}) == "EmptyNested { a: 0, b: 0, c: 0, empty: Empty }"_exp);
}

struct[[= Nyx::debug::derive]] AllHidden {
  [[= Nyx::debug::hide]] u32 a{}, b{};
  [[= Nyx::debug::hide]] bool c{};
};

[[= test]] auto allHiddenStruct() -> void {
  /// Same as Empty
  check(std::format("{}", AllHidden{}) == "AllHidden"_exp);
}

enum class[[= Nyx::debug::derive]] Color : u8 {
  Red = 1,
  Green = 2,
  Blue = 3,
};

[[= test]] auto enumDisplay() -> void {
  check(std::format("{}", Color::Red) == "Red"_exp);
  check(std::format("{}", Color::Green) == "Green"_exp);
  check(std::format("{}", Color::Blue) == "Blue"_exp);
  check(std::format("{}", static_cast<Color>(69)) == "<unnamed>"_exp);
}

[[= test]] auto renamedEnum() -> void {
  enum class[[= Nyx::debug::derive]] Status : u8 {
    Failed[[= Nyx::debug::rename<"failed">()]] = 1,
    Skipped[[= Nyx::debug::rename<"skipped">()]],
    Success[[= Nyx::debug::rename<"success">()]],
  };

  check(std::format("{}", Status::Failed) == "failed"_exp);
  check(std::format("{}", Status::Skipped) == "skipped"_exp);
  check(std::format("{}", Status::Success) == "success"_exp);
}

[[= test]] auto hiddenEnum() -> void {
  enum class[[= Nyx::debug::derive]] HiddenEnum : u8 {
    Default = 1,
    Hidden[[= Nyx::debug::hide]],
    Work,
  };

  check(std::format("{}", HiddenEnum::Hidden) == "<unnamed>"_exp);
}

[[= test]] auto hiddenEnumWithDefaultEnumerator() -> void {
  enum class[[= Nyx::debug::derive]] HiddenEnum : u8 {
    Default[[ = Nyx::debug::prefer, = Nyx::debug::rename<"default">() ]] = 1,
    Hidden[[= Nyx::debug::hide]],
    Work,
  };

  check(std::format("{}", HiddenEnum::Hidden) == "default"_exp);
}

} // namespace Tests
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)

consteval {
  discover<^^Tests::debug>();
}
