#include "Renderer.h"

#include "core/Assert.h"
#include "core/Log.h"
#include "core/Paths.h"

#include "app/EngineContext.h"
#include "npgms/MeshCPU.h"
#include "npgms/PrimitiveGenerator.h"
#include "render/rg/RenderContext.h"
#include "scene/RenderableRegistry.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <sstream>
#include <vector>

namespace Nyx {

// ------------------------------------------------------------
// Small file helper
// ------------------------------------------------------------
static std::string readTextFile(const std::string &path) {
  auto p = std::filesystem::absolute(path);
  std::ifstream file(p);
  if (!file.is_open()) {
    Log::Error("Failed to open file: {}", p.string());
    return "";
  }
  std::stringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

static constexpr uint32_t kSelectedIDsBinding = 15;

Renderer::Renderer() : m_rgRes(m_res) {
  m_fsTri.init();
  m_tonemap.init();

  std::string kPresentVS = readTextFile(Paths::shader("present.vert"));
  std::string kPresentFS = readTextFile(Paths::shader("present.frag"));
  std::string kForwardVS = readTextFile(Paths::shader("forward.vert"));
  std::string kForwardFS = readTextFile(Paths::shader("forward.frag"));
  std::string kOutlineVS = readTextFile(Paths::shader("fullscreen.vert"));
  std::string kOutlineFS = readTextFile(Paths::shader("outline.frag"));

  {
    uint32_t vs = compileShader(GL_VERTEX_SHADER, kPresentVS);
    uint32_t fs = compileShader(GL_FRAGMENT_SHADER, kPresentFS);
    m_presentProg = linkProgram(vs, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);
  }

  {
    uint32_t vs = compileShader(GL_VERTEX_SHADER, kForwardVS);
    uint32_t fs = compileShader(GL_FRAGMENT_SHADER, kForwardFS);
    m_forwardProg = linkProgram(vs, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);
  }

  {
    uint32_t vs = compileShader(GL_VERTEX_SHADER, kOutlineVS);
    uint32_t fs = compileShader(GL_FRAGMENT_SHADER, kOutlineFS);
    m_outlineProg = linkProgram(vs, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);
  }

  m_hdrFbo = m_res.acquireFBO();
  m_outlineFbo = m_res.acquireFBO();

  glCreateBuffers(1, &m_selectedSSBO);
  std::vector<uint32_t> init{0u}; // selectedCount=0
  glNamedBufferData(m_selectedSSBO,
                    (GLsizeiptr)(init.size() * sizeof(uint32_t)), init.data(),
                    GL_DYNAMIC_DRAW);
}

Renderer::~Renderer() {
  m_tonemap.shutdown();
  m_fsTri.shutdown();

  if (m_presentProg)
    glDeleteProgram(m_presentProg);
  if (m_forwardProg)
    glDeleteProgram(m_forwardProg);
  if (m_outlineProg)
    glDeleteProgram(m_outlineProg);

  if (m_selectedSSBO)
    glDeleteBuffers(1, &m_selectedSSBO);

  m_res.releaseFBO(m_hdrFbo);
  m_res.releaseFBO(m_outlineFbo);
}

void Renderer::setSelectedPickIDs(const std::vector<uint32_t> &ids) {
  const uint32_t count = (uint32_t)ids.size();
  std::vector<uint32_t> tmp;
  tmp.reserve(count + 1);
  tmp.push_back(count);
  for (uint32_t v : ids)
    tmp.push_back(v);

  glNamedBufferData(m_selectedSSBO, (GLsizeiptr)(tmp.size() * sizeof(uint32_t)),
                    tmp.data(), GL_DYNAMIC_DRAW);
  m_selectedCount = count;
}

static uint32_t primIndex(ProcMeshType t) {
  switch (t) {
  case ProcMeshType::Cube:
    return 0;
  case ProcMeshType::Plane:
    return 1;
  case ProcMeshType::Circle:
    return 2;
  case ProcMeshType::Sphere:
    return 3;
  case ProcMeshType::Monkey:
    return 4;
  default:
    return 0;
  }
}

void Renderer::drawPrimitive(ProcMeshType t) {
  const uint32_t i = primIndex(t);
  if (!m_primReady[i]) {
    MeshCPU cpu = makePrimitivePN(t, 32);
    m_primMeshes[i].upload(cpu);
    m_primReady[i] = true;
  }
  m_primMeshes[i].draw();
}

uint32_t Renderer::renderFrame(const RenderContext &ctx, bool editorVisible,
                               const RenderableRegistry &registry,
                               const std::vector<uint32_t> &selectedPickIDs,
                               EngineContext &engine) {
  // Selection SSBO for outline
  setSelectedPickIDs(selectedPickIDs);
  const bool wantOutline = (m_selectedCount > 0);

  // Begin RG frame
  m_graph.reset();
  m_rgRes.beginFrame(ctx.frameIndex, ctx.fbWidth, ctx.fbHeight);

  const RGTexDesc hdrDesc{ctx.fbWidth, ctx.fbHeight, RGFormat::RGBA16F};
  const RGTexDesc depDesc{ctx.fbWidth, ctx.fbHeight, RGFormat::Depth32F};
  const RGTexDesc idDesc{ctx.fbWidth, ctx.fbHeight, RGFormat::R32UI};
  const RGTexDesc ldrDesc{ctx.fbWidth, ctx.fbHeight, RGFormat::RGBA8};

  m_out.hdr = m_rgRes.acquireTex("HDRColor", hdrDesc);
  m_out.depth = m_rgRes.acquireTex("Depth", depDesc);
  m_out.id = m_rgRes.acquireTex("ID", idDesc);
  m_out.ldr = m_rgRes.acquireTex("LDR", ldrDesc);
  m_out.outlined =
      wantOutline ? m_rgRes.acquireTex("Outlined", ldrDesc) : InvalidRG;

  // ------------------------------------------------------------
  // Pass: ForwardPlusHDR (PBR stub)
  // Writes: HDRColor + ID + Depth
  // ------------------------------------------------------------
  m_graph.addPass("ForwardPlusHDR", [&](RGResources &rg) {
    const auto &hdrT = rg.tex(m_out.hdr);
    const auto &depT = rg.tex(m_out.depth);
    const auto &idT = rg.tex(m_out.id);

    glBindFramebuffer(GL_FRAMEBUFFER, m_hdrFbo);

    glNamedFramebufferTexture(m_hdrFbo, GL_COLOR_ATTACHMENT0, hdrT.tex, 0);
    glNamedFramebufferTexture(m_hdrFbo, GL_COLOR_ATTACHMENT1, idT.tex, 0);
    glNamedFramebufferTexture(m_hdrFbo, GL_DEPTH_ATTACHMENT, depT.tex, 0);

    const std::array<GLenum, 2> bufs{GL_COLOR_ATTACHMENT0,
                                     GL_COLOR_ATTACHMENT1};
    glNamedFramebufferDrawBuffers(m_hdrFbo, (GLsizei)bufs.size(), bufs.data());

    NYX_ASSERT(glCheckNamedFramebufferStatus(m_hdrFbo, GL_FRAMEBUFFER) ==
                   GL_FRAMEBUFFER_COMPLETE,
               "HDR framebuffer incomplete");

    glViewport(0, 0, (int)ctx.fbWidth, (int)ctx.fbHeight);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    // Clears
    const float clearC[4] = {0.1f, 0.1f, 0.2f, 0.0f};
    glClearBufferfv(GL_COLOR, 0, clearC);

    const uint32_t clearID[1] = {0u};
    glClearBufferuiv(GL_COLOR, 1, clearID);

    const float clearZ[1] = {1.0f};
    glClearBufferfv(GL_DEPTH, 0, clearZ);

    // Materials
    engine.materials().uploadIfDirty();
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 14, engine.materials().ssbo());

    glUseProgram(m_forwardProg);

    const int locVP = glGetUniformLocation(m_forwardProg, "u_ViewProj");
    const int locM = glGetUniformLocation(m_forwardProg, "u_Model");
    const int locPick = glGetUniformLocation(m_forwardProg, "u_PickID");
    const int locVM = glGetUniformLocation(m_forwardProg, "u_ViewMode");
    const int locMat = glGetUniformLocation(m_forwardProg, "u_MaterialIndex");
    glUniform1ui(locVM, static_cast<uint32_t>(engine.viewMode()));

    glUniformMatrix4fv(locVP, 1, GL_FALSE, &ctx.viewProj[0][0]);

    for (const auto &r : registry.all()) {
      glUniformMatrix4fv(locM, 1, GL_FALSE, &r.model[0][0]);
      glUniform1ui(locPick, r.pickID);
      glUniform1ui(locMat, r.materialGpuIndex);
      drawPrimitive(r.mesh);
    }
  });

  // ------------------------------------------------------------
  // Pass: Tonemap (HDR -> LDR)
  // ------------------------------------------------------------
  m_graph.addPass("Tonemap", [&](RGResources &rg) {
    const auto &hdrT = rg.tex(m_out.hdr);
    const auto &ldrT = rg.tex(m_out.ldr);
    m_tonemap.dispatch(hdrT.tex, ldrT.tex, ctx.fbWidth, ctx.fbHeight,
                       /*exposure=*/1.0f,
                       /*applyGamma=*/true);
  });

  // ------------------------------------------------------------
  // Pass: Outline (optional) preserves original shading
  // Reads: LDR + Depth + ID
  // Writes: Outlined
  // ------------------------------------------------------------
  m_graph.addPass("Outline", [&](RGResources &rg) {
    if (!wantOutline)
      return;

    const auto &ldrT = rg.tex(m_out.ldr);
    const auto &depT = rg.tex(m_out.depth);
    const auto &idT = rg.tex(m_out.id);
    const auto &outT = rg.tex(m_out.outlined);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kSelectedIDsBinding,
                     m_selectedSSBO);

    glBindFramebuffer(GL_FRAMEBUFFER, m_outlineFbo);
    glNamedFramebufferTexture(m_outlineFbo, GL_COLOR_ATTACHMENT0, outT.tex, 0);

    const GLenum drawBuf = GL_COLOR_ATTACHMENT0;
    glNamedFramebufferDrawBuffers(m_outlineFbo, 1, &drawBuf);

    NYX_ASSERT(glCheckNamedFramebufferStatus(m_outlineFbo, GL_FRAMEBUFFER) ==
                   GL_FRAMEBUFFER_COMPLETE,
               "Outline framebuffer incomplete");

    glViewport(0, 0, (int)ctx.fbWidth, (int)ctx.fbHeight);
    glDisable(GL_DEPTH_TEST);

    glUseProgram(m_outlineProg);
    glBindVertexArray(m_fsTri.vao);

    glBindTextureUnit(0, ldrT.tex); // uSceneColor
    glBindTextureUnit(1, depT.tex); // uDepth
    glBindTextureUnit(2, idT.tex);  // uID

    glDrawArrays(GL_TRIANGLES, 0, 3);
  });

