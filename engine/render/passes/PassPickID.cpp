#include "PassPickID.h"

#include "app/EngineContext.h"
#include "core/Assert.h"
#include "render/material/MaterialGraph.h"
#include "render/material/MaterialSystem.h"
#include "render/material/TextureTable.h"
#include "scene/RenderableRegistry.h"

#include <glad/glad.h>
#include <unordered_map>

namespace Nyx {

static constexpr uint32_t kMaterialsBinding = 14;
static constexpr uint32_t kPerDrawBinding = 13;

PassPickID::~PassPickID() {
  if (m_fbo != 0 && m_res) {
    m_res->releaseFBO(m_fbo);
    m_fbo = 0;
  }
  if (m_prog != 0) {
    glDeleteProgram(m_prog);
    m_prog = 0;
  }
}

void PassPickID::configure(GLShaderUtil &shaders, GLResources &res) {
  m_res = &res;
  m_fbo = res.acquireFBO();
  m_prog = shaders.buildProgramVF("pick_id.vert", "pick_id.frag");
}

void PassPickID::setup(RenderGraph &graph, const RenderPassContext &ctx,
                       const RenderableRegistry &registry,
                       EngineContext &engine, bool editorVisible) {
  (void)ctx;
  (void)editorVisible;

  graph.addPass(
      "PickID",
      [&](RenderPassBuilder &b) {
        b.writeTexture("ID.Pick", RenderAccess::ColorWrite);
        b.writeTexture("Depth.Pick", RenderAccess::DepthWrite);
        b.readBuffer("Scene.PerDraw", RenderAccess::SSBORead);
      },
      [&](const RenderPassContext &rc, RenderResourceBlackboard &bb,
          RGResources &rg) {
        NYX_ASSERT(m_prog != 0, "PickID: missing program");
        const auto &idT = tex(bb, rg, "ID.Pick");
        const auto &depT = tex(bb, rg, "Depth.Pick");

        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        glNamedFramebufferTexture(m_fbo, GL_COLOR_ATTACHMENT0, idT.tex, 0);
        glNamedFramebufferTexture(m_fbo, GL_DEPTH_ATTACHMENT, depT.tex, 0);

        const GLenum drawBuf = GL_COLOR_ATTACHMENT0;
        glNamedFramebufferDrawBuffers(m_fbo, 1, &drawBuf);

        NYX_ASSERT(glCheckNamedFramebufferStatus(m_fbo, GL_FRAMEBUFFER) ==
                       GL_FRAMEBUFFER_COMPLETE,
                   "PickID framebuffer incomplete");

        glViewport(0, 0, (int)rc.fbWidth, (int)rc.fbHeight);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);

        const uint32_t clearID[1] = {0u};
        glClearBufferuiv(GL_COLOR, 0, clearID);
        const float clearZ = 1.0f;
        glClearBufferfv(GL_DEPTH, 0, &clearZ);

        glUseProgram(m_prog);

        const int locVP = glGetUniformLocation(m_prog, "u_ViewProj");
        const int locCam = glGetUniformLocation(m_prog, "u_CamPos");
        const int locTexRemapCount =
            glGetUniformLocation(m_prog, "u_TexRemapCount");

        glUniformMatrix4fv(locVP, 1, GL_FALSE, &rc.viewProj[0][0]);
        glUniform3f(locCam, rc.cameraPos.x, rc.cameraPos.y, rc.cameraPos.z);

        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kPerDrawBinding,
                         engine.perDraw().ssbo());
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kMaterialsBinding,
                         engine.materials().ssbo());
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 16,
                         engine.materials().graphHeadersSSBO());
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 17,
                         engine.materials().graphNodesSSBO());

        // Build a compact texture table for this frame (<=16).
        auto &texTable = engine.materials().textures();
        std::vector<uint32_t> compactOrig;
        compactOrig.reserve(16);
        std::unordered_map<uint32_t, uint32_t> used;

        auto tryAdd = [&](uint32_t origIdx) {
          if (origIdx == kInvalidTexIndex)
            return;
          if (used.find(origIdx) != used.end())
            return;
          if (compactOrig.size() >= 16)
            return;
          uint32_t compactIdx = static_cast<uint32_t>(compactOrig.size());
          used[origIdx] = compactIdx;
          compactOrig.push_back(origIdx);
        };

        const auto &drawList = registry.all();
        for (const auto &r : drawList) {
          if (engine.isEntityHidden(r.entity))
            continue;
          (void)engine.materialIndex(r);
          if (!engine.world().hasMesh(r.entity))
            continue;
          const auto &mc = engine.world().mesh(r.entity);
          if (r.submesh >= mc.submeshes.size())
            continue;
          const auto &sm = mc.submeshes[r.submesh];
          if (!engine.materials().isAlive(sm.material))
            continue;

          const auto &gpu = engine.materials().gpu(sm.material);
          tryAdd(gpu.tex0123.x);
          tryAdd(gpu.tex0123.y);
          tryAdd(gpu.tex0123.z);
          tryAdd(gpu.tex0123.w);
          tryAdd(gpu.tex4_pad.x);

          const auto &graph = engine.materials().graph(sm.material);
          for (const auto &node : graph.nodes) {
            switch (node.type) {
            case MatNodeType::Texture2D:
            case MatNodeType::TextureMRA:
            case MatNodeType::NormalMap:
              tryAdd(node.u.x);
              break;
            default:
              break;
            }
          }
        }

        const auto &glTex = texTable.glTextures();
        std::vector<uint32_t> remap(glTex.size(), TextureTable::Invalid);

        for (size_t i = 0; i < compactOrig.size(); ++i) {
          const uint32_t origIdx = compactOrig[i];
          if (origIdx < remap.size())
            remap[origIdx] = static_cast<uint32_t>(i);
        }

        glNamedBufferData(engine.texRemapSSBO(),
                          remap.size() * sizeof(uint32_t), remap.data(),
                          GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 15, engine.texRemapSSBO());

        if (locTexRemapCount >= 0)
          glUniform1ui(locTexRemapCount,
                       static_cast<uint32_t>(remap.size()));

        // Bind compacted textures at unit 10 (matches pick_id.frag).
        for (uint32_t i = 0; i < 16; ++i) {
          uint32_t tex = 0;
          if (i < compactOrig.size()) {
            tex = texTable.glTexByIndex(compactOrig[i]);
          }
          glBindTextureUnit(10 + i, tex);
        }

        const uint32_t baseOpaque = engine.perDrawOpaqueOffset();
        const uint32_t baseTrans = engine.perDrawTransparentOffset();

        uint32_t visibleOpaque = 0;
        for (uint32_t i = 0; i < (uint32_t)registry.opaque().size(); ++i) {
          const auto &r = registry.opaque()[i];
          if (engine.isEntityHidden(r.entity) || r.isCamera)
            continue;
          engine.rendererDrawPrimitive(static_cast<uint32_t>(r.mesh),
                                       baseOpaque + visibleOpaque);
          visibleOpaque++;
        }

        uint32_t visibleTrans = 0;
        for (uint32_t i = 0; i < (uint32_t)registry.transparentSorted().size();
             ++i) {
          const auto &r = registry.transparentSorted()[i];
          if (engine.isEntityHidden(r.entity) || r.isCamera)
            continue;
          engine.rendererDrawPrimitive(static_cast<uint32_t>(r.mesh),
                                       baseTrans + visibleTrans);
          visibleTrans++;
        }

        glDepthMask(GL_TRUE);
      });
}

} // namespace Nyx
