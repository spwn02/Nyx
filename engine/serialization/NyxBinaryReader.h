#pragma once

#include "NyxChunkIDs.h"
#include <cstdint>
#include <fstream>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Nyx {

class NyxBinaryReader {
public:
  explicit NyxBinaryReader(const std::string &path);
  ~NyxBinaryReader();

  bool ok() const { return m_ok; }

  uint8_t readU8();
  uint32_t readU32();
  uint64_t readU64();
  float readF32();
  void readBytes(void *dst, size_t sz);

  void seek(uint64_t abs);
  uint64_t tell() const;

  bool readSceneHeader(uint64_t &magic, uint32_t &version);
  bool readChunkHeader(uint32_t &fourcc, uint32_t &version, uint64_t &size);

  bool loadTOC();
  std::optional<NyxTocEntry> findChunk(uint32_t fourcc) const;
  std::vector<NyxTocEntry> findAll(uint32_t fourcc) const;

  void skip(uint64_t bytes);

private:
  mutable std::ifstream m_in;
  bool m_ok = false;
  std::vector<NyxTocEntry> m_toc;
  std::unordered_map<uint32_t, std::vector<size_t>> m_index;
};

} // namespace Nyx