  // ------------------------------------------------------------
  // Pass: Present (fullscreen only)
  // ------------------------------------------------------------
  m_graph.addPass("Present", [&](RGResources &rg) {
    if (editorVisible)
      return;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, (int)ctx.fbWidth, (int)ctx.fbHeight);
    glDisable(GL_DEPTH_TEST);

    glUseProgram(m_presentProg);
    glBindVertexArray(m_fsTri.vao);

    const auto &finalT =
        wantOutline ? rg.tex(m_out.outlined) : rg.tex(m_out.ldr);
    glBindTextureUnit(0, finalT.tex);

    glDrawArrays(GL_TRIANGLES, 0, 3);
  });

  // Execute graph
  m_graph.execute(m_rgRes);

  // Editor wants the final texture id
  const auto &finalT =
      wantOutline ? m_rgRes.tex(m_out.outlined) : m_rgRes.tex(m_out.ldr);
  return finalT.tex;
}

uint32_t Renderer::readPickID(uint32_t px, uint32_t py,
                              uint32_t fbHeight) const {
  if (m_out.id == InvalidRG)
    return 0u;
  if (fbHeight == 0u)
    return 0u;

  const uint32_t glY = (fbHeight - 1u) > py ? (fbHeight - 1u - py) : 0u;

  uint32_t id = 0;
  const auto &idT = m_rgRes.tex(m_out.id);

  static uint32_t s_readFbo = 0;
  if (s_readFbo == 0)
    glCreateFramebuffers(1, &s_readFbo);

  glNamedFramebufferTexture(s_readFbo, GL_COLOR_ATTACHMENT0, idT.tex, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, s_readFbo);
  glReadBuffer(GL_COLOR_ATTACHMENT0);

  glReadPixels((int)px, (int)glY, 1, 1, GL_RED_INTEGER, GL_UNSIGNED_INT, &id);

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  return id;
}

} // namespace Nyx
