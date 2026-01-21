#include "MaterialSystem.h"
#include "../core/Assert.h"
#include "../core/Log.h"

#include <glad/glad.h>

namespace Nyx {

static GpuMaterialPacked packMat(const MaterialData &m) {
  GpuMaterialPacked g{};
  g.baseColor = m.baseColor;
  g.mrAoCut = glm::vec4(m.metallic, m.roughness, m.ao, m.alphaCutoff);
  g.flags = glm::vec4(m.alphaMasked ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
  return g;
}

void MaterialSystem::ensureSlot(uint32_t s) {
  if (s < m_slots.size())
    return;
  m_slots.resize((size_t)s + 1);
}

MaterialData &MaterialSystem::edit(MaterialHandle h) {
  NYX_ASSERT(isAlive(h), "MaterialSystem::edit: invalid handle");
  Slot &sl = m_slots[h.slot];
  sl.dirty = true;
  m_gpuDirty = true;
  return sl.data;
}

MaterialHandle MaterialSystem::create(const MaterialData &d) {
  uint32_t slot = 0;
  if (!m_free.empty()) {
    slot = m_free.back();
    m_free.pop_back();
  } else {
    slot = (uint32_t)m_slots.size();
    m_slots.push_back(Slot{});
  }

  ensureSlot(slot);

  Slot &sl = m_slots[slot];
  sl.alive = true;
  sl.data = d;
  sl.dirty = true;

  // Assign a GPU index equal to slot (stable). Keep it simple.
  sl.gpuIdx = slot;

  // generation increments when reusing slot (avoid 0 gen)
  if (sl.gen == 0)
    sl.gen = 1;

  m_gpuDirty = true;
  return MaterialHandle{slot, sl.gen};
}

bool MaterialSystem::isAlive(MaterialHandle h) const {
  if (h == InvalidMaterial)
    return false;
  if (h.slot >= m_slots.size())
    return false;
  const Slot &sl = m_slots[h.slot];
  return sl.alive && sl.gen == h.gen;
}

MaterialData &MaterialSystem::get(MaterialHandle h) {
  NYX_ASSERT(isAlive(h), "MaterialSystem::get: invalid handle");
  return m_slots[h.slot].data;
}

const MaterialData &MaterialSystem::get(MaterialHandle h) const {
  NYX_ASSERT(isAlive(h), "MaterialSystem::get: invalid handle");
  return m_slots[h.slot].data;
}

uint32_t MaterialSystem::gpuIndex(MaterialHandle h) const {
  NYX_ASSERT(isAlive(h), "MaterialSystem::gpuIndex: invalid handle");
  return m_slots[h.slot].gpuIdx;
}

void MaterialSystem::markDirty(MaterialHandle h) {
  if (!isAlive(h))
    return;
  m_slots[h.slot].dirty = true;
  m_gpuDirty = true;
}

void MaterialSystem::initGL() {
  if (m_ssbo != 0)
    return;
  glCreateBuffers(1, &m_ssbo);

  // Start with at least 1 (default material slot 0 reserved implicitly)
  if (m_gpuTable.empty()) {
    m_gpuTable.resize(1);
    m_gpuTable[0] = packMat(MaterialData{});
  }

  glNamedBufferData(m_ssbo,
                    (GLsizeiptr)(m_gpuTable.size() * sizeof(GpuMaterialPacked)),
                    m_gpuTable.data(), GL_DYNAMIC_DRAW);
}

void MaterialSystem::shutdownGL() {
  if (m_ssbo != 0) {
    glDeleteBuffers(1, &m_ssbo);
    m_ssbo = 0;
  }
}

void MaterialSystem::rebuildGpuTableIfNeeded() {
  // Ensure gpu table has size = max(slot.gpuIdx)+1. Here gpuIdx == slot.
  const uint32_t need = (uint32_t)std::max<size_t>(1, m_slots.size());
  if (m_gpuTable.size() < (size_t)need) {
    m_gpuTable.resize((size_t)need);
    // fill new entries as default
    for (size_t i = 0; i < m_gpuTable.size(); ++i) {
      // keep existing; defaults are ok
    }
    m_gpuDirty = true;
  }

  // Update dirty slots
  for (uint32_t i = 0; i < (uint32_t)m_slots.size(); ++i) {
    Slot &sl = m_slots[i];
    if (!sl.alive)
      continue;
    if (!sl.dirty)
      continue;
    m_gpuTable[sl.gpuIdx] = packMat(sl.data);
    sl.dirty = false;
  }
}

void MaterialSystem::uploadIfDirty() {
  if (m_ssbo == 0)
    initGL();

  if (!m_gpuDirty)
    return;
  m_gpuDirty = false;

  rebuildGpuTableIfNeeded();

  glNamedBufferData(m_ssbo,
                    (GLsizeiptr)(m_gpuTable.size() * sizeof(GpuMaterialPacked)),
                    m_gpuTable.data(), GL_DYNAMIC_DRAW);
}

} // namespace Nyx
