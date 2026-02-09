#include "SequencerPanel.h"

#include "animation/AnimationSystem.h"
#include "scene/World.h"

#include <algorithm>
#include <cstring>
#include <cstdio>

namespace Nyx {

void SequencerPanel::setHiddenExclusions(const std::vector<EntityID> &ents) {
  m_hiddenExclude.clear();
  m_hiddenExclude.reserve(ents.size());
  for (EntityID e : ents) {
    if (e != InvalidEntity)
      m_hiddenExclude.insert(e);
  }
  markLayoutDirty();
}

void SequencerPanel::setTrackExclusions(const std::vector<EntityID> &ents) {
  m_trackExclude.clear();
  m_trackExclude.reserve(ents.size());
  for (EntityID e : ents) {
    if (e != InvalidEntity)
      m_trackExclude.insert(e);
  }
  markLayoutDirty();
}

uint64_t SequencerPanel::computeLayoutSignature() const {
  auto mix = [](uint64_t h, uint64_t v) -> uint64_t {
    h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
    return h;
  };

  uint64_t h = 1469598103934665603ull;
  h = mix(h, (uint64_t)(uintptr_t)m_world);
  h = mix(h, (uint64_t)(uintptr_t)m_clip);
  h = mix(h, (uint64_t)(uintptr_t)m_anim);
  h = mix(h, (uint64_t)m_sortMode);
  h = mix(h, (uint64_t)m_showGraphPanel);
  h = mix(h, (uint64_t)m_trackExclude.size());
  h = mix(h, (uint64_t)m_hiddenExclude.size());
  h = mix(h, (uint64_t)m_isolated.size());
  h = mix(h, (uint64_t)m_expandState.size());
  h = mix(h, (uint64_t)m_stopwatchState.size());
  for (const char *p = m_searchBuf; p && *p; ++p)
    h = mix(h, (uint64_t)(uint8_t)*p);

  if (m_world) {
    h = mix(h, (uint64_t)m_world->alive().size());
    const size_t n = m_world->alive().size();
    if (n > 0) {
      h = mix(h, (uint64_t)m_world->alive().front().index);
      h = mix(h, (uint64_t)m_world->alive().back().index);
    }
  }
  if (m_clip) {
    h = mix(h, (uint64_t)m_clip->tracks.size());
    h = mix(h, (uint64_t)m_clip->entityRanges.size());
    h = mix(h, (uint64_t)m_clip->lastFrame);
    h = mix(h, (uint64_t)m_clip->nextBlockId);
  }
  return h;
}

void SequencerPanel::rebuildLayoutCacheIfNeeded() {
  if (!m_world || !m_clip)
    return;
  const uint64_t sig = computeLayoutSignature();
  if (!m_layoutDirty && sig == m_layoutSignature)
    return;

  ensureTracksForWorld();
  buildRowEntities();
  buildRows();
  applyIsolation();
  updateHiddenEntities();

  m_layoutSignature = sig;
  m_layoutDirty = false;
}

void SequencerPanel::capturePersistState(SequencerPersistState &out) const {
  out = SequencerPersistState{};
  out.valid = true;
  out.pixelsPerFrame = m_pixelsPerFrame;
  out.labelGutter = m_labelGutter;
  out.viewFirstFrame = m_viewFirstFrame;
  out.autoUpdateLastFrame = m_autoUpdateLastFrame;
  out.sortMode = (int)m_sortMode;
  out.showGraphPanel = m_showGraphPanel;
  out.search = std::string(m_searchBuf);

  out.expand.reserve(m_expandState.size());
  for (const auto &it : m_expandState) {
    const uint64_t packed = it.first;
    const uint8_t prop = (uint8_t)(packed & 0xFFu);
    const uint8_t rowType = (uint8_t)((packed >> 8) & 0xFFu);
    const uint16_t generation = (uint16_t)((packed >> 16) & 0xFFFFu);
    const uint32_t index = (uint32_t)(packed >> 32);
    EntityID e{};
    e.index = index;
    e.generation = generation;
    if (!m_world || !m_world->isAlive(e))
      continue;
    SequencerPersistToggle t{};
    t.entity = m_world->uuid(e);
    if (!t.entity)
      continue;
    t.rowType = rowType;
    t.prop = prop;
    t.value = it.second;
    out.expand.push_back(t);
  }

  out.stopwatch.reserve(m_stopwatchState.size());
  for (const auto &it : m_stopwatchState) {
    const uint64_t packed = it.first;
    const uint8_t prop = (uint8_t)(packed & 0xFFu);
    const uint8_t rowType = (uint8_t)((packed >> 8) & 0xFFu);
    const uint16_t generation = (uint16_t)((packed >> 16) & 0xFFFFu);
    const uint32_t index = (uint32_t)(packed >> 32);
    EntityID e{};
    e.index = index;
    e.generation = generation;
    if (!m_world || !m_world->isAlive(e))
      continue;
    SequencerPersistToggle t{};
    t.entity = m_world->uuid(e);
    if (!t.entity)
      continue;
    t.rowType = rowType;
    t.prop = prop;
    t.value = it.second;
    out.stopwatch.push_back(t);
  }

  out.selectedLayers.reserve(m_selectedLayerBlocks.size());
  for (EntityID e : m_selectedLayerBlocks) {
    if (!m_world || !m_world->isAlive(e))
      continue;
    const EntityUUID u = m_world->uuid(e);
    if (u)
      out.selectedLayers.push_back(u);
  }
}

void SequencerPanel::applyPersistState(const SequencerPersistState &in) {
  if (!in.valid)
    return;
  m_pixelsPerFrame = std::max(1.0f, in.pixelsPerFrame);
  m_labelGutter = std::clamp(in.labelGutter, m_labelGutterMin, m_labelGutterMax);
  m_viewFirstFrame = std::max<int32_t>(0, in.viewFirstFrame);
  m_autoUpdateLastFrame = in.autoUpdateLastFrame;
  m_sortMode = (SeqSortMode)std::clamp(in.sortMode, 0, (int)SeqSortMode::Type);
  m_showGraphPanel = in.showGraphPanel;
  std::snprintf(m_searchBuf, sizeof(m_searchBuf), "%s", in.search.c_str());

  m_expandState.clear();
  m_stopwatchState.clear();
  m_selectedLayerBlocks.clear();
  markLayoutDirty();
  if (!m_world)
    return;

  for (const auto &t : in.expand) {
    if (!t.entity)
      continue;
    const EntityID e = m_world->findByUUID(t.entity);
    if (e == InvalidEntity || !m_world->isAlive(e))
      continue;
    m_expandState[rowKey(e, (SeqRowType)t.rowType, (SeqProperty)t.prop)] =
        t.value;
  }
  for (const auto &t : in.stopwatch) {
    if (!t.entity)
      continue;
    const EntityID e = m_world->findByUUID(t.entity);
    if (e == InvalidEntity || !m_world->isAlive(e))
      continue;
    m_stopwatchState[rowKey(e, (SeqRowType)t.rowType, (SeqProperty)t.prop)] =
        t.value;
  }
  for (const EntityUUID &u : in.selectedLayers) {
    if (!u)
      continue;
    const EntityID e = m_world->findByUUID(u);
    if (e != InvalidEntity && m_world->isAlive(e))
      m_selectedLayerBlocks.insert(e);
  }
}

int32_t SequencerPanel::entityEndFrame(EntityID e) const {
  auto it = m_entityEndFrame.find(e);
  if (it == m_entityEndFrame.end())
    return m_clip ? std::max<int32_t>(0, m_clip->lastFrame) : 0;
  return it->second;
}

void SequencerPanel::setEntityEndFrame(EntityID e, int32_t endFrame) {
  if (!m_clip)
    return;
  const uint32_t blockId = resolveTargetBlock(e);
  m_rangeUserEdited.insert(e);
  const int32_t clamped = std::max<int32_t>(0, endFrame);
  m_entityEndFrame[e] = clamped;
  for (auto &r : m_clip->entityRanges) {
    if (r.entity == e && r.blockId == blockId) {
      r.end = clamped;
      if (m_autoUpdateLastFrame)
        recomputeLastFrameFromKeys();
      return;
    }
  }
  AnimEntityRange r{};
  r.entity = e;
  r.blockId =
      (blockId != 0) ? blockId : std::max<uint32_t>(1u, m_clip->nextBlockId++);
  r.start = 0;
  r.end = clamped;
  m_clip->entityRanges.push_back(r);
  if (m_autoUpdateLastFrame)
    recomputeLastFrameFromKeys();
}

int32_t SequencerPanel::entityStartFrame(EntityID e) const {
  auto it = m_entityStartFrame.find(e);
  if (it == m_entityStartFrame.end())
    return 0;
  return it->second;
}

void SequencerPanel::setEntityStartFrame(EntityID e, int32_t startFrame) {
  if (!m_clip)
    return;
  const uint32_t blockId = resolveTargetBlock(e);
  m_rangeUserEdited.insert(e);
  const int32_t clamped = std::max<int32_t>(0, startFrame);
  m_entityStartFrame[e] = clamped;
  for (auto &r : m_clip->entityRanges) {
    if (r.entity == e && r.blockId == blockId) {
      r.start = clamped;
      if (m_autoUpdateLastFrame)
        recomputeLastFrameFromKeys();
      return;
    }
  }
  AnimEntityRange r{};
  r.entity = e;
  r.blockId =
      (blockId != 0) ? blockId : std::max<uint32_t>(1u, m_clip->nextBlockId++);
  r.start = clamped;
  r.end = std::max<int32_t>(clamped, entityEndFrame(e));
  m_clip->entityRanges.push_back(r);
  if (m_autoUpdateLastFrame)
    recomputeLastFrameFromKeys();
}

void SequencerPanel::updateHiddenEntities() {
  if (!m_anim || !m_clip)
    return;

  if (m_anim->frame() > m_clip->lastFrame)
    m_anim->setFrame(m_clip->lastFrame);

  m_hiddenEntities.clear();
  // disabledAnim is handled by AnimationSystem using clip ranges.
}

} // namespace Nyx
