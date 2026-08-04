#include <catch2/catch_test_macros.hpp>

import std;
import Nyx.Core;

using namespace Nyx;

struct[[= debug::derive]] Vec2 {
  f32 x{}, y{};
};

struct[[= debug::derive]] A {
  f32 x{};
};

struct[[= debug::derive]] Player {
  Vec2 pos{}, vel{};
  A a{};
  f32 accelaration{};
};

struct[[= debug::derive]] Group {
  Player p1{}, p2{};
};

static constexpr f32 posX = 10;
static constexpr f32 posY = 20;
static constexpr f32 acceleration = 45;

TEST_CASE("Debug") {
  Vec2 vec2{
      .x = posX,
      .y = posY,
  };
  REQUIRE(std::format("{}", vec2) == "Vec2 { x: 10, y: 20 }");
  REQUIRE(std::format("{:#}", vec2) == R"(Vec2 {
  x: 10,
  y: 20,
})");

  Group group{};
  group.p1.accelaration = acceleration;
  group.p2.pos = {.x = posX * 2, .y = posY * 2};
  REQUIRE(std::format("{}", group) == "Group { p1: Player { pos: Vec2 { x: 0, y: 0 }, vel: Vec2 { x: "
                                      "0, y: 0 }, a: A { x: 0 }, accelaration: 45 }, p2: Player { "
                                      "pos: Vec2 { x: 20, y: 40 }, vel: Vec2 { x: 0, y: 0 }, a: A { "
                                      "x: 0 }, accelaration: 0 } }");
  REQUIRE(std::format("{:#}", group) == R"(Group {
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
})");
}

struct[[= debug::derive]] Vec3 {
  f32 x{}, y{}, z{};
  [[= debug::hide]] f32 acc{};
};

TEST_CASE("Structs with hidden members") {
  REQUIRE(std::format("{}", Vec3{}) == R"(Vec3 { x: 0, y: 0, z: 0 })");
}

struct[[= debug::derive]] DataChunk {
  u32 fourcc{};
  u32 version{};
};

constexpr inline u32 signature = 0x56494430; // VID0

struct[[= debug::derive]] VideoChunk : DataChunk {
  [[= debug::rename("signature")]] u32 fourcc{signature};
  [[= debug::hide]] u32 version{1};
};

TEST_CASE("Structs with renamed members") {
  REQUIRE(std::format("{}", VideoChunk{}) == "VideoChunk { signature: 1447642160 }");
}

struct[[= debug::derive]] Empty {};

TEST_CASE("Empty struct") {
  REQUIRE(std::format("{}", Empty{}) == "Empty");
}

struct[[= debug::derive]] EmptyNested {
  u32 a{}, b{}, c{};
  Empty empty;
};

TEST_CASE("Empty nested struct") {
  REQUIRE(std::format("{}", EmptyNested{}) == "EmptyNested { a: 0, b: 0, c: 0, empty: Empty }");
}

struct[[= debug::derive]] AllHidden {
  [[= debug::hide]] u32 a{}, b{};
  [[= debug::hide]] bool c{};
};

TEST_CASE("All hidden struct") {
  /// Same as Empty
  REQUIRE(std::format("{}", AllHidden{}) == "AllHidden");
}

enum class[[= debug::derive]] Color : u8 {
  Red = 1,
  Green = 2,
  Blue = 3,
};

TEST_CASE("Enum") {
  REQUIRE(std::format("{}", Color::Red) == "Red");
  REQUIRE(std::format("{}", Color::Green) == "Green");
  REQUIRE(std::format("{}", Color::Blue) == "Blue");
  REQUIRE(std::format("{}", static_cast<Color>(69)) == "<unnamed>");
}

TEST_CASE("Renamed enum") {
  enum class[[= debug::derive]] Status : u8 {
    Failed[[= debug::rename("failed")]] = 1,
    Skipped[[= debug::rename("skipped")]],
    Success[[= debug::rename("success")]],
  };

  REQUIRE(std::format("{}", Status::Failed) == "failed");
  REQUIRE(std::format("{}", Status::Skipped) == "skipped");
  REQUIRE(std::format("{}", Status::Success) == "success");
}

TEST_CASE("Hidden enum") {
  enum class[[= debug::derive]] HiddenEnum : u8 {
    Default = 1,
    Hidden[[= debug::hide]],
    Work,
  };

  REQUIRE(std::format("{}", HiddenEnum::Hidden) == "<unnamed>");
}

TEST_CASE("Hidden enum with default enumerator") {
  enum class[[= debug::derive]] HiddenEnum : u8 {
    Default[[ = debug::prefer, = debug::rename("default") ]] = 1,
    Hidden[[= debug::hide]],
    Work,
  };

  REQUIRE(std::format("{}", HiddenEnum::Hidden) == "default");
}
