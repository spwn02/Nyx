#include "PassTonemapRG.h"

namespace Nyx {

void PassTonemapRG::init() { m_tonemap.init(); }
void PassTonemapRG::shutdown() { m_tonemap.shutdown(); }

void PassTonemapRG::setup(RenderGraph &graph, const RenderPassContext &ctx,
                          const RenderableRegistry &registry,
                          EngineContext &engine, bool editorVisible) {
  (void)ctx;
  (void)registry;
  (void)engine;
  (void)editorVisible;

  graph.addPass(
      "Tonemap",
      [&](RenderPassBuilder &b) {
        b.readTexture("HDR.Color", RenderAccess::SampledRead);
        b.writeTexture("Post.In", RenderAccess::ImageWrite);
      },
      [&](const RenderPassContext &rc, RenderResourceBlackboard &bb,
          RGResources &rg) {
        const auto &hdrT = tex(bb, rg, "HDR.Color");
        const auto &postT = tex(bb, rg, "Post.In");
        m_tonemap.dispatch(hdrT.tex, postT.tex, rc.fbWidth, rc.fbHeight,
                           /*exposure=*/1.0f,
                           /*applyGamma=*/true);
      });
}

} // namespace Nyx
