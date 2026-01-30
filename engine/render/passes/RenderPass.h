#pragma once

#include "render/rg/RGResources.h"
#include "render/rg/RenderGraph.h"
#include "render/rg/RenderPassContext.h"
#include "scene/RenderableRegistry.h"
#include "core/Assert.h"

namespace Nyx {

class EngineContext;

class RenderPass {
public:
  virtual ~RenderPass() = default;

  virtual void setup(RenderGraph &graph, const RenderPassContext &ctx,
                     const RenderableRegistry &registry, EngineContext &engine,
                     bool editorVisible) = 0;

protected:
  static const GLTexture2D &tex(RenderResourceBlackboard &bb, RGResources &rg,
                                const char *name) {
    RGTextureRef ref = bb.getTexture(name);
    NYX_ASSERT(ref != InvalidRGTexture, "Missing RG texture");
    return rg.tex(bb.textureHandle(ref));
  }
};

} // namespace Nyx
