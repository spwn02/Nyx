#include "RGResources.h"
#include "core/Assert.h"
#include "render/gl/GLResources.h"
#include "render/rg/RGDesc.h"
#include "render/rg/RGFormat.h"
#include "render/rg/RGResource.h"

namespace Nyx {

RGHandle RGResources::acquireTex(const char* debugName, const RGTexDesc &desc) {
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
    .fmt = desc.fmt
  });

  return makeHandle(idx);
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
}

}
