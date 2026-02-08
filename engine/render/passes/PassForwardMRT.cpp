#include "PassForwardMRT.h"

#include "app/EngineContext.h"
#include "core/Assert.h"
#include "scene/World.h"
#include "render/material/GpuMaterial.h"
#include "render/material/MaterialGraph.h"

#include <array>
#include <glad/glad.h>
#include <unordered_map>

namespace Nyx {

static constexpr uint32_t kMaterialsBinding = 14;
static constexpr uint32_t kLightsBinding = 20;
static constexpr uint32_t kPerDrawBinding = 13;

PassForwardMRT::~PassForwardMRT() {
  if (m_fbo != 0 && m_res) {
    m_res->releaseFBO(m_fbo);
    m_fbo = 0;
  }
  if (m_forwardProg != 0) {
    glDeleteProgram(m_forwardProg);
    m_forwardProg = 0;
  }
}

void PassForwardMRT::configure(GLShaderUtil &shader, GLResources &res,
                               std::function<void(ProcMeshType)> drawFn) {
  m_res = &res;

  m_fbo = res.acquireFBO();
  m_forwardProg = shader.buildProgramVF("forward_mrt.vert", "forward_mrt.frag");
  m_draw = std::move(drawFn);
}

void PassForwardMRT::setup(RenderGraph &graph, const RenderPassContext &ctx,
                           const RenderableRegistry &registry,
                           EngineContext &engine, bool editorVisible) {
  (void)ctx;
  (void)editorVisible;

  const char *passName =
      (m_mode == Mode::Transparent) ? "ForwardMRT_Transparent"
                                    : "ForwardMRT_Opaque";
  graph.addPass(
      passName,
      [&](RenderPassBuilder &b) {
        b.writeTexture("HDR.Color", RenderAccess::ColorWrite);
        b.writeTexture("ID.Submesh", RenderAccess::ColorWrite);
        b.readTexture("Depth.Pre", RenderAccess::SampledRead);
        b.readTexture("Shadow.CSMAtlas", RenderAccess::SampledRead);
        b.readTexture("Shadow.SpotAtlas", RenderAccess::SampledRead);
        b.readTexture("Shadow.DirAtlas", RenderAccess::SampledRead);
        b.readTexture("Shadow.PointArray", RenderAccess::SampledRead);
        b.readBuffer("Scene.Lights", RenderAccess::SSBORead);
        b.readBuffer("Scene.PerDraw", RenderAccess::SSBORead);
        b.readBuffer("LightGrid.Meta", RenderAccess::UBORead);
        b.readBuffer("LightGrid.Header", RenderAccess::SSBORead);
        b.readBuffer("LightGrid.Indices", RenderAccess::SSBORead);
      },
      [&](const RenderPassContext &rc, RenderResourceBlackboard &bb,
          RGResources &rg) {
        const auto &hdrT = tex(bb, rg, "HDR.Color");
        const auto &idT = tex(bb, rg, "ID.Submesh");
        const auto &depT = tex(bb, rg, "Depth.Pre");

        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

        glNamedFramebufferTexture(m_fbo, GL_COLOR_ATTACHMENT0, hdrT.tex, 0);
        glNamedFramebufferTexture(m_fbo, GL_COLOR_ATTACHMENT1, idT.tex, 0);
        glNamedFramebufferTexture(m_fbo, GL_DEPTH_ATTACHMENT, depT.tex, 0);

        if (m_mode == Mode::Transparent) {
          const GLenum bufs[1] = {GL_COLOR_ATTACHMENT0};
          glNamedFramebufferDrawBuffers(m_fbo, 1, bufs);
          glColorMaski(1, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        } else {
          const std::array<GLenum, 2> bufs{GL_COLOR_ATTACHMENT0,
                                           GL_COLOR_ATTACHMENT1};
          glNamedFramebufferDrawBuffers(m_fbo, (GLsizei)bufs.size(),
                                        bufs.data());
          glColorMaski(1, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        }

        NYX_ASSERT(glCheckNamedFramebufferStatus(m_fbo, GL_FRAMEBUFFER) ==
                       GL_FRAMEBUFFER_COMPLETE,
                   "ForwardMRT framebuffer incomplete");

        glViewport(0, 0, (int)rc.fbWidth, (int)rc.fbHeight);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        if (m_mode == Mode::Transparent) {
          glDepthFunc(GL_LEQUAL);
          glEnable(GL_BLEND);
          glBlendEquation(GL_FUNC_ADD);
          glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE,
                              GL_ONE_MINUS_SRC_ALPHA);
        } else {
          glDepthFunc(GL_EQUAL);
          glDisable(GL_BLEND);
        }

        if (m_mode == Mode::Opaque) {
          const float clearC[4] = {0.1f, 0.1f, 0.2f, 0.0f};
          glClearBufferfv(GL_COLOR, 0, clearC);

          const uint32_t clearID[1] = {0u};
          glClearBufferuiv(GL_COLOR, 1, clearID);
        }

        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kMaterialsBinding,
                         engine.materials().ssbo());
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kPerDrawBinding,
                         engine.perDraw().ssbo());
        const auto &lights = buf(bb, rg, "Scene.Lights");
        const auto &gridMeta = buf(bb, rg, "LightGrid.Meta");
        const auto &gridHeader = buf(bb, rg, "LightGrid.Header");
        const auto &gridIndices = buf(bb, rg, "LightGrid.Indices");

        if (lights.buf)
          glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kLightsBinding, lights.buf);
        if (gridMeta.buf)
          glBindBufferBase(GL_UNIFORM_BUFFER, 22, gridMeta.buf);
        if (gridHeader.buf)
          glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 24, gridHeader.buf);
        if (gridIndices.buf)
          glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 25, gridIndices.buf);

        glUseProgram(m_forwardProg);

        const int locVP = glGetUniformLocation(m_forwardProg, "u_ViewProj");
        const int locV = glGetUniformLocation(m_forwardProg, "u_View");
        const int locCam = glGetUniformLocation(m_forwardProg, "u_CamPos");
        const int locVM = glGetUniformLocation(m_forwardProg, "u_ViewMode");
        const int locTexRemapCount =
            glGetUniformLocation(m_forwardProg, "u_TexRemapCount");
        const int locHasIBL = glGetUniformLocation(m_forwardProg, "u_HasIBL");

        glUniform1ui(locVM, static_cast<uint32_t>(engine.viewMode()));
        glUniformMatrix4fv(locVP, 1, GL_FALSE, &rc.viewProj[0][0]);
        if (locV >= 0)
          glUniformMatrix4fv(locV, 1, GL_FALSE, &rc.view[0][0]);
        if (locCam >= 0)
          glUniform3f(locCam, rc.cameraPos.x, rc.cameraPos.y, rc.cameraPos.z);

        const auto &csmAtlas = tex(bb, rg, "Shadow.CSMAtlas");
        const auto &spotAtlas = tex(bb, rg, "Shadow.SpotAtlas");
        const auto &dirAtlas = tex(bb, rg, "Shadow.DirAtlas");
        const auto &pointArray = tex(bb, rg, "Shadow.PointArray");
        
        if (csmAtlas.tex)
          glBindTextureUnit(6, csmAtlas.tex);
        if (spotAtlas.tex)
          glBindTextureUnit(7, spotAtlas.tex);
        if (dirAtlas.tex)
          glBindTextureUnit(8, dirAtlas.tex);
        if (pointArray.tex)
          glBindTextureUnit(9, pointArray.tex);

        // CSM UBO binding=5 (set by PassShadowCSM)
        glBindBufferBase(GL_UNIFORM_BUFFER, 5, engine.shadowCSMUBO());

        // Shadow metadata UBO binding=10
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 10, engine.lights().shadowMetadataUBO());

        // Sky UBO is already bound at binding point 2 by EngineContext

        auto &env = engine.envIBL();
        if (env.ready()) {
          glBindTextureUnit(0, env.envIrradianceCube());
          glBindTextureUnit(1, env.envPrefilteredCube());
          glBindTextureUnit(2, env.brdfLUT());
        }
        if (locHasIBL >= 0)
          glUniform1i(locHasIBL, env.ready() ? 1 : 0);

        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 14,
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

        const auto &drawList =
            (m_mode == Mode::Transparent) ? registry.transparentSorted()
                                          : registry.opaque();
        const uint32_t baseOffset =
            (m_mode == Mode::Transparent) ? engine.perDrawTransparentOffset()
                                          : engine.perDrawOpaqueOffset();
        uint32_t visibleIdx = 0;
        for (uint32_t i = 0; i < static_cast<uint32_t>(drawList.size()); ++i) {
          const auto &r = drawList[i];
          if (engine.isEntityHidden(r.entity))
            continue;
          (void)engine.materialIndex(r);
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

        // Bind compacted textures at unit 10 (matches forward_mrt.frag).
        for (uint32_t i = 0; i < 16; ++i) {
          uint32_t tex = 0;
          if (i < compactOrig.size()) {
            tex = texTable.glTexByIndex(compactOrig[i]);
          }
          glBindTextureUnit(10 + i, tex);
        }

        for (uint32_t i = 0; i < static_cast<uint32_t>(drawList.size()); ++i) {
          const auto &r = drawList[i];
          if (engine.isEntityHidden(r.entity))
            continue;
          if (r.isCamera) {
            glColorMaski(0, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
            glDepthMask(GL_FALSE);
          } else {
            glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            if (m_mode == Mode::Transparent)
              glDepthMask(GL_FALSE);
            else
              glDepthMask(GL_TRUE);
          }
          const uint32_t baseInstance = baseOffset + visibleIdx;
          engine.rendererDrawPrimitive(static_cast<uint32_t>(r.mesh),
                                       baseInstance);
          visibleIdx++;
        }
        glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glColorMaski(1, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
      });
}

} // namespace Nyx
