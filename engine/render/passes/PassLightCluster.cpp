#include "PassLightCluster.h"

#include "app/EngineContext.h"
#include "core/Assert.h"
#include "render/gl/GLShaderUtil.h"

#include <algorithm>
#include <glad/glad.h>
#include <glm/gtc/matrix_inverse.hpp>

namespace Nyx {

static uint32_t divUp(uint32_t a, uint32_t b) { return (a + b - 1u) / b; }

PassLightCluster::~PassLightCluster() {
  if (m_prog != 0) {
    glDeleteProgram(m_prog);
    m_prog = 0;
  }
}

void PassLightCluster::configure(GLShaderUtil &shaders) {
  m_prog = shaders.buildProgramC("passes/light_cluster.comp");
  NYX_ASSERT(m_prog != 0, "PassLightCluster: shader build failed");
}

void PassLightCluster::setup(RenderGraph &graph, const RenderPassContext &ctx,
                             const RenderableRegistry &registry,
                             EngineContext &engine, bool editorVisible) {
  (void)ctx;
  (void)registry;
  (void)engine;
  (void)editorVisible;

  graph.addPass(
      "LightCluster",
      [&](RenderPassBuilder &b) {
        b.readTexture("HiZ.Depth", RenderAccess::SampledRead);
        b.readBuffer("Scene.Lights", RenderAccess::SSBORead);
        b.readBuffer("LightGrid.Meta", RenderAccess::UBORead);
        b.writeBuffer("LightGrid.Header", RenderAccess::SSBOWrite);
        b.writeBuffer("LightGrid.Indices", RenderAccess::SSBOWrite);
      },
      [&](const RenderPassContext &rc, RenderResourceBlackboard &bb,
          RGResources &rg) {
        NYX_ASSERT(m_prog != 0, "PassLightCluster: not initialized");

        const auto &hiz = tex(bb, rg, "HiZ.Depth");
        const auto &lights = buf(bb, rg, "Scene.Lights");
        const auto &meta = buf(bb, rg, "LightGrid.Meta");
        const auto &header = buf(bb, rg, "LightGrid.Header");
        const auto &indices = buf(bb, rg, "LightGrid.Indices");

        NYX_ASSERT(hiz.tex, "PassLightCluster: missing HiZ.Depth");
        NYX_ASSERT(lights.buf && meta.buf && header.buf && indices.buf,
                   "PassLightCluster: missing buffers");

        const uint32_t tileSize = 16;
        const uint32_t tilesX = divUp(rc.fbWidth, tileSize);
        const uint32_t tilesY = divUp(rc.fbHeight, tileSize);
        const uint32_t zSlices = 16;
        const uint32_t maxPerCluster = 96;

        LightGridMetaGPU metaCPU{};
        metaCPU.tileCountX = tilesX;
        metaCPU.tileCountY = tilesY;
        metaCPU.tileSize = tileSize;
        metaCPU.zSlices = zSlices;
        metaCPU.maxPerCluster = maxPerCluster;
        metaCPU.lightCount = m_lightCount;
        metaCPU.nearZ = engine.cachedCameraNear();
        metaCPU.farZ = engine.cachedCameraFar();
        metaCPU.hizMip = 4;
        glNamedBufferSubData(meta.buf, 0, sizeof(LightGridMetaGPU), &metaCPU);

        glUseProgram(m_prog);
        glBindTextureUnit(0, hiz.tex);

        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 20, lights.buf);
        glBindBufferBase(GL_UNIFORM_BUFFER, 22, meta.buf);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 24, header.buf);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 25, indices.buf);

        const glm::mat4 invViewProj = glm::inverse(rc.viewProj);
        const GLint locInvVP = glGetUniformLocation(m_prog, "uInvViewProj");
        if (locInvVP >= 0)
          glUniformMatrix4fv(locInvVP, 1, GL_FALSE, &invViewProj[0][0]);

        const GLint locView = glGetUniformLocation(m_prog, "uView");
        if (locView >= 0)
          glUniformMatrix4fv(locView, 1, GL_FALSE, &rc.view[0][0]);

        const GLint locVPsz = glGetUniformLocation(m_prog, "uViewportSize");
        if (locVPsz >= 0)
          glUniform2f(locVPsz, (float)rc.fbWidth, (float)rc.fbHeight);

        const GLint locTile = glGetUniformLocation(m_prog, "uTileCount");
        if (locTile >= 0)
          glUniform2ui(locTile, tilesX, tilesY);

        const GLint locNear = glGetUniformLocation(m_prog, "uNear");
        const GLint locFar = glGetUniformLocation(m_prog, "uFar");
        if (locNear >= 0)
          glUniform1f(locNear, metaCPU.nearZ);
        if (locFar >= 0)
          glUniform1f(locFar, metaCPU.farZ);

        glDispatchCompute(tilesX, tilesY, zSlices);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT |
                        GL_TEXTURE_FETCH_BARRIER_BIT);
      });
}

} // namespace Nyx
