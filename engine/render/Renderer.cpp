#include "Renderer.h"

#include "core/Assert.h"
#include "core/Log.h"
#include "core/Paths.h"

#include "app/EngineContext.h"
#include "npgms/MeshCPU.h"
#include "npgms/PrimitiveGenerator.h"
#include "render/rg/RenderPassContext.h"
#include "scene/RenderableRegistry.h"

#include <array>
#include <cstdlib>
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

Renderer::Renderer() : m_rgRes(m_res) {
  m_fsTri.init();
  m_passTonemap.init();

  const char *rgDump = std::getenv("NYX_RG_DUMP");
  if (rgDump && rgDump[0] != '\0' && rgDump[0] != '0') {
    std::filesystem::path path =
        std::filesystem::current_path() / ".nyx" / "rendergraph.dot";
    std::filesystem::create_directories(path.parent_path());
    m_graph.enableDebug(path.string(), /*dumpLifetimes=*/true);
  }

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

  m_passDepthPre.configure(
      m_hdrFbo, m_forwardProg,
      [this](ProcMeshType t) { drawPrimitive(t); });
  m_passForward.configure(
      m_hdrFbo, m_forwardProg,
      [this](ProcMeshType t) { drawPrimitive(t); });
  m_passPost.configure(m_hdrFbo, m_presentProg, &m_fsTri);
  m_passSelection.configure(m_outlineFbo, m_outlineProg, &m_fsTri,
                            m_selectedSSBO);
  m_passPresent.configure(m_presentProg, &m_fsTri);
}

Renderer::~Renderer() {
  m_passTonemap.shutdown();
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

uint32_t Renderer::renderFrame(const RenderPassContext &ctx, bool editorVisible,
                               const RenderableRegistry &registry,
                               const std::vector<uint32_t> &selectedPickIDs,
                               EngineContext &engine) {
  // Selection SSBO for outline
  setSelectedPickIDs(selectedPickIDs);

  // Begin RG frame
  m_graph.reset();
  m_rgRes.beginFrame(ctx.frameIndex, ctx.fbWidth, ctx.fbHeight);

  const RenderTextureDesc depthDesc{
      .format = RGFormat::Depth32F,
      .usage = RGTexUsage::DepthAttach | RGTexUsage::Sampled,
      .extent = {RenderExtentKind::Framebuffer, 0, 0},
  };
  const RenderTextureDesc hdrDesc{
      .format = RGFormat::RGBA16F,
      .usage = RGTexUsage::ColorAttach | RGTexUsage::Sampled,
      .extent = {RenderExtentKind::Framebuffer, 0, 0},
  };
  const RenderTextureDesc idDesc{
      .format = RGFormat::R32UI,
      .usage = RGTexUsage::ColorAttach | RGTexUsage::Sampled,
      .extent = {RenderExtentKind::Framebuffer, 0, 0},
  };
  const RenderTextureDesc postInDesc{
      .format = RGFormat::RGBA8,
      .usage = RGTexUsage::Sampled | RGTexUsage::Image,
      .extent = {RenderExtentKind::Framebuffer, 0, 0},
  };
  const RenderTextureDesc ldrDesc{
      .format = RGFormat::RGBA8,
      .usage = RGTexUsage::ColorAttach | RGTexUsage::Sampled,
      .extent = {RenderExtentKind::Framebuffer, 0, 0},
  };
  const RenderTextureDesc outDesc{
      .format = RGFormat::RGBA8,
      .usage = RGTexUsage::ColorAttach | RGTexUsage::Sampled,
      .extent = {RenderExtentKind::Framebuffer, 0, 0},
  };

  m_graph.declareTexture("Depth.Pre", depthDesc);
  m_graph.declareTexture("HDR.Color", hdrDesc);
  m_graph.declareTexture("ID.Submesh", idDesc);
  m_graph.declareTexture("Post.In", postInDesc);
  m_graph.declareTexture("LDR.Color", ldrDesc);
  m_graph.declareTexture("OUT.Color", outDesc);
  m_passDepthPre.setup(m_graph, ctx, registry, engine, editorVisible);
  m_passForward.setup(m_graph, ctx, registry, engine, editorVisible);
  m_passTonemap.setup(m_graph, ctx, registry, engine, editorVisible);
  m_passPost.setup(m_graph, ctx, registry, engine, editorVisible);
  m_passSelection.setup(m_graph, ctx, registry, engine, editorVisible);
  m_passPresent.setup(m_graph, ctx, registry, engine, editorVisible);

  m_graph.execute(ctx, m_rgRes);

  auto &bb = m_graph.blackboard();
  m_out.hdr = bb.textureHandle(bb.getTexture("HDR.Color"));
  m_out.id = bb.textureHandle(bb.getTexture("ID.Submesh"));
  m_out.depth = bb.textureHandle(bb.getTexture("Depth.Pre"));
  m_out.ldr = bb.textureHandle(bb.getTexture("LDR.Color"));
  m_out.outlined = bb.textureHandle(bb.getTexture("OUT.Color"));

  const auto &finalT = m_rgRes.tex(m_out.outlined);
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
