#pragma once

#include <cstdint>

namespace Nyx {

constexpr uint32_t FOURCC(const char a, const char b, const char c,
                          const char d) {
  return (uint32_t(uint8_t(a))) | (uint32_t(uint8_t(b)) << 8) |
         (uint32_t(uint8_t(c)) << 16) | (uint32_t(uint8_t(d)) << 24);
}

enum class NyxChunk : uint32_t {
  STRS = FOURCC('S', 'T', 'R', 'S'), // string table
  ENTS = FOURCC('E', 'N', 'T', 'S'), // entities
  TRNS = FOURCC('T', 'R', 'N', 'S'), // transforms
  MATL = FOURCC('M', 'A', 'T', 'L'), // material refs table
  MESH = FOURCC('M', 'E', 'S', 'H'), // mesh/submesh refs
  CAMR = FOURCC('C', 'A', 'M', 'R'), // cameras + active camera
  LITE = FOURCC('L', 'I', 'T', 'E'), // lights
  SKY = FOURCC('S', 'K', 'Y', ' '),  // sky/environment settings
  CATS = FOURCC('C', 'A', 'T', 'S'), // editor category tree
  TOC = FOURCC('T', 'O', 'C', ' '),  // chunk directory footer
};

struct NyxTocEntry final {
  uint32_t fourcc = 0;
  uint32_t version = 1;
  uint64_t offset = 0; // absolute file offset to chunk header
  uint64_t size = 0;   // payload size, excluding 16-byte header
};

constexpr uint64_t NYXSCENE_MAGIC = 0x004E595853434E31ull;       // "\0NYXSCN1"
constexpr uint32_t NYXSCENE_VERSION = 0x00010001u;               // 1.1
constexpr uint64_t NYX_TOC_FOOTER_MAGIC = 0x4F46434F5458594Eull; // "NYXTOCFO"

} // namespace Nyx
