#include "MaterialSystem.h"

#include "core/Assert.h"
#include "material/MaterialHandle.h"
#include "render/material/MaterialGraphCompiler.h"
#include "render/material/MaterialGraphVM.h"

#include <glad/glad.h>
#include <unordered_map>
#include <vector>

namespace Nyx {

static uint32_t idxFromHandle(MaterialHandle h) { return h.slot; }

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
        if (l.to.node == cur)
          stack.push_back(l.from.node);
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

void MaterialSystem::updateGraphTablesIfDirty() {
  if (!m_anyGraphDirty)
    return;
  if (!m_graphHeadersSSBO || !m_graphNodesSSBO)
    return;

  MaterialGraphCompiler compiler{};

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
