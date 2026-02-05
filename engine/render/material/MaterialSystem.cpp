#include "MaterialSystem.h"
#include "core/Assert.h"
#include "core/Log.h"
#include "material/MaterialHandle.h"
#include "render/gl/GLResources.h"
#include "render/material/GpuMaterial.h"
#include "scene/material/MaterialData.h"
#include "scene/material/MaterialTypes.h"
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

  m_slots.clear();
  m_free.clear();
  m_anyDirty = true;
}

void MaterialSystem::shutdownGL() {
  m_tex.shutdown();

  if (m_ssbo) {
    glDeleteBuffers(1, &m_ssbo);
    m_ssbo = 0;
  }

  m_slots.clear();
  m_free.clear();
  m_gl = nullptr;
  m_anyDirty = true;
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

  // Build GPU immediately (so gpuIndex valid)
  rebuildGpuForSlot(idx);

  m_anyDirty = true;

  // build handle
  MaterialHandle h{};
  h.slot = idx;
  h.gen = s.gen;
  return h;
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

uint32_t MaterialSystem::gpuIndex(MaterialHandle h) const {
  NYX_ASSERT(isAlive(h), "MaterialSystem::gpuIndex invalid handle");
  return idxFromHandle(h);
}

void MaterialSystem::markDirty(MaterialHandle h) {
  if (!isAlive(h))
    return;
  m_slots[idxFromHandle(h)].dirty = true;
  m_anyDirty = true;
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

  g.mrAoFlags.w = static_cast<float>(flags);
  g.tex0123 = glm::uvec4(tBase, tEmis, tNorm, tMet);
  g.tex4_pad = glm::uvec4(tRgh, tOcc, 0u, 0u);
  g.uvScaleOffset = glm::vec4(m.uvScale, m.uvOffset);

  s.gpu = g;
  s.dirty = false;
}

void MaterialSystem::uploadIfDirty() {
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

  m_tex.shutdown();
  m_tex.init(*m_gl);

  if (m_ssbo) {
    glNamedBufferData(m_ssbo, 0, nullptr, GL_DYNAMIC_DRAW);
  }
}

} // namespace Nyx
