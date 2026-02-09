#include "MaterialSystem.h"

#include "core/Assert.h"
#include "material/MaterialHandle.h"
#include "render/gl/GLResources.h"
#include "render/material/GpuMaterial.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <vector>

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

  rebuildGpuForSlot(idx);
  ensureGraphFromMaterial(MaterialHandle{idx, s.gen}, true);

  m_anyDirty = true;
  m_anyGraphDirty = true;

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

  for (uint32_t i = 0; i < m_slots.size(); ++i) {
    if (!m_slots[i].alive)
      continue;
    if (m_slots[i].dirty)
      rebuildGpuForSlot(i);
  }

  std::vector<GpuMaterialPacked> packed;
  packed.reserve(m_slots.size());
  for (const Slot &s : m_slots)
    packed.push_back(s.alive ? s.gpu : GpuMaterialPacked{});

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

  if (m_ssbo)
    glNamedBufferData(m_ssbo, 0, nullptr, GL_DYNAMIC_DRAW);
  if (m_graphHeadersSSBO)
    glNamedBufferData(m_graphHeadersSSBO, 0, nullptr, GL_DYNAMIC_DRAW);
  if (m_graphNodesSSBO)
    glNamedBufferData(m_graphNodesSSBO, 0, nullptr, GL_DYNAMIC_DRAW);
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

} // namespace Nyx
