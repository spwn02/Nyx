#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace Nyx {

static_assert(sizeof(uint8_t) == 1, "u8 size");
static_assert(sizeof(uint16_t) == 2, "u16 size");
static_assert(sizeof(uint32_t) == 4, "u32 size");
static_assert(sizeof(uint64_t) == 8, "u64 size");

inline bool hostIsLittleEndian() {
  const uint32_t x = 0x01020304u;
  return *(const uint8_t *)&x == 0x04;
}

class BinaryWriter final {
public:
  void clear() { m_buf.clear(); }
  const std::vector<uint8_t> &data() const { return m_buf; }
  std::vector<uint8_t> &&moveData() { return std::move(m_buf); }
  size_t size() const { return m_buf.size(); }

  void writeBytes(const void *ptr, size_t n) {
    if (!ptr || n == 0)
      return;
    const size_t off = m_buf.size();
    m_buf.resize(off + n);
    std::memcpy(m_buf.data() + off, ptr, n);
  }

  template <class T> void writePodLE(const T &v) {
    static_assert(std::is_trivially_copyable_v<T>, "writePodLE: POD only");
    if constexpr (sizeof(T) == 1) {
      writeBytes(&v, 1);
    } else {
      if (hostIsLittleEndian()) {
        writeBytes(&v, sizeof(T));
      } else {
        T tmp = v;
        uint8_t *p = (uint8_t *)&tmp;
        for (size_t i = 0, j = sizeof(T) - 1; i < j; ++i, --j) {
          const uint8_t t = p[i];
          p[i] = p[j];
          p[j] = t;
        }
        writeBytes(&tmp, sizeof(T));
      }
    }
  }

  void writeU8(uint8_t v) { writePodLE(v); }
  void writeU16(uint16_t v) { writePodLE(v); }
  void writeU32(uint32_t v) { writePodLE(v); }
  void writeU64(uint64_t v) { writePodLE(v); }
  void writeI32(int32_t v) { writePodLE(v); }
  void writeF32(float v) { writePodLE(v); }

  void writeStringU32(const std::string &s) {
    writeU32((uint32_t)s.size());
    writeBytes(s.data(), s.size());
  }

  void align(size_t alignment) {
    if (alignment <= 1)
      return;
    const size_t r = m_buf.size() % alignment;
    if (r == 0)
      return;
    const size_t pad = alignment - r;
    const uint8_t zero = 0;
    for (size_t i = 0; i < pad; ++i)
      writeBytes(&zero, 1);
  }

private:
  std::vector<uint8_t> m_buf;
};

class BinaryReader final {
public:
  BinaryReader() = default;
  BinaryReader(const uint8_t *data, size_t size) { reset(data, size); }

  void reset(const uint8_t *data, size_t size) {
    m_data = data;
    m_size = size;
    m_off = 0;
  }

  size_t size() const { return m_size; }
  size_t tell() const { return m_off; }
  bool eof() const { return m_off >= m_size; }

  bool skip(size_t n) {
    if (m_off + n > m_size)
      return false;
    m_off += n;
    return true;
  }

  bool readBytes(void *out, size_t n) {
    if (!out || m_off + n > m_size)
      return false;
    std::memcpy(out, m_data + m_off, n);
    m_off += n;
    return true;
  }

  template <class T> bool readPodLE(T &out) {
    static_assert(std::is_trivially_copyable_v<T>, "readPodLE: POD only");
    if (m_off + sizeof(T) > m_size)
      return false;

    if constexpr (sizeof(T) == 1) {
      std::memcpy(&out, m_data + m_off, 1);
      m_off += 1;
      return true;
    } else {
      if (hostIsLittleEndian()) {
        std::memcpy(&out, m_data + m_off, sizeof(T));
        m_off += sizeof(T);
        return true;
      }

      T tmp{};
      std::memcpy(&tmp, m_data + m_off, sizeof(T));
      uint8_t *p = (uint8_t *)&tmp;
      for (size_t i = 0, j = sizeof(T) - 1; i < j; ++i, --j) {
        const uint8_t t = p[i];
        p[i] = p[j];
        p[j] = t;
      }
      out = tmp;
      m_off += sizeof(T);
      return true;
    }
  }

  bool readU8(uint8_t &v) { return readPodLE(v); }
  bool readU16(uint16_t &v) { return readPodLE(v); }
  bool readU32(uint32_t &v) { return readPodLE(v); }
  bool readU64(uint64_t &v) { return readPodLE(v); }
  bool readI32(int32_t &v) { return readPodLE(v); }
  bool readF32(float &v) { return readPodLE(v); }

  bool readStringU32(std::string &out) {
    uint32_t len = 0;
    if (!readU32(len))
      return false;
    if (m_off + len > m_size)
      return false;
    out.assign((const char *)(m_data + m_off), (size_t)len);
    m_off += len;
    return true;
  }

  bool readSpan(const uint8_t *&ptr, size_t n) {
    if (m_off + n > m_size)
      return false;
    ptr = m_data + m_off;
    m_off += n;
    return true;
  }

private:
  const uint8_t *m_data = nullptr;
  size_t m_size = 0;
  size_t m_off = 0;
};

} // namespace Nyx
