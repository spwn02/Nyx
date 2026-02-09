#include "editor/ui/panels/SequencerPanel.h"

#include "animation/AnimKeying.h"
#include "animation/AnimationSystem.h"
#include "animation/AnimationTypes.h"
#include "scene/World.h"

#include <algorithm>
#include <glm/gtx/quaternion.hpp>
#include <unordered_map>
#include <vector>

namespace Nyx {

namespace {

bool propertyHasAnimChannels(SeqProperty prop) {
  return prop == SeqProperty::Position || prop == SeqProperty::Rotation ||
         prop == SeqProperty::Scale;
}

uint64_t packTrackKey(EntityID e, uint32_t blockId, AnimChannel ch) {
  const uint64_t ent = (uint64_t(e.generation) << 32) | uint64_t(e.index);
  return (ent * 1315423911ull) ^ (uint64_t(blockId) << 8) ^ uint64_t(ch);
}

} // namespace

void SequencerPanel::rebuildTrackIndexCache() const {
  m_trackIndexCache.clear();
  if (!m_clip) {
    m_trackIndexCacheDirty = false;
    return;
  }

  m_trackIndexCache.reserve(m_clip->tracks.size() * 2 + 1);
  for (int ti = 0; ti < (int)m_clip->tracks.size(); ++ti) {
    const auto &t = m_clip->tracks[(size_t)ti];
    const uint64_t key = packTrackKey(t.entity, t.blockId, t.channel);
    m_trackIndexCache[key] = ti;
  }
  m_trackIndexCacheDirty = false;
}

int SequencerPanel::findTrackIndexCached(EntityID e, uint32_t blockId,
                                         AnimChannel ch) const {
  if (!m_clip)
    return -1;
  if (m_trackIndexCacheDirty)
    rebuildTrackIndexCache();
  const uint64_t key = packTrackKey(e, blockId, ch);
  auto it = m_trackIndexCache.find(key);
  if (it == m_trackIndexCache.end())
    return -1;
  const int ti = it->second;
  if (ti < 0 || ti >= (int)m_clip->tracks.size())
    return -1;
  const auto &t = m_clip->tracks[(size_t)ti];
  if (t.entity != e || t.blockId != blockId || t.channel != ch) {
    m_trackIndexCacheDirty = true;
    return -1;
  }
  return ti;
}

uint32_t SequencerPanel::resolveTargetBlock(EntityID e) const {
  if (!m_clip)
    return 0;
  for (const auto &r : m_clip->entityRanges) {
    if (r.entity != e)
      continue;
    if (m_selectedRangeBlocks.find(r.blockId) != m_selectedRangeBlocks.end())
      return r.blockId;
  }
  for (const auto &r : m_clip->entityRanges) {
    if (r.entity == e)
      return r.blockId;
  }
  return 0;
}

int SequencerPanel::graphTrackForProperty(EntityID e, SeqProperty prop,
                                          int component) const {
  if (!propertyHasAnimChannels(prop))
    return -1;
  if (!m_clip)
    return -1;
  AnimChannel ch[3];
  propertyChannels(prop, ch);
  const int ci = std::clamp(component, 0, 2);
  const uint32_t blockId = resolveTargetBlock(e);
  return findTrackIndexCached(e, blockId, ch[ci]);
}

int SequencerPanel::graphTrackForPropertyBest(EntityID e, SeqProperty prop) const {
  if (!propertyHasAnimChannels(prop))
    return -1;
  if (!m_clip)
    return -1;

  int firstValid = -1;
  for (int ci = 0; ci < 3; ++ci) {
    const int ti = graphTrackForProperty(e, prop, ci);
    if (ti < 0)
      continue;
    if (firstValid < 0)
      firstValid = ti;
    const auto &keys = m_clip->tracks[(size_t)ti].curve.keys;
    if (!keys.empty())
      return ti;
  }
  return firstValid;
}

bool SequencerPanel::stopwatchEnabled(EntityID e, SeqProperty prop) const {
  const uint64_t key = ((uint64_t)resolveTargetBlock(e) << 32) |
                       (uint64_t)prop;
  auto it = m_stopwatchState.find(key);
  return it != m_stopwatchState.end() && it->second;
}

void SequencerPanel::setStopwatch(EntityID e, SeqProperty prop, bool enabled) {
  const uint64_t key = ((uint64_t)resolveTargetBlock(e) << 32) |
                       (uint64_t)prop;
  const bool wasEnabled = stopwatchEnabled(e, prop);
  m_stopwatchState[key] = enabled;
  if (!m_anim || !m_clip || !m_world || !m_world->isAlive(e))
    return;

  if (enabled && !wasEnabled) {
    const int32_t f = clampFrame(m_anim->frame());
    const bool hasAny = findPropertyKeys(e, prop, m_frameScratch);
    if (!hasAny || std::find(m_frameScratch.begin(), m_frameScratch.end(), f) ==
                       m_frameScratch.end())
      addOrOverwritePropertyKeys(e, prop, f);
  } else if (!enabled && wasEnabled) {
    clearPropertyKeys(e, prop);
  }
}

void SequencerPanel::propertyChannels(SeqProperty prop,
                                      AnimChannel out[3]) const {
  switch (prop) {
  case SeqProperty::Position:
    out[0] = AnimChannel::TranslateX;
    out[1] = AnimChannel::TranslateY;
    out[2] = AnimChannel::TranslateZ;
    break;
  case SeqProperty::Rotation:
    out[0] = AnimChannel::RotateX;
    out[1] = AnimChannel::RotateY;
    out[2] = AnimChannel::RotateZ;
    break;
  case SeqProperty::Scale:
    out[0] = AnimChannel::ScaleX;
    out[1] = AnimChannel::ScaleY;
    out[2] = AnimChannel::ScaleZ;
    break;
  default:
    out[0] = AnimChannel::TranslateX;
    out[1] = AnimChannel::TranslateY;
    out[2] = AnimChannel::TranslateZ;
    break;
  }
}

bool SequencerPanel::findPropertyKeys(EntityID e, SeqProperty prop,
                                      std::vector<int32_t> &outFrames) const {
  return buildPropertyFrameCache(e, prop, outFrames, nullptr);
}

bool SequencerPanel::buildPropertyFrameCache(
    EntityID e, SeqProperty prop, std::vector<int32_t> &outFrames,
    std::unordered_map<int32_t, SeqKeyRef> *outFrameToKey) const {
  if (!propertyHasAnimChannels(prop))
    return false;
  if (!m_clip)
    return false;

  outFrames.clear();
  if (outFrameToKey)
    outFrameToKey->clear();

  const uint32_t blockId = resolveTargetBlock(e);
  AnimChannel ch[3];
  propertyChannels(prop, ch);
  for (int ci = 0; ci < 3; ++ci) {
    const int ti = findTrackIndexCached(e, blockId, ch[ci]);
    if (ti < 0)
      continue;

    const auto &t = m_clip->tracks[(size_t)ti];
    for (int ki = 0; ki < (int)t.curve.keys.size(); ++ki) {
      const int32_t frame = (int32_t)t.curve.keys[(size_t)ki].frame;
      outFrames.push_back(frame);
      if (outFrameToKey && outFrameToKey->find(frame) == outFrameToKey->end())
        outFrameToKey->emplace(frame, SeqKeyRef{ti, ki});
    }
  }

  if (outFrames.empty())
    return false;
  std::sort(outFrames.begin(), outFrames.end());
  outFrames.erase(std::unique(outFrames.begin(), outFrames.end()),
                  outFrames.end());
  return true;
}

void SequencerPanel::setKeyAt(int32_t trackIndex, int32_t frame, float value) {
  if (!m_clip)
    return;
  if (trackIndex < 0 || trackIndex >= (int)m_clip->tracks.size())
    return;

  auto &keys = m_clip->tracks[(size_t)trackIndex].curve.keys;
  const int32_t f = clampFrame(frame);
  for (int i = 0; i < (int)keys.size(); ++i) {
    if ((int32_t)keys[(size_t)i].frame == f) {
      keys[(size_t)i].value = value;
      return;
    }
  }

  AnimKey k{};
  k.frame = (AnimFrame)f;
  k.value = value;
  keys.push_back(k);
  std::sort(keys.begin(), keys.end(),
            [](const AnimKey &a, const AnimKey &b) { return a.frame < b.frame; });
}

bool SequencerPanel::addOrOverwritePropertyKeys(EntityID e, SeqProperty prop,
                                                int32_t frame,
                                                const float *rotationEulerDeg) {
  if (!propertyHasAnimChannels(prop))
    return false;
  if (!m_clip || !m_world)
    return false;

  AnimChannel ch[3];
  propertyChannels(prop, ch);
  float values[3] = {};
  if (prop == SeqProperty::Position) {
    const auto &tr = m_world->transform(e);
    values[0] = tr.translation.x;
    values[1] = tr.translation.y;
    values[2] = tr.translation.z;
  } else if (prop == SeqProperty::Rotation) {
    if (rotationEulerDeg) {
      values[0] = rotationEulerDeg[0];
      values[1] = rotationEulerDeg[1];
      values[2] = rotationEulerDeg[2];
    } else {
      const auto &tr = m_world->transform(e);
      const glm::vec3 eulerDeg = glm::degrees(glm::eulerAngles(tr.rotation));
      values[0] = eulerDeg.x;
      values[1] = eulerDeg.y;
      values[2] = eulerDeg.z;
    }
  } else if (prop == SeqProperty::Scale) {
    const auto &tr = m_world->transform(e);
    values[0] = tr.scale.x;
    values[1] = tr.scale.y;
    values[2] = tr.scale.z;
  } else {
    return false;
  }

  bool wrote = false;
  const uint32_t blockId = resolveTargetBlock(e);
  for (int ci = 0; ci < 3; ++ci) {
    int trackIndex = normalizeTrackPair(e, blockId, ch[ci]);
    if (trackIndex < 0) {
      AnimTrack nt{};
      nt.entity = e;
      nt.blockId = blockId;
      nt.channel = ch[ci];
      m_clip->tracks.push_back(nt);
      invalidateTrackIndexCache();
      trackIndex = (int)m_clip->tracks.size() - 1;
    }
    setKeyAt(trackIndex, frame, values[ci]);
    wrote = true;
  }

  if (wrote && m_autoUpdateLastFrame)
    recomputeLastFrameFromKeys();
  return wrote;
}

bool SequencerPanel::deletePropertyKeysAtFrame(EntityID e, SeqProperty prop,
                                               int32_t frame) {
  if (!propertyHasAnimChannels(prop))
    return false;
  if (!m_clip)
    return false;

  const int32_t f = clampFrame(frame);
  AnimChannel ch[3];
  propertyChannels(prop, ch);
  const uint32_t blockId = resolveTargetBlock(e);
  bool removed = false;

  for (int ci = 0; ci < 3; ++ci) {
    const int ti = normalizeTrackPair(e, blockId, ch[ci]);
    if (ti < 0 || ti >= (int)m_clip->tracks.size())
      continue;
    auto &keys = m_clip->tracks[(size_t)ti].curve.keys;
    for (int ki = (int)keys.size() - 1; ki >= 0; --ki) {
      if ((int32_t)keys[(size_t)ki].frame == f) {
        keys.erase(keys.begin() + (ptrdiff_t)ki);
        removed = true;
      }
    }
  }

  if (removed && m_autoUpdateLastFrame)
    recomputeLastFrameFromKeys();
  return removed;
}

void SequencerPanel::clearPropertyKeys(EntityID e, SeqProperty prop) {
  if (!propertyHasAnimChannels(prop) || !m_clip)
    return;

  AnimChannel ch[3];
  propertyChannels(prop, ch);
  const uint32_t blockId = resolveTargetBlock(e);
  bool changed = false;

  for (int ci = 0; ci < 3; ++ci) {
    const int ti = normalizeTrackPair(e, blockId, ch[ci]);
    if (ti < 0 || ti >= (int)m_clip->tracks.size())
      continue;
    auto &keys = m_clip->tracks[(size_t)ti].curve.keys;
    if (!keys.empty()) {
      keys.clear();
      changed = true;
    }
  }

  if (changed && m_autoUpdateLastFrame)
    recomputeLastFrameFromKeys();
}

int SequencerPanel::normalizeTrackPair(EntityID e, uint32_t blockId, AnimChannel ch) {
  if (!m_clip)
    return -1;

  std::vector<int> idx;
  for (int ti = 0; ti < (int)m_clip->tracks.size(); ++ti) {
    const auto &t = m_clip->tracks[(size_t)ti];
    if (t.entity == e && t.blockId == blockId && t.channel == ch)
      idx.push_back(ti);
  }
  if (idx.empty())
    return -1;
  if (idx.size() == 1)
    return idx[0];

  std::vector<AnimKey> merged;
  for (int ti : idx) {
    const auto &keys = m_clip->tracks[(size_t)ti].curve.keys;
    for (const auto &k : keys)
      merged.push_back(k);
  }
  std::sort(merged.begin(), merged.end(),
            [](const AnimKey &a, const AnimKey &b) { return a.frame < b.frame; });

  std::vector<AnimKey> dedup;
  dedup.reserve(merged.size());
  for (const auto &k : merged) {
    if (!dedup.empty() && dedup.back().frame == k.frame) {
      dedup.back() = k;
    } else {
      dedup.push_back(k);
    }
  }

  const int keep = idx.back();
  m_clip->tracks[(size_t)keep].curve.keys = std::move(dedup);
  for (int i = (int)idx.size() - 2; i >= 0; --i) {
    const int eraseTi = idx[(size_t)i];
    m_clip->tracks.erase(m_clip->tracks.begin() + (ptrdiff_t)eraseTi);
  }
  invalidateTrackIndexCache();

  return findTrackIndexCached(e, blockId, ch);
}

bool SequencerPanel::movePropertyKeys(EntityID e, SeqProperty prop,
                                      int32_t fromFrame, int32_t toFrame) {
  if (!propertyHasAnimChannels(prop))
    return false;
  if (!m_clip || fromFrame == toFrame)
    return false;

  AnimChannel ch[3];
  propertyChannels(prop, ch);
  bool moved = false;

  for (int ci = 0; ci < 3; ++ci) {
    for (int ti = 0; ti < (int)m_clip->tracks.size(); ++ti) {
      auto &t = m_clip->tracks[(size_t)ti];
      if (t.entity != e || t.channel != ch[ci])
        continue;
      auto &keys = t.curve.keys;
      int idx = -1;
      float val = 0.0f;
      for (int ki = 0; ki < (int)keys.size(); ++ki) {
        if ((int32_t)keys[(size_t)ki].frame == fromFrame) {
          idx = ki;
          val = keys[(size_t)ki].value;
          break;
        }
      }
      if (idx < 0)
        continue;
      keys.erase(keys.begin() + (ptrdiff_t)idx);
      int existing = -1;
      for (int ki = 0; ki < (int)keys.size(); ++ki) {
        if ((int32_t)keys[(size_t)ki].frame == toFrame) {
          existing = ki;
          break;
        }
      }
      if (existing >= 0) {
        keys[(size_t)existing].value = val;
      } else {
        AnimKey nk{};
        nk.frame = (AnimFrame)toFrame;
        nk.value = val;
        keys.push_back(nk);
      }
      std::sort(keys.begin(), keys.end(),
                [](const AnimKey &a, const AnimKey &b) {
                  return a.frame < b.frame;
                });
      moved = true;
      break;
    }
  }
  return moved;
}

} // namespace Nyx
