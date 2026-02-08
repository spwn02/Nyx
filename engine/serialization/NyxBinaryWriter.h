#pragma once

#include "NyxChunkIDs.h"
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace Nyx {

class NyxBinaryWriter {
public:
  explicit NyxBinaryWriter(const std::string &path);
  ~NyxBinaryWriter();

  bool ok() const { return m_ok; }

  void writeU8(uint8_t v);
  void writeU32(uint32_t v);
  void writeU64(uint64_t v);
  void writeF32(float v);
  void writeBytes(const void *data, size_t sz);

  void beginChunk(uint32_t fourcc, uint32_t version = 1);
  void endChunk();

  void finalize();
  uint64_t tell() const;

private:
  mutable std::ofstream m_out;
  bool m_ok = false;

  struct OpenChunk {
    uint32_t fourcc = 0;
    uint32_t version = 1;
    uint64_t headerOffset = 0;
    uint64_t payloadStart = 0;
  };

  std::vector<NyxTocEntry> m_toc;
  std::vector<OpenChunk> m_chunkStack;
};

} // namespace Nyx
