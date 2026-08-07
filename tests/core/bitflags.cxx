import std;
import Nyx.Core;
import Nyx.Test;

using namespace Nyx::Test;

enum class[[= Nyx::bitflags]] BufferUsage : Nyx::u8 {
  None = 0,
  Vertex = 1 << 0,
  Index = 1 << 1,
  Uniform = 1 << 6,
  Storage = 1 << 3,
};

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)
namespace Tests::bitflags {

[[= test]] auto bitflags() -> void {
  BufferUsage flags = BufferUsage::Vertex | BufferUsage::Index;
  require(Nyx::bits(flags) == 0b11);
  flags |= BufferUsage::Storage;
  require(Nyx::bits(flags) == 0b1011);
  require(Nyx::has(flags, BufferUsage::Vertex));
}

[[= test]] auto bitflagsDisplay() -> void {
  BufferUsage flags = BufferUsage::Vertex | BufferUsage::Storage;
  require(std::format("{}", flags) == "9");
  require(std::format("{:b}", flags) == "00001001");
  require(std::format("{:x}", flags) == "9");
}

[[= test]] auto bitflagsDisplayWithPrefix() -> void {
  BufferUsage flags = BufferUsage::Vertex | BufferUsage::Storage;
  require(std::format("{:p}", flags) == "9");
  require(std::format("{:bp}", flags) == "0b00001001");
  require(std::format("{:xp}", flags) == "0x9");
}

[[= test]] auto bitflagsAll() -> void {
  require(Nyx::bits(Nyx::all<BufferUsage>()) == 0b1001011);
}

} // namespace Tests
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)

consteval {
  discover<^^Tests::bitflags>();
}
