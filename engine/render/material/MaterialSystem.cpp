#include "MaterialSystem.h"
#include "core/Assert.h"
#include "core/Log.h"
#include "material/MaterialHandle.h"
#include "render/gl/GLResources.h"
#include "render/material/MaterialGraphCompiler.h"
#include "render/material/MaterialGraphVM.h"
#include "render/material/GpuMaterial.h"
#include "scene/material/MaterialData.h"
#include "scene/material/MaterialTypes.h"
#include <bit>
#include <cstddef>
#include <cstdint>
#include <glad/glad.h>

namespace Nyx {

static uint32_t idxFromHandle(MaterialHandle h) { return h.slot; }
static uint32_t genFromHandle(MaterialHandle h) { return h.gen; }

void MaterialSystem::initGL(GLResources &gl) {
  m_gl = &gl;
  m_tex.init(gl);

  if (!m_ssbo)
    glCreateBuffers(1, &m_ssbo);
  if (!m_graphHeadersSSBO)
    glCreateBuffers(1, &m_graphHeadersSSBO);
  if (!m_graphNodesSSBO)
    glCreateBuffers(1, &m_graphNodesSSBO);

  m_slots.clear();
  m_free.clear();
  m_anyDirty = true;
  m_anyGraphDirty = true;
  ++m_changeSerial;
}

void MaterialSystem::shutdownGL() {
  m_tex.shutdown();

  if (m_ssbo) {
    glDeleteBuffers(1, &m_ssbo);
    m_ssbo = 0;
  }
  if (m_graphHeadersSSBO) {
    glDeleteBuffers(1, &m_graphHeadersSSBO);
    m_graphHeadersSSBO = 0;
  }
  if (m_graphNodesSSBO) {
    glDeleteBuffers(1, &m_graphNodesSSBO);
    m_graphNodesSSBO = 0;
  }

  m_slots.clear();
  m_free.clear();
  m_gl = nullptr;
  m_anyDirty = true;
  m_anyGraphDirty = true;
  ++m_changeSerial;
}

MaterialHandle MaterialSystem::create(const MaterialData &data) {
  uint32_t idx = 0;
  if (!m_free.empty()) {
    idx = m_free.back();
    m_free.pop_back();
  } else {
    idx = static_cast<uint32_t>(m_slots.size());
    m_slots.push_back(Slot{});
  }

  Slot &s = m_slots[idx];
  s.alive = true;
  s.cpu = data;
  s.dirty = true;
  s.graphDirty = true;
  s.graphErr.clear();

  ++m_changeSerial;

  // Build GPU immediately (so gpuIndex valid)
  rebuildGpuForSlot(idx);

  ensureGraphFromMaterial(MaterialHandle{idx, s.gen}, true);

  m_anyDirty = true;
  m_anyGraphDirty = true;

  // build handle
  MaterialHandle h{};
  h.slot = idx;
  h.gen = s.gen;
  return h;
}

void MaterialSystem::destroy(MaterialHandle h) {
  if (!isAlive(h))
    return;
  const uint32_t idx = idxFromHandle(h);
  Slot &s = m_slots[idx];
  s.alive = false;
  ++s.gen;
  s.cpu = MaterialData{};
  s.graph = MaterialGraph{};
  s.compiled = CompiledMaterialGraph{};
  s.graphErr.clear();
  s.dirty = true;
  s.graphDirty = true;
  m_free.push_back(idx);
  m_anyDirty = true;
  m_anyGraphDirty = true;
  ++m_changeSerial;
}

bool MaterialSystem::isAlive(MaterialHandle h) const {
  const uint32_t idx = idxFromHandle(h);
  if (idx >= m_slots.size())
    return false;
  const Slot &s = m_slots[idx];
  return s.alive && s.gen == genFromHandle(h);
}

MaterialData &MaterialSystem::cpu(MaterialHandle h) {
  NYX_ASSERT(isAlive(h), "MaterialSystem::cpu invalid handle");
  return m_slots[idxFromHandle(h)].cpu;
}

const MaterialData &MaterialSystem::cpu(MaterialHandle h) const {
  NYX_ASSERT(isAlive(h), "MaterialSystem::cpu invalid handle");
  return m_slots[idxFromHandle(h)].cpu;
}

const GpuMaterialPacked &MaterialSystem::gpu(MaterialHandle h) const {
  NYX_ASSERT(isAlive(h), "MaterialSystem::gpu invalid handle");
  return m_slots[idxFromHandle(h)].gpu;
}

MaterialGraph &MaterialSystem::graph(MaterialHandle h) {
  NYX_ASSERT(isAlive(h), "MaterialSystem::graph invalid handle");
  return m_slots[idxFromHandle(h)].graph;
}

const MaterialGraph &MaterialSystem::graph(MaterialHandle h) const {
  NYX_ASSERT(isAlive(h), "MaterialSystem::graph invalid handle");
  return m_slots[idxFromHandle(h)].graph;
}

const std::string &MaterialSystem::graphError(MaterialHandle h) const {
  static const std::string kEmpty;
  if (!isAlive(h))
    return kEmpty;
  return m_slots[idxFromHandle(h)].graphErr;
}

MatAlphaMode MaterialSystem::alphaMode(MaterialHandle h) const {
  if (!isAlive(h))
    return MatAlphaMode::Opaque;
  const Slot &s = m_slots[idxFromHandle(h)];
  if (s.compiled.header.nodeCount == 0)
    return MatAlphaMode::Opaque;
  return static_cast<MatAlphaMode>(s.compiled.header.alphaMode);
}

uint32_t MaterialSystem::gpuIndex(MaterialHandle h) const {
  NYX_ASSERT(isAlive(h), "MaterialSystem::gpuIndex invalid handle");
  return idxFromHandle(h);
}

MaterialHandle MaterialSystem::handleBySlot(uint32_t slot) const {
  if (slot >= m_slots.size())
    return InvalidMaterial;
  const Slot &s = m_slots[slot];
  if (!s.alive)
    return InvalidMaterial;
  return MaterialHandle{slot, s.gen};
}

static void buildGraphFromMaterial(MaterialSystem &sys, MaterialHandle h,
                                   MaterialGraph &g,
                                   const MaterialData &m) {
  (void)h;
  g.nodes.clear();
  g.links.clear();
  g.nextNodeId = 1;
  g.nextLinkId = 1;
  g.alphaMode = m.alphaMode;
  g.alphaCutoff = m.alphaCutoff;

  auto addNode = [&](MatNodeType type, const char *label, glm::vec2 pos) {
    MatNode n{};
    n.id = g.nextNodeId++;
    n.type = type;
    n.label = label ? label : "";
    n.pos = pos;
    n.posSet = true;
    g.nodes.push_back(n);
    return n.id;
  };

  auto addLink = [&](MatNodeID from, uint32_t fromSlot, MatNodeID to,
                     uint32_t toSlot) {
    MatLink l{};
    l.id = g.nextLinkId++;
    l.from = {from, fromSlot};
    l.to = {to, toSlot};
    g.links.push_back(l);
  };

  const MatNodeID out =
      addNode(MatNodeType::SurfaceOutput, "Surface Output", {680.0f, 200.0f});

  const MatNodeID uv = addNode(MatNodeType::UV0, "UV0", {60.0f, 40.0f});
  const MatNodeID normalWS =
      addNode(MatNodeType::NormalWS, "NormalWS", {60.0f, 260.0f});

  // BaseColor
  MatNodeID baseConst =
      addNode(MatNodeType::ConstVec4, "BaseColor (White)", {240.0f, 40.0f});
  g.nodes.back().f =
      glm::vec4(m.baseColorFactor.x, m.baseColorFactor.y,
                m.baseColorFactor.z, m.baseColorFactor.w);

  MatNodeID baseOut = baseConst;
  const std::string &basePath =
      m.texPath[static_cast<size_t>(MaterialTexSlot::BaseColor)];
  if (!basePath.empty()) {
    MatNodeID tex = addNode(MatNodeType::Texture2D, "Texture2D", {240.0f, 120.0f});
    uint32_t idx = sys.textures().getOrCreate2D(basePath, true);
    g.nodes.back().u = glm::uvec4(idx, 1u, 0u, 0u);
    g.nodes.back().path = basePath;
    addLink(uv, 0, tex, 0);

    MatNodeID mul = addNode(MatNodeType::Mul, "Mul", {460.0f, 80.0f});
    addLink(tex, 0, mul, 0);
    addLink(baseConst, 0, mul, 1);
    baseOut = mul;
  }
  addLink(baseOut, 0, out, 0);

  // Metallic
  MatNodeID metal =
      addNode(MatNodeType::ConstFloat, "Metallic (Black)", {240.0f, 220.0f});
  g.nodes.back().f = glm::vec4(m.metallic, 0, 0, 0);
  const std::string &metPath =
      m.texPath[static_cast<size_t>(MaterialTexSlot::Metallic)];
  if (!metPath.empty()) {
    MatNodeID tex = addNode(MatNodeType::Texture2D, "Texture2D", {240.0f, 260.0f});
    uint32_t idx = sys.textures().getOrCreate2D(metPath, false);
    g.nodes.back().u = glm::uvec4(idx, 0u, 0u, 0u);
    g.nodes.back().path = metPath;
    addLink(uv, 0, tex, 0);
    metal = tex;
  }
  addLink(metal, 0, out, 1);

  // Roughness
  MatNodeID rough =
      addNode(MatNodeType::ConstFloat, "Roughness (Gray)", {240.0f, 320.0f});
  g.nodes.back().f = glm::vec4(m.roughness, 0, 0, 0);
  const std::string &rghPath =
      m.texPath[static_cast<size_t>(MaterialTexSlot::Roughness)];
  if (!rghPath.empty()) {
    MatNodeID tex = addNode(MatNodeType::Texture2D, "Texture2D", {240.0f, 360.0f});
    uint32_t idx = sys.textures().getOrCreate2D(rghPath, false);
    g.nodes.back().u = glm::uvec4(idx, 0u, 0u, 0u);
    g.nodes.back().path = rghPath;
    addLink(uv, 0, tex, 0);
    rough = tex;
  }
  addLink(rough, 0, out, 2);

  // Normal
  const std::string &nrmPath =
      m.texPath[static_cast<size_t>(MaterialTexSlot::Normal)];
  if (!nrmPath.empty()) {
    MatNodeID nrm = addNode(MatNodeType::NormalMap, "Normal Map", {240.0f, 420.0f});
    uint32_t idx = sys.textures().getOrCreate2D(nrmPath, false);
    g.nodes.back().u = glm::uvec4(idx, 0u, 0u, 0u);
    g.nodes.back().path = nrmPath;
    addLink(uv, 0, nrm, 0);
    addLink(nrm, 0, out, 3);
  } else {
    addLink(normalWS, 0, out, 3);
  }

  // AO
  MatNodeID ao =
      addNode(MatNodeType::ConstFloat, "AO (White)", {240.0f, 500.0f});
  g.nodes.back().f = glm::vec4(m.ao, 0, 0, 0);
  const std::string &aoPath =
      m.texPath[static_cast<size_t>(MaterialTexSlot::AO)];
  if (!aoPath.empty()) {
    MatNodeID tex = addNode(MatNodeType::Texture2D, "Texture2D", {240.0f, 540.0f});
    uint32_t idx = sys.textures().getOrCreate2D(aoPath, false);
    g.nodes.back().u = glm::uvec4(idx, 0u, 0u, 0u);
    g.nodes.back().path = aoPath;
    addLink(uv, 0, tex, 0);
    ao = tex;
  }
  addLink(ao, 0, out, 4);

  // Emissive
  MatNodeID emi =
      addNode(MatNodeType::ConstVec3, "Emissive (Black)", {240.0f, 600.0f});
  g.nodes.back().f =
      glm::vec4(m.emissiveFactor.x, m.emissiveFactor.y, m.emissiveFactor.z, 1);
  const std::string &emiPath =
      m.texPath[static_cast<size_t>(MaterialTexSlot::Emissive)];
  MatNodeID emiOut = emi;
  if (!emiPath.empty()) {
    MatNodeID tex = addNode(MatNodeType::Texture2D, "Texture2D", {240.0f, 660.0f});
    uint32_t idx = sys.textures().getOrCreate2D(emiPath, true);
    g.nodes.back().u = glm::uvec4(idx, 1u, 0u, 0u);
    g.nodes.back().path = emiPath;
    addLink(uv, 0, tex, 0);

    MatNodeID mul = addNode(MatNodeType::Mul, "Mul", {460.0f, 630.0f});
    addLink(tex, 0, mul, 0);
    addLink(emi, 0, mul, 1);
    emiOut = mul;
  }
  addLink(emiOut, 0, out, 5);

  // Alpha
  MatNodeID alpha =
      addNode(MatNodeType::ConstFloat, "Alpha (White)", {240.0f, 740.0f});
  g.nodes.back().f = glm::vec4(m.baseColorFactor.w, 0, 0, 0);
  addLink(alpha, 0, out, 6);
}

void MaterialSystem::ensureGraphFromMaterial(MaterialHandle h, bool force) {
  if (!isAlive(h))
    return;

  Slot &s = m_slots[idxFromHandle(h)];
  MaterialGraph &g = s.graph;
  if (!force && !g.nodes.empty() && g.findSurfaceOutput() != 0)
    return;

  buildGraphFromMaterial(*this, h, g, s.cpu);

  s.graphErr.clear();
  markGraphDirty(h);
}

void MaterialSystem::syncGraphFromMaterial(MaterialHandle h, bool force) {
  ensureGraphFromMaterial(h, force);
}

void MaterialSystem::syncMaterialFromGraph(MaterialHandle h) {
  if (!isAlive(h))
    return;
  Slot &s = m_slots[idxFromHandle(h)];
  MaterialGraph &g = s.graph;
  MatNodeID out = g.findSurfaceOutput();
  if (out == 0)
    return;

  auto findInput = [&](uint32_t slot) -> MatNodeID {
    for (const auto &l : g.links) {
      if (l.to.node == out && l.to.slot == slot)
        return l.from.node;
    }
    return 0;
  };

  auto findTexturePath = [&](MatNodeID nodeId) -> std::string {
    if (nodeId == 0)
      return {};
    std::vector<MatNodeID> stack;
    std::unordered_map<MatNodeID, bool> visited;
    stack.push_back(nodeId);
    while (!stack.empty()) {
      MatNodeID cur = stack.back();
      stack.pop_back();
      if (visited[cur])
        continue;
      visited[cur] = true;

      for (const auto &n : g.nodes) {
        if (n.id != cur)
          continue;
        if (n.type == MatNodeType::Texture2D ||
            n.type == MatNodeType::NormalMap) {
          if (!n.path.empty())
            return n.path;
        }
        break;
      }

      for (const auto &l : g.links) {
        if (l.to.node == cur) {
          stack.push_back(l.from.node);
        }
      }
    }
    return {};
  };

  s.cpu.texPath[(size_t)MaterialTexSlot::BaseColor] =
      findTexturePath(findInput(0));
  s.cpu.texPath[(size_t)MaterialTexSlot::Metallic] =
      findTexturePath(findInput(1));
  s.cpu.texPath[(size_t)MaterialTexSlot::Roughness] =
      findTexturePath(findInput(2));
  s.cpu.texPath[(size_t)MaterialTexSlot::Normal] =
      findTexturePath(findInput(3));
  s.cpu.texPath[(size_t)MaterialTexSlot::AO] =
      findTexturePath(findInput(4));
  s.cpu.texPath[(size_t)MaterialTexSlot::Emissive] =
      findTexturePath(findInput(5));

  s.cpu.alphaMode = g.alphaMode;
  s.cpu.alphaCutoff = g.alphaCutoff;

  markDirty(h);
}

// Graph sync helpers removed in rewrite.

// syncMaterialFromGraph removed in rewrite.

void MaterialSystem::markDirty(MaterialHandle h) {
  if (!isAlive(h))
    return;
  m_slots[idxFromHandle(h)].dirty = true;
  m_anyDirty = true;
  ++m_changeSerial;
}

void MaterialSystem::markGraphDirty(MaterialHandle h) {
  if (!isAlive(h))
    return;
  m_slots[idxFromHandle(h)].graphDirty = true;
  m_anyGraphDirty = true;
  ++m_changeSerial;
}

void MaterialSystem::rebuildGpuForSlot(uint32_t idx) {
  Slot &s = m_slots[idx];
  const MaterialData &m = s.cpu;

  GpuMaterialPacked g{};
  g.baseColorFactor = m.baseColorFactor;
  g.emissiveFactor = glm::vec4(m.emissiveFactor, 0.0f);
  g.mrAoFlags = glm::vec4(m.metallic, m.roughness, m.ao, 0.0f);

  uint32_t flags = Mat_None;

  auto setTex = [&](MaterialTexSlot slot, bool srgb) -> uint32_t {
    const std::string &p = m.texPath[static_cast<size_t>(slot)];
    if (p.empty())
      return kInvalidTexIndex;
    uint32_t ti = m_tex.getOrCreate2D(p, srgb);
    return (ti == TextureTable::Invalid) ? kInvalidTexIndex : ti;
  };

  const uint32_t tBase = setTex(MaterialTexSlot::BaseColor, true);
  const uint32_t tEmis = setTex(MaterialTexSlot::Emissive, true);
  const uint32_t tNorm = setTex(MaterialTexSlot::Normal, false);
  const uint32_t tMet = setTex(MaterialTexSlot::Metallic, false);
  const uint32_t tRgh = setTex(MaterialTexSlot::Roughness, false);
  const uint32_t tOcc = setTex(MaterialTexSlot::AO, false);

  if (tBase != kInvalidTexIndex)
    flags |= Mat_HasBaseColor;
  if (tEmis != kInvalidTexIndex)
    flags |= Mat_HasEmissive;
  if (tNorm != kInvalidTexIndex)
    flags |= Mat_HasNormal;
  if (tMet != kInvalidTexIndex)
    flags |= Mat_HasMetallic;
  if (tRgh != kInvalidTexIndex)
    flags |= Mat_HasRoughness;
  if (tOcc != kInvalidTexIndex)
    flags |= Mat_HasAO;
  if (m.tangentSpaceNormal)
    flags |= Mat_TangentSpaceNormal;

  g.mrAoFlags.w = std::bit_cast<float>(flags);
  g.tex0123 = glm::uvec4(tBase, tEmis, tNorm, tMet);
  g.tex4_pad = glm::uvec4(tRgh, tOcc, 0u, 0u);
  g.uvScaleOffset = glm::vec4(m.uvScale, m.uvOffset);
  g.extra =
      glm::vec4(m.alphaCutoff, static_cast<float>(m.alphaMode), 0.0f, 0.0f);

  s.gpu = g;
  s.dirty = false;
}

void MaterialSystem::uploadIfDirty() {
  updateGraphTablesIfDirty();
  if (!m_anyDirty)
    return;
  if (!m_ssbo)
    return;

  // Rebuild all dirty GPU entries
  for (uint32_t i = 0; i < m_slots.size(); ++i) {
    if (!m_slots[i].alive)
      continue;
    if (m_slots[i].dirty)
      rebuildGpuForSlot(i);
  }

  // Upload full buffer.
  std::vector<GpuMaterialPacked> packed;
  packed.reserve(m_slots.size());
  for (const Slot &s : m_slots) {
    packed.push_back(s.alive ? s.gpu : GpuMaterialPacked{});
  }

  glNamedBufferData(m_ssbo, packed.size() * sizeof(GpuMaterialPacked),
                    packed.data(), GL_DYNAMIC_DRAW);

  m_anyDirty = false;
}

void MaterialSystem::reset() {
  if (!m_gl)
    return;

  m_slots.clear();
  m_free.clear();
  m_anyDirty = true;
  m_anyGraphDirty = true;

  m_tex.shutdown();
  m_tex.init(*m_gl);

  if (m_ssbo) {
    glNamedBufferData(m_ssbo, 0, nullptr, GL_DYNAMIC_DRAW);
  }
  if (m_graphHeadersSSBO) {
    glNamedBufferData(m_graphHeadersSSBO, 0, nullptr, GL_DYNAMIC_DRAW);
  }
  if (m_graphNodesSSBO) {
    glNamedBufferData(m_graphNodesSSBO, 0, nullptr, GL_DYNAMIC_DRAW);
  }
}

void MaterialSystem::snapshot(MaterialSystemSnapshot &out) const {
  out.slots.clear();
  out.slots.reserve(m_slots.size());
  for (const Slot &s : m_slots) {
    MaterialSnapshot ms{};
    ms.gen = s.gen;
    ms.alive = s.alive;
    ms.cpu = s.cpu;
    ms.graph = s.graph;
    out.slots.push_back(std::move(ms));
  }
  out.free = m_free;
  out.changeSerial = m_changeSerial;
}

void MaterialSystem::restore(const MaterialSystemSnapshot &snap) {
  if (!m_gl)
    return;
  m_slots.clear();
  m_slots.resize(snap.slots.size());
  for (size_t i = 0; i < snap.slots.size(); ++i) {
    const MaterialSnapshot &ms = snap.slots[i];
    Slot &s = m_slots[i];
    s.gen = ms.gen;
    s.alive = ms.alive;
    s.cpu = ms.cpu;
    s.graph = ms.graph;
    s.compiled = {};
    s.graphErr.clear();
    s.dirty = true;
    s.graphDirty = true;
  }
  m_free = snap.free;
  m_changeSerial = snap.changeSerial;
  m_anyDirty = true;
  m_anyGraphDirty = true;
}

void MaterialSystem::updateGraphTablesIfDirty() {
  if (!m_anyGraphDirty)
    return;
  if (!m_graphHeadersSSBO || !m_graphNodesSSBO)
    return;

  MaterialGraphCompiler compiler{};

  // Compile dirty graphs
  for (uint32_t i = 0; i < m_slots.size(); ++i) {
    Slot &s = m_slots[i];
    if (!s.alive)
      continue;
    if (!s.graphDirty)
      continue;

    s.graphErr.clear();
    s.compiled = {};

    if (!s.graph.nodes.empty()) {
      MatCompilerError err{};
      if (!compiler.compile(s.graph, s.compiled, &err)) {
        s.graphErr = err.msg.empty() ? "Material graph compile failed" : err.msg;
        s.compiled = {};
      }
    }

    s.graphDirty = false;
  }

  // Build packed header + node arrays
  std::vector<GpuMatGraphHeader> headers;
  std::vector<GpuMatNode> nodes;
  headers.resize(m_slots.size());

  for (uint32_t i = 0; i < m_slots.size(); ++i) {
    const Slot &s = m_slots[i];
    if (!s.alive || s.compiled.nodes.empty()) {
      headers[i] = GpuMatGraphHeader{};
      headers[i].nodeOffset = 0;
      headers[i].nodeCount = 0;
      headers[i].alphaMode = static_cast<uint32_t>(MatAlphaMode::Opaque);
      headers[i].alphaCutoff = 0.5f;
      continue;
    }

    GpuMatGraphHeader h = s.compiled.header;
    h.nodeOffset = (uint32_t)nodes.size();
    h.nodeCount = (uint32_t)s.compiled.nodes.size();
    nodes.insert(nodes.end(), s.compiled.nodes.begin(), s.compiled.nodes.end());
    headers[i] = h;
  }

  glNamedBufferData(m_graphHeadersSSBO,
                    headers.size() * sizeof(GpuMatGraphHeader),
                    headers.empty() ? nullptr : headers.data(),
                    GL_DYNAMIC_DRAW);
  glNamedBufferData(m_graphNodesSSBO,
                    nodes.size() * sizeof(GpuMatNode),
                    nodes.empty() ? nullptr : nodes.data(), GL_DYNAMIC_DRAW);

  m_anyGraphDirty = false;
}

} // namespace Nyx
