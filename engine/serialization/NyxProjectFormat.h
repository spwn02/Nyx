#pragma once

#include "serialization/NyxChunkIDs.h"
#include <cstdint>

namespace Nyx {

static constexpr uint64_t NYXPROJ_MAGIC = 0x004E595850524F31ull; // "\0NYXPRO1"
static constexpr uint32_t NYXPROJ_VERSION = 0x00010000u;

enum class NyxProjChunk : uint32_t {
  HEAD = FOURCC('H', 'E', 'A', 'D'),
  STRS = FOURCC('S', 'T', 'R', 'S'),
  MNT = FOURCC('M', 'N', 'T', ' '),
  STRT = FOURCC('S', 'T', 'R', 'T'),
  EDTR = FOURCC('E', 'D', 'T', 'R'),
  TOC = FOURCC('T', 'O', 'C', ' '),
};

} // namespace Nyx
