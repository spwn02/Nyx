#pragma once

#include "RGDesc.h"
#include "RGResource.h"

#include "../gl/GLResources.h"
#include <cstdint>
#include <vector>

namespace Nyx {

struct RGTexture {
  GLTexture2D tex{};
  RGTexDesc desc{};
  uint32_t gen = 1;
  uint32_t lastUsedFrame = 0;
  bool alive = false;
};

struct RGBuffer {
  GLBuffer buf{};
  RGBufferDesc desc{};
  uint32_t gen = 1;
  uint32_t lastUsedFrame = 0;
  bool alive = false;
};

class RGResources {
public:
  explicit RGResources(GLResources &res) : m_res(res) {}

  void beginFrame(uint32_t frameIndex, uint32_t w, uint32_t h) {
    m_frame = frameIndex;
    m_fbW = w;
    m_fbH = h;
  }

  // Acquire a texture resource matching the description
  RGHandle acquireTex(const char *debugName, const RGTexDesc &desc);
  RGHandle allocateTex(const char *debugName, const RGTexDesc &desc);
  void releaseTex(RGHandle h);

  // Resolve handle -> GLTexture2D
  const GLTexture2D &tex(RGHandle h) const;
  GLTexture2D &tex(RGHandle h);
  const RGTexDesc &desc(RGHandle h) const;

  // Acquire a buffer resource matching the description
  RGBufHandle acquireBuf(const char *debugName, const RGBufferDesc &desc);
  RGBufHandle allocateBuf(const char *debugName, const RGBufferDesc &desc);
  void releaseBuf(RGBufHandle h);

  const GLBuffer &buf(RGBufHandle h) const;
  GLBuffer &buf(RGBufHandle h);
  const RGBufferDesc &bufDesc(RGBufHandle h) const;

  // Void: GC not used recently
  void gc(uint32_t keepFrames = 120);

  uint32_t fbW() const { return m_fbW; }
  uint32_t fbH() const { return m_fbH; }

private:
  GLResources &m_res;
  uint32_t m_frame = 0;
  uint32_t m_fbW = 1, m_fbH = 1;

  std::vector<RGTexture> m_tex;
  std::vector<uint32_t> m_free;

  std::vector<RGBuffer> m_buf;
  std::vector<uint32_t> m_freeBuf;

  RGHandle makeHandle(uint32_t idx) { return RGHandle{idx, m_tex[idx].gen}; }
  RGBufHandle makeBufHandle(uint32_t idx) {
    return RGBufHandle{idx, m_buf[idx].gen};
  }
};

} // namespace Nyx
