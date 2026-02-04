#include "RGResources.h"
#include "core/Assert.h"
#include "render/gl/GLResources.h"
#include "render/rg/RGDesc.h"
#include "render/rg/RGFormat.h"
#include "render/rg/RGResource.h"

#include <glad/glad.h>

namespace Nyx {

RGHandle RGResources::acquireTex(const char *debugName, const RGTexDesc &desc) {
  // Try reuse existing alive texture with same desc
  for (uint32_t i = 0; i < m_tex.size(); ++i) {
    RGTexture &tex = m_tex[i];
    if (tex.alive && tex.desc == desc) {
      tex.lastUsedFrame = m_frame;
      return makeHandle(i);
    }
  }

  // Allocate new or reuse free slot
  uint32_t idx = 0;
  if (!m_free.empty()) {
    idx = m_free.back();
    m_free.pop_back();
  } else {
    idx = static_cast<uint32_t>(m_tex.size());
    m_tex.push_back(RGTexture{});
  }

  auto &slot = m_tex[idx];
  if (slot.alive) {
    m_res.releaseTexture2D(slot.tex);
    slot.alive = false;
  }

  slot.desc = desc;
  slot.lastUsedFrame = m_frame;
  slot.alive = true;
  slot.gen = (slot.gen == 0 ? 1 : slot.gen + 1);

  slot.tex = m_res.acquireTexture2D({
      .w = desc.w,
      .h = desc.h,
      .layers = desc.layers,
      .mips = desc.mips,
      .fmt = desc.fmt,
      .usage = desc.usage,
  });

  return makeHandle(idx);
}

RGHandle RGResources::allocateTex(const char *debugName, const RGTexDesc &desc) {
  (void)debugName;
  uint32_t idx = 0;
  if (!m_free.empty()) {
    idx = m_free.back();
    m_free.pop_back();
  } else {
    idx = static_cast<uint32_t>(m_tex.size());
    m_tex.push_back(RGTexture{});
  }

  auto &slot = m_tex[idx];
  if (slot.tex.tex != 0 && !(slot.desc == desc)) {
    m_res.releaseTexture2D(slot.tex);
  }

  slot.desc = desc;
  slot.lastUsedFrame = m_frame;
  slot.alive = true;
  slot.gen = (slot.gen == 0 ? 1 : slot.gen + 1);

  if (slot.tex.tex == 0) {
    slot.tex = m_res.acquireTexture2D({
        .w = desc.w,
        .h = desc.h,
        .layers = desc.layers,
        .mips = desc.mips,
        .fmt = desc.fmt,
        .usage = desc.usage,
    });
  }

  return makeHandle(idx);
}

void RGResources::releaseTex(RGHandle h) {
  if (h == InvalidRG)
    return;
  if (h.idx >= m_tex.size())
    return;
  RGTexture &tex = m_tex[h.idx];
  if (!tex.alive || tex.gen != h.gen)
    return;
  tex.alive = false;
  tex.lastUsedFrame = m_frame;
  m_free.push_back(h.idx);
}

const GLTexture2D &RGResources::tex(RGHandle h) const {
  NYX_ASSERT(h != InvalidRG, "Invalid RGHandle");
  NYX_ASSERT(h.idx < m_tex.size(), "Invalid RGHandle");
  const RGTexture &tex = m_tex[h.idx];
  NYX_ASSERT(tex.alive, "RGTexture is not alive");
  NYX_ASSERT(tex.gen == h.gen, "RGHandle generation mismatch");
  return tex.tex;
}

GLTexture2D &RGResources::tex(RGHandle h) {
  NYX_ASSERT(h != InvalidRG, "Invalid RGHandle");
  NYX_ASSERT(h.idx < m_tex.size(), "Invalid RGHandle");
  RGTexture &tex = m_tex[h.idx];
  NYX_ASSERT(tex.alive, "RGTexture is not alive");
  NYX_ASSERT(tex.gen == h.gen, "RGHandle generation mismatch");
  return tex.tex;
}

const RGTexDesc &RGResources::desc(RGHandle h) const {
  NYX_ASSERT(h != InvalidRG, "Invalid RGHandle");
  NYX_ASSERT(h.idx < m_tex.size(), "Invalid RGHandle");
  const RGTexture &tex = m_tex[h.idx];
  NYX_ASSERT(tex.gen == h.gen, "RGHandle generation mismatch");
  return tex.desc;
}

static GLenum bufferUsageHint(const RGBufferDesc &desc) {
  return desc.dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW;
}

RGBufHandle RGResources::acquireBuf(const char *debugName,
                                    const RGBufferDesc &desc) {
  for (uint32_t i = 0; i < m_buf.size(); ++i) {
    RGBuffer &buf = m_buf[i];
    if (buf.alive && buf.desc == desc && buf.buf.byteSize == desc.byteSize) {
      buf.lastUsedFrame = m_frame;
      return makeBufHandle(i);
    }
  }
  return allocateBuf(debugName, desc);
}

RGBufHandle RGResources::allocateBuf(const char *debugName,
                                     const RGBufferDesc &desc) {
  (void)debugName;
  uint32_t idx = 0;
  if (!m_freeBuf.empty()) {
    idx = m_freeBuf.back();
    m_freeBuf.pop_back();
  } else {
    idx = static_cast<uint32_t>(m_buf.size());
    m_buf.push_back(RGBuffer{});
  }

  auto &slot = m_buf[idx];
  if (slot.buf.buf != 0) {
    glDeleteBuffers(1, &slot.buf.buf);
    slot.buf.buf = 0;
  }

  slot.desc = desc;
  slot.lastUsedFrame = m_frame;
  slot.alive = true;
  slot.gen = (slot.gen == 0 ? 1 : slot.gen + 1);

  glCreateBuffers(1, &slot.buf.buf);
  glNamedBufferData(slot.buf.buf, (GLsizeiptr)desc.byteSize, nullptr,
                    bufferUsageHint(desc));
  slot.buf.byteSize = desc.byteSize;

  return makeBufHandle(idx);
}

void RGResources::releaseBuf(RGBufHandle h) {
  if (h == InvalidRG)
    return;
  if (h.idx >= m_buf.size())
    return;
  RGBuffer &buf = m_buf[h.idx];
  if (!buf.alive || buf.gen != h.gen)
    return;
  buf.alive = false;
  buf.lastUsedFrame = m_frame;
  m_freeBuf.push_back(h.idx);
}

const GLBuffer &RGResources::buf(RGBufHandle h) const {
  static GLBuffer dummy{};
  if (h == InvalidRG)
    return dummy;
  if (h.idx >= m_buf.size())
    return dummy;
  const RGBuffer &buf = m_buf[h.idx];
  if (!buf.alive || buf.gen != h.gen)
    return dummy;
  return buf.buf;
}

GLBuffer &RGResources::buf(RGBufHandle h) {
  static GLBuffer dummy{};
  if (h == InvalidRG)
    return dummy;
  if (h.idx >= m_buf.size())
    return dummy;
  RGBuffer &buf = m_buf[h.idx];
  if (!buf.alive || buf.gen != h.gen)
    return dummy;
  return buf.buf;
}

const RGBufferDesc &RGResources::bufDesc(RGBufHandle h) const {
  static RGBufferDesc dummy{};
  if (h == InvalidRG)
    return dummy;
  if (h.idx >= m_buf.size())
    return dummy;
  const RGBuffer &buf = m_buf[h.idx];
  if (!buf.alive || buf.gen != h.gen)
    return dummy;
  return buf.desc;
}

void RGResources::gc(uint32_t keepFrames) {
  for (uint32_t i = 0; i < m_tex.size(); ++i) {
    auto &t = m_tex[i];
    if (!t.alive) continue;
    if ((m_frame  - t.lastUsedFrame) > keepFrames) {
      m_res.releaseTexture2D(t.tex);
      t.alive = false;
      m_free.push_back(i);
    }
  }

  for (uint32_t i = 0; i < m_buf.size(); ++i) {
    auto &b = m_buf[i];
    if (!b.alive)
      continue;
    if ((m_frame - b.lastUsedFrame) > keepFrames) {
      if (b.buf.buf != 0) {
        glDeleteBuffers(1, &b.buf.buf);
        b.buf.buf = 0;
      }
      b.buf.byteSize = 0;
      b.alive = false;
      m_freeBuf.push_back(i);
    }
  }
}

}
