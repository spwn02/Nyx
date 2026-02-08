#include "PassMaterialPreview.h"

#include "app/EngineContext.h"
#include "core/Assert.h"
#include "render/material/GpuMaterial.h"
#include "render/material/TextureTable.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <unordered_map>
#include <vector>

namespace Nyx {

static constexpr uint32_t kMaterialsBinding = 14;
static constexpr uint32_t kTexRemapBinding = 15;

PassMaterialPreview::~PassMaterialPreview() {
  if (m_fbo != 0 && m_res) {
    m_res->releaseFBO(m_fbo);
    m_fbo = 0;
  }
  if (m_prog != 0) {
    glDeleteProgram(m_prog);
    m_prog = 0;
  }
}

void PassMaterialPreview::configure(GLShaderUtil &shader, GLResources &res,
                                    std::function<void(ProcMeshType)> drawFn) {
  m_res = &res;
  m_fbo = res.acquireFBO();
  m_prog =
      shader.buildProgramVF("preview_material.vert", "preview_material.frag");
  m_draw = std::move(drawFn);
}

void PassMaterialPreview::setup(RenderGraph &graph,
                                const RenderPassContext &ctx,
                                const RenderableRegistry &registry,
                                EngineContext &engine, bool editorVisible) {
  (void)ctx;
  (void)registry;
  (void)editorVisible;

  graph.addPass(
      "MaterialPreview",
      [&](RenderPassBuilder &b) {
        b.writeTexture("Preview.Material", RenderAccess::ColorWrite);
        b.writeTexture("Preview.MaterialDepth", RenderAccess::DepthWrite);
      },
      [&](const RenderPassContext &, RenderResourceBlackboard &bb,
          RGResources &rg) {
        NYX_ASSERT(m_prog != 0, "PassMaterialPreview: missing program");

        const auto &outT = tex(bb, rg, "Preview.Material");
        const auto &depT = tex(bb, rg, "Preview.MaterialDepth");

        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        glNamedFramebufferTexture(m_fbo, GL_COLOR_ATTACHMENT0, outT.tex, 0);
        glNamedFramebufferTexture(m_fbo, GL_DEPTH_ATTACHMENT, depT.tex, 0);

        const GLenum drawBuf = GL_COLOR_ATTACHMENT0;
        glNamedFramebufferDrawBuffers(m_fbo, 1, &drawBuf);

        NYX_ASSERT(glCheckNamedFramebufferStatus(m_fbo, GL_FRAMEBUFFER) ==
                       GL_FRAMEBUFFER_COMPLETE,
                   "MaterialPreview framebuffer incomplete");

        glViewport(0, 0, (int)outT.width, (int)outT.height);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);

        const float clearC[4] = {0.08f, 0.08f, 0.09f, 1.0f};
        glClearBufferfv(GL_COLOR, 0, clearC);
        const float clearDepth = 1.0f;
        glClearBufferfv(GL_DEPTH, 0, &clearDepth);

        const MaterialHandle mh = engine.previewMaterial();
        if (mh == InvalidMaterial || !engine.materials().isAlive(mh)) {
          return;
        }

        glUseProgram(m_prog);

        const float aspect = 1.0f;
        const glm::mat4 proj =
            glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
        const glm::vec3 camPos(0.0f, 0.0f, 2.5f);
        const glm::mat4 view =
            glm::lookAt(camPos, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 vp = proj * view;
        const glm::mat4 model(1.0f);

        const int locVP = glGetUniformLocation(m_prog, "u_ViewProj");
        const int locM = glGetUniformLocation(m_prog, "u_Model");
        const int locMat = glGetUniformLocation(m_prog, "u_MaterialIndex");
        const int locLightDir = glGetUniformLocation(m_prog, "u_LightDir");
        const int locLightColor = glGetUniformLocation(m_prog, "u_LightColor");
        const int locLightInt =
            glGetUniformLocation(m_prog, "u_LightIntensity");
        const int locLightExposure =
            glGetUniformLocation(m_prog, "u_LightExposure");
        const int locAmbient = glGetUniformLocation(m_prog, "u_Ambient");
        const int locCamPos = glGetUniformLocation(m_prog, "u_CamPos");
        const int locTexRemapCount =
            glGetUniformLocation(m_prog, "u_TexRemapCount");

        if (locVP >= 0)
          glUniformMatrix4fv(locVP, 1, GL_FALSE, &vp[0][0]);
        if (locM >= 0)
          glUniformMatrix4fv(locM, 1, GL_FALSE, &model[0][0]);

        const uint32_t matIndex = engine.materials().gpuIndex(mh);
        if (locMat >= 0)
          glUniform1ui(locMat, matIndex);

        glm::vec3 lightDir = engine.previewLightDir();
        if (glm::length(lightDir) < 1e-4f)
          lightDir = glm::vec3(0.0f, 1.0f, 0.0f);
        lightDir = glm::normalize(lightDir);
        if (locLightDir >= 0)
          glUniform3fv(locLightDir, 1, &lightDir[0]);
        const glm::vec3 lightColor = engine.previewLightColor();
        if (locLightColor >= 0)
          glUniform3fv(locLightColor, 1, &lightColor[0]);
        if (locLightInt >= 0)
          glUniform1f(locLightInt, engine.previewLightIntensity());
        if (locLightExposure >= 0)
          glUniform1f(locLightExposure, engine.previewLightExposure());
        if (locAmbient >= 0)
          glUniform1f(locAmbient, engine.previewAmbient());
        if (locCamPos >= 0)
          glUniform3fv(locCamPos, 1, &camPos[0]);

        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kMaterialsBinding,
                         engine.materials().ssbo());

        auto &texTable = engine.materials().textures();
        const auto &gpu = engine.materials().gpu(mh);

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

        tryAdd(gpu.tex0123.x);
        tryAdd(gpu.tex0123.y);
        tryAdd(gpu.tex0123.z);
        tryAdd(gpu.tex0123.w);
        tryAdd(gpu.tex4_pad.x);
        tryAdd(gpu.tex4_pad.y);

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
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kTexRemapBinding,
                         engine.texRemapSSBO());

        if (locTexRemapCount >= 0)
          glUniform1ui(locTexRemapCount,
                       static_cast<uint32_t>(remap.size()));

        for (uint32_t i = 0; i < 16; ++i) {
          uint32_t tex = 0;
          if (i < compactOrig.size())
            tex = texTable.glTexByIndex(compactOrig[i]);
          glBindTextureUnit(10 + i, tex);
        }

        if (m_draw)
          m_draw(ProcMeshType::Sphere);
      });
}

} // namespace Nyx
