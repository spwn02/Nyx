#include "NyxBinaryReader.h"

namespace Nyx {

NyxBinaryReader::NyxBinaryReader(const std::string &path) {
  m_in.open(path, std::ios::binary);
  m_ok = m_in.good();
}

NyxBinaryReader::~NyxBinaryReader() {
  if (m_in.is_open())
    m_in.close();
}

uint8_t NyxBinaryReader::readU8() {
  uint8_t v;
  m_in.read(reinterpret_cast<char *>(&v), sizeof(v));
  return v;
}
uint32_t NyxBinaryReader::readU32() {
  uint32_t v;
  m_in.read(reinterpret_cast<char *>(&v), sizeof(v));
  return v;
}
uint64_t NyxBinaryReader::readU64() {
  uint64_t v;
  m_in.read(reinterpret_cast<char *>(&v), sizeof(v));
  return v;
}
float NyxBinaryReader::readF32() {
  float v;
  m_in.read(reinterpret_cast<char *>(&v), sizeof(v));
  return v;
}

void NyxBinaryReader::readBytes(void *dst, size_t sz) {
  m_in.read(reinterpret_cast<char *>(dst), sz);
}

void NyxBinaryReader::seek(uint64_t abs) {
  m_in.seekg(static_cast<std::streamoff>(abs), std::ios::beg);
}

uint64_t NyxBinaryReader::tell() const {
  return static_cast<uint64_t>(m_in.tellg());
}

bool NyxBinaryReader::readSceneHeader(uint64_t &magic, uint32_t &version) {
  if (!m_ok)
    return false;
  magic = readU64();
  version = readU32();
  return true;
}

bool NyxBinaryReader::readChunkHeader(uint32_t &fourcc, uint32_t &version,
                                      uint64_t &size) {
  if (!m_in.good())
    return false;

  fourcc = readU32();
  version = readU32();
  size = readU64();
  return true;
}

bool NyxBinaryReader::loadTOC() {
  m_in.seekg(0, std::ios::end);
  const uint64_t fileSize = static_cast<uint64_t>(m_in.tellg());
  if (fileSize < 32)
    return false;

  seek(fileSize - 32);

  const uint32_t footerFourcc = readU32();
  const uint32_t tocVersion = readU32();
  const uint64_t tocPayloadSize = readU64();
  const uint64_t tocPayloadOffset = readU64();
  const uint64_t footerMagic = readU64();

  if (footerFourcc != static_cast<uint32_t>(NyxChunk::TOC) ||
      footerMagic != NYX_TOC_FOOTER_MAGIC) {
    return false;
  }
  (void)tocVersion;

  if (tocPayloadOffset + tocPayloadSize > fileSize)
    return false;

  seek(tocPayloadOffset);
  const uint32_t count = readU32();
  m_toc.clear();
  m_toc.reserve(count);

  for (uint32_t i = 0; i < count; ++i) {
    NyxTocEntry e{};
    e.fourcc = readU32();
    e.version = readU32();
    e.offset = readU64();
    e.size = readU64();
    m_toc.push_back(e);
  }

  m_index.clear();
  for (size_t i = 0; i < m_toc.size(); ++i) {
    m_index[m_toc[i].fourcc].push_back(i);
  }
  return true;
}

std::optional<NyxTocEntry> NyxBinaryReader::findChunk(uint32_t fourcc) const {
  auto it = m_index.find(fourcc);
  if (it == m_index.end() || it->second.empty())
    return std::nullopt;
  return m_toc[it->second.front()];
}

std::vector<NyxTocEntry> NyxBinaryReader::findAll(uint32_t fourcc) const {
  std::vector<NyxTocEntry> out;
  auto it = m_index.find(fourcc);
  if (it == m_index.end())
    return out;
  out.reserve(it->second.size());
  for (size_t idx : it->second)
    out.push_back(m_toc[idx]);
  return out;
}

void NyxBinaryReader::skip(uint64_t bytes) {
  m_in.seekg(static_cast<std::streamoff>(bytes), std::ios::cur);
}

} // namespace Nyx
