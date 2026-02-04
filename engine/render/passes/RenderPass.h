#pragma once

#include "core/Assert.h"
#include "render/rg/RGResources.h"
#include "render/rg/RenderGraph.h"
#include "render/rg/RenderPassContext.h"
#include "scene/RenderableRegistry.h"

namespace Nyx {

class EngineContext;

class RenderPass {
public:
  virtual ~RenderPass() = default;

  virtual void setup(RenderGraph &graph, const RenderPassContext &ctx,
                     const RenderableRegistry &registry, EngineContext &engine,
                     bool editorVisible) = 0;

protected:
  uint32_t m_prog = 0;

  static const GLTexture2D &tex(RenderResourceBlackboard &bb, RGResources &rg,
                                const char *name) {
    RGTextureRef ref = bb.getTexture(name);
    NYX_ASSERT(ref != InvalidRGTexture, "Missing RG texture");
    return rg.tex(bb.textureHandle(ref));
  }

  static const GLBuffer &buf(RenderResourceBlackboard &bb, RGResources &rg,
                             const char *name) {
    RGBufferRef ref = bb.getBuffer(name);
    NYX_ASSERT(ref != InvalidRGBuffer, "Missing RG buffer");
    if (bb.isExternalBuffer(ref))
      return bb.externalBuffer(ref);
    return rg.buf(bb.bufferHandle(ref));
  }
};

} // namespace Nyx
