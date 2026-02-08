#include "NyxBinaryWriter.h"
#include <cstdint>

namespace Nyx {

NyxBinaryWriter::NyxBinaryWriter(const std::string &path) {
  m_out.open(path, std::ios::binary);
  m_ok = m_out.good();
}

NyxBinaryWriter::~NyxBinaryWriter() {
  if (m_out.is_open())
    m_out.close();
}

uint64_t NyxBinaryWriter::tell() const {
  return static_cast<uint64_t>(m_out.tellp());
}

void NyxBinaryWriter::writeU8(uint8_t v) {
  m_out.write(reinterpret_cast<const char *>(&v), sizeof(v));
}
void NyxBinaryWriter::writeU32(uint32_t v) {
  m_out.write(reinterpret_cast<const char *>(&v), sizeof(v));
}
void NyxBinaryWriter::writeU64(uint64_t v) {
  m_out.write(reinterpret_cast<const char *>(&v), sizeof(v));
}
void NyxBinaryWriter::writeF32(float v) {
  m_out.write(reinterpret_cast<const char *>(&v), sizeof(v));
}

void NyxBinaryWriter::writeBytes(const void *data, size_t sz) {
  m_out.write(reinterpret_cast<const char *>(data), sz);
}

void NyxBinaryWriter::beginChunk(uint32_t fourcc, uint32_t version) {
  OpenChunk c{};
  c.fourcc = fourcc;
  c.version = version;
  c.headerOffset = tell();

  writeU32(fourcc);
  writeU32(version);
  writeU64(0);

  c.payloadStart = tell();
  m_chunkStack.push_back(c);
}

void NyxBinaryWriter::endChunk() {
  if (m_chunkStack.empty())
    return;

  const OpenChunk c = m_chunkStack.back();
  m_chunkStack.pop_back();

  const uint64_t end = tell();
  const uint64_t payloadSize = end - c.payloadStart;
  const uint64_t sizePos = c.headerOffset + 8;
  const auto cur = tell();
  m_out.seekp(sizePos);
  writeU64(payloadSize);
  m_out.seekp(cur);

  NyxTocEntry e{};
  e.fourcc = c.fourcc;
  e.version = c.version;
  e.offset = c.headerOffset;
  e.size = payloadSize;
  m_toc.push_back(e);
}

void NyxBinaryWriter::finalize() {
  const uint64_t tocPayloadOffset = tell();
  writeU32(static_cast<uint32_t>(m_toc.size()));
  for (const NyxTocEntry &e : m_toc) {
    writeU32(e.fourcc);
    writeU32(e.version);
    writeU64(e.offset);
    writeU64(e.size);
  }

  const uint64_t tocPayloadSize = tell() - tocPayloadOffset;
  writeU32(static_cast<uint32_t>(NyxChunk::TOC));
  writeU32(1); // toc footer version
  writeU64(tocPayloadSize);
  writeU64(tocPayloadOffset);
  writeU64(NYX_TOC_FOOTER_MAGIC);
}

} // namespace Nyx
