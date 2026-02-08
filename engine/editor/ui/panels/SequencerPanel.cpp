#include "SequencerPanel.h"

#include "animation/AnimationSystem.h"
#include "animation/AnimationTypes.h"
#include "animation/AnimKeying.h"
#include "scene/World.h"
#include "core/Paths.h"

#include "input/InputSystem.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <imgui.h>
#include <glm/gtx/quaternion.hpp>

namespace Nyx {

static int32_t clampi(int32_t v, int32_t a, int32_t b) {
  if (v < a)
    return a;
  if (v > b)
    return b;
  return v;
}

static float clampf(float v, float a, float b) {
  if (v < a)
    return a;
  if (v > b)
    return b;
  return v;
}

static bool vecNear(const ImVec2 &a, const ImVec2 &b, float r) {
  const float dx = a.x - b.x;
  const float dy = a.y - b.y;
  return (dx * dx + dy * dy) <= r * r;
}

static ImU32 brightenColor(ImU32 c, float mul) {
  int r = (int)(float((c >> IM_COL32_R_SHIFT) & 0xFF) * mul);
  int g = (int)(float((c >> IM_COL32_G_SHIFT) & 0xFF) * mul);
  int b = (int)(float((c >> IM_COL32_B_SHIFT) & 0xFF) * mul);
  const int a = (int)((c >> IM_COL32_A_SHIFT) & 0xFF);
  r = std::clamp(r, 0, 255);
  g = std::clamp(g, 0, 255);
  b = std::clamp(b, 0, 255);
  return IM_COL32(r, g, b, a);
}

static uint64_t packRowKey(EntityID e, SeqRowType type, SeqProperty prop) {
  const uint64_t a = (uint64_t)e.index;
  const uint64_t b = (uint64_t)e.generation;
  const uint64_t t = (uint64_t)type;
  const uint64_t p = (uint64_t)prop;
  return (a << 32) ^ (b << 16) ^ (t << 8) ^ p;
}

static bool drawAtlasIconButton(const IconAtlas &atlas, const char *name,
                                const ImVec2 &size, ImU32 tint) {
  const AtlasRegion *r = atlas.find(name);
  if (!r)
    return ImGui::SmallButton("?");
  ImGui::InvisibleButton(name, size);
  ImDrawList *dl = ImGui::GetWindowDrawList();
  ImVec2 p0 = ImGui::GetItemRectMin();
  ImVec2 p1 = ImGui::GetItemRectMax();
  dl->AddImage(atlas.imguiTexId(), p0, p1, r->uv0, r->uv1, tint);
  return ImGui::IsItemClicked();
}

static const char *channelName(AnimChannel c) {
  switch (c) {
  case AnimChannel::TranslateX:
    return "T.X";
  case AnimChannel::TranslateY:
    return "T.Y";
  case AnimChannel::TranslateZ:
    return "T.Z";
  case AnimChannel::RotateX:
    return "R.X";
  case AnimChannel::RotateY:
    return "R.Y";
  case AnimChannel::RotateZ:
    return "R.Z";
  case AnimChannel::ScaleX:
    return "S.X";
  case AnimChannel::ScaleY:
    return "S.Y";
  case AnimChannel::ScaleZ:
    return "S.Z";
  default:
    return "Ch";
  }
}

static bool propertyHasAnimChannels(SeqProperty prop) {
  return prop == SeqProperty::Position || prop == SeqProperty::Rotation ||
         prop == SeqProperty::Scale;
}

static bool isNlaSelectId(uint32_t id) { return (id & 0x80000000u) != 0u; }
static uint32_t nlaSelectIdFromIndex(int idx) {
  return 0x80000000u | (uint32_t)(idx + 1);
}
static int nlaIndexFromSelectId(uint32_t id) {
  return (int)((id & 0x7fffffffu) - 1u);
}

float SequencerPanel::frameToX(int32_t frame, int32_t firstFrame,
                               float xStart) const {
  return xStart + float(frame - firstFrame) * m_pixelsPerFrame;
}

int32_t SequencerPanel::xToFrame(float x, int32_t firstFrame,
                                 float xStart) const {
  const float localX = x - xStart;
  return firstFrame +
         (int32_t)std::floor(localX / m_pixelsPerFrame + 0.5f);
}

int32_t SequencerPanel::clampFrame(int32_t f) const {
  if (!m_clip)
    return std::max<int32_t>(0, f);
  const int32_t last = std::max<int32_t>(0, m_clip->lastFrame);
  return clampi(f, 0, last);
}

uint64_t SequencerPanel::rowKey(EntityID e, SeqRowType type,
                                SeqProperty prop) const {
  return packRowKey(e, type, prop);
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
  for (int i = 0; i < (int)m_clip->tracks.size(); ++i) {
    const auto &t = m_clip->tracks[(size_t)i];
    if (t.entity == e && t.blockId == blockId && t.channel == ch[ci])
      return i;
  }
  return -1;
}

int SequencerPanel::graphTrackForPropertyBest(EntityID e, SeqProperty prop) const {
  if (!propertyHasAnimChannels(prop))
    return -1;
  if (!m_clip)
    return -1;
  // Prefer a channel that actually has keys so graph doesn't appear empty.
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
    std::vector<int32_t> frames;
    const bool hasAny = findPropertyKeys(e, prop, frames);
    if (!hasAny || std::find(frames.begin(), frames.end(), f) == frames.end())
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
  if (!propertyHasAnimChannels(prop))
    return false;
  if (!m_clip)
    return false;
  outFrames.clear();
  const uint32_t blockId = resolveTargetBlock(e);
  AnimChannel ch[3];
  propertyChannels(prop, ch);
  for (int ci = 0; ci < 3; ++ci) {
    for (const auto &t : m_clip->tracks) {
      if (t.entity != e || t.blockId != blockId || t.channel != ch[ci])
        continue;
      for (const auto &k : t.curve.keys) {
        outFrames.push_back((int32_t)k.frame);
      }
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
            [](const AnimKey &a, const AnimKey &b) {
              return a.frame < b.frame;
            });
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
  for (int ci = 0; ci < 3; ++ci) {
    const uint32_t blockId = resolveTargetBlock(e);
    int trackIndex = normalizeTrackPair(e, blockId, ch[ci]);
    if (trackIndex < 0) {
      AnimTrack nt{};
      nt.entity = e;
      nt.blockId = blockId;
      nt.channel = ch[ci];
      m_clip->tracks.push_back(nt);
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
  bool removed = false;
  for (int ci = 0; ci < 3; ++ci) {
    const int ti = normalizeTrackPair(e, resolveTargetBlock(e), ch[ci]);
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
  if (!propertyHasAnimChannels(prop))
    return;
  if (!m_clip)
    return;
  AnimChannel ch[3];
  propertyChannels(prop, ch);
  bool changed = false;
  for (int ci = 0; ci < 3; ++ci) {
    const int ti = normalizeTrackPair(e, resolveTargetBlock(e), ch[ci]);
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
  std::sort(merged.begin(), merged.end(), [](const AnimKey &a, const AnimKey &b) {
    return a.frame < b.frame;
  });
  // Deduplicate same-frame keys, keeping the latest encountered value.
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

  for (int ti = 0; ti < (int)m_clip->tracks.size(); ++ti) {
    const auto &t = m_clip->tracks[(size_t)ti];
    if (t.entity == e && t.blockId == blockId && t.channel == ch)
      return ti;
  }
  return -1;
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

bool SequencerPanel::hitTestPropertyKey(const ImRect &r, int32_t firstFrame,
                                        const ImVec2 &mouse,
                                        EntityID &outEntity,
                                        SeqProperty &outProp,
                                        int32_t &outFrame,
                                        SeqKeyRef &outKey) const {
  outEntity = InvalidEntity;
  outProp = SeqProperty::Position;
  outFrame = 0;
  outKey = SeqKeyRef{};
  if (!m_clip)
    return false;
  const float laneH = m_rulerHeight;
  ImRect tracks = r;
  tracks.Min.y += laneH;

  if (mouse.x < (tracks.Min.x + m_labelGutter) || mouse.x > tracks.Max.x ||
      mouse.y < tracks.Min.y || mouse.y > tracks.Max.y)
    return false;

  const int32_t row = (int32_t)((mouse.y - tracks.Min.y) / m_rowHeight);
  if (row < 0 || row >= (int32_t)m_rows.size())
    return false;

  const SeqRow &rr = m_rows[(size_t)row];
  if (rr.type != SeqRowType::Property)
    return false;
  if (!propertyHasAnimChannels(rr.prop))
    return false;

  const float xStart = tracks.Min.x + m_labelGutter;
  const float y0 = tracks.Min.y + float(row) * m_rowHeight;
  const float y1 = y0 + m_rowHeight;
  const float cy = (y0 + y1) * 0.5f;

  std::vector<int32_t> frames;
  if (!findPropertyKeys(rr.entity, rr.prop, frames))
    return false;

  for (int32_t f : frames) {
    const float x = frameToX(f, firstFrame, xStart);
    if (!vecNear(mouse, ImVec2(x, cy), 6.0f))
      continue;
    AnimChannel ch[3];
    propertyChannels(rr.prop, ch);
    for (int ci = 0; ci < 3; ++ci) {
      for (int ti = 0; ti < (int)m_clip->tracks.size(); ++ti) {
        const auto &t = m_clip->tracks[(size_t)ti];
        if (t.entity != rr.entity || t.channel != ch[ci])
          continue;
        const auto &keys = t.curve.keys;
        for (int ki = 0; ki < (int)keys.size(); ++ki) {
          if ((int32_t)keys[(size_t)ki].frame == f) {
            outEntity = rr.entity;
            outProp = rr.prop;
            outFrame = f;
            outKey = SeqKeyRef{ti, ki};
            return true;
          }
        }
      }
    }
  }
  return false;
}

bool SequencerPanel::isLayerHidden(EntityID e) const {
  if (!m_world || !m_world->isAlive(e))
    return false;
  return m_world->transform(e).hidden;
}

ImU32 SequencerPanel::layerColor(EntityID e) const {
  if (!m_world || !m_world->isAlive(e))
    return IM_COL32(90, 90, 90, 255);
  if (m_world->hasCamera(e))
    return IM_COL32(80, 140, 255, 255);
  if (m_world->hasLight(e))
    return IM_COL32(255, 210, 80, 255);
  if (m_world->hasMesh(e))
    return IM_COL32(90, 200, 180, 255);
  return IM_COL32(120, 120, 120, 255);
}

void SequencerPanel::togglePlay() {
  if (!m_anim)
    return;
  m_anim->toggle();
}

void SequencerPanel::stop() {
  if (!m_anim)
    return;
  m_anim->pause();
  m_anim->setFrame(0);
}

void SequencerPanel::step(int32_t delta) {
  if (!m_anim || !m_clip)
    return;
  const int32_t cur = m_anim->frame();
  const int32_t last = std::max<int32_t>(0, m_clip->lastFrame);
  m_anim->setFrame(clampi(cur + delta, 0, last));
}

void SequencerPanel::recomputeLastFrameFromKeys() {
  if (!m_clip)
    return;

  int32_t maxF = 0;
  for (const auto &t : m_clip->tracks) {
    for (const auto &k : t.curve.keys) {
      maxF = std::max(maxF, (int32_t)k.frame);
    }
  }
  for (const auto &r : m_clip->entityRanges) {
    maxF = std::max(maxF, (int32_t)r.end);
  }

  // Blender-ish: last_frame can shrink/expand, loop uses lastFrame
  m_clip->lastFrame = std::max<int32_t>(0, maxF);
}

void SequencerPanel::clearSelection() {
  m_selectedKeys.clear();
  m_activeKey = SeqKeyRef{};
  m_draggingKey = false;
}

bool SequencerPanel::isSelected(const SeqKeyRef &k) const {
  for (const auto &s : m_selectedKeys)
    if (s == k)
      return true;
  return false;
}

void SequencerPanel::selectSingle(const SeqKeyRef &k) {
  m_selectedKeys.clear();
  m_selectedKeys.push_back(k);
  m_activeKey = k;
}

void SequencerPanel::toggleSelect(const SeqKeyRef &k) {
  for (size_t i = 0; i < m_selectedKeys.size(); ++i) {
    if (m_selectedKeys[i] == k) {
      m_selectedKeys.erase(m_selectedKeys.begin() + (ptrdiff_t)i);
      if (m_activeKey == k) {
        m_activeKey =
            m_selectedKeys.empty() ? SeqKeyRef{} : m_selectedKeys.back();
      }
      return;
    }
  }
  m_selectedKeys.push_back(k);
  m_activeKey = k;
}

void SequencerPanel::addSelect(const SeqKeyRef &k) {
  if (!isSelected(k))
    m_selectedKeys.push_back(k);
  m_activeKey = k;
}

void SequencerPanel::deleteSelectedKeys() {
  if (!m_clip || m_selectedKeys.empty())
    return;

  struct Pair {
    int t;
    int k;
  };
  std::vector<Pair> del;
  del.reserve(m_selectedKeys.size());
  for (auto &r : m_selectedKeys)
    del.push_back({r.trackIndex, r.keyIndex});

  std::sort(del.begin(), del.end(), [](const Pair &a, const Pair &b) {
    if (a.t != b.t)
      return a.t < b.t;
    return a.k > b.k;
  });

  for (const auto &p : del) {
    if (p.t < 0 || p.t >= (int)m_clip->tracks.size())
      continue;
    auto &keys = m_clip->tracks[(size_t)p.t].curve.keys;
    if (p.k < 0 || p.k >= (int)keys.size())
      continue;
    keys.erase(keys.begin() + (ptrdiff_t)p.k);
  }

  if (m_autoUpdateLastFrame)
    recomputeLastFrameFromKeys();
  clearSelection();
}

void SequencerPanel::copySelectedKeys() {
  if (!m_clip)
    return;
  m_clipboard.clear();
  if (m_selectedKeys.empty())
    return;

  for (const auto &r : m_selectedKeys) {
    if (r.trackIndex < 0 || r.trackIndex >= (int)m_clip->tracks.size())
      continue;
    const auto &keys = m_clip->tracks[(size_t)r.trackIndex].curve.keys;
    if (r.keyIndex < 0 || r.keyIndex >= (int)keys.size())
      continue;

    const auto &k = keys[(size_t)r.keyIndex];
    SeqKeyCopy c{};
    c.trackIndex = r.trackIndex;
    c.frame = (int32_t)k.frame;
    c.value = (float)k.value;
    m_clipboard.push_back(c);
  }
}

void SequencerPanel::pasteKeysAtFrame(int32_t frame) {
  if (!m_clip || m_clipboard.empty())
    return;

  int32_t minF = m_clipboard[0].frame;
  for (auto &c : m_clipboard)
    minF = std::min(minF, c.frame);

  clearSelection();

  for (auto &c : m_clipboard) {
    if (c.trackIndex < 0 || c.trackIndex >= (int)m_clip->tracks.size())
      continue;
    auto &keys = m_clip->tracks[(size_t)c.trackIndex].curve.keys;

    const int32_t newF = clampFrame(frame + (c.frame - minF));

    int existing = -1;
    for (int i = 0; i < (int)keys.size(); ++i) {
      if ((int32_t)keys[(size_t)i].frame == newF) {
        existing = i;
        break;
      }
    }

    if (existing >= 0) {
      keys[(size_t)existing].value = c.value;
      addSelect(SeqKeyRef{c.trackIndex, existing});
    } else {
      AnimKey nk{};
      nk.frame = (AnimFrame)newF;
      nk.value = c.value;
      keys.push_back(nk);
      std::sort(keys.begin(), keys.end(),
                [](const AnimKey &a, const AnimKey &b) {
                  return a.frame < b.frame;
                });

      int idx = -1;
      for (int i = 0; i < (int)keys.size(); ++i) {
        if ((int32_t)keys[(size_t)i].frame == newF &&
            std::fabs(keys[(size_t)i].value - c.value) < 1e-6f) {
          idx = i;
          break;
        }
      }
      if (idx >= 0)
        addSelect(SeqKeyRef{c.trackIndex, idx});
    }
  }

  if (m_autoUpdateLastFrame)
    recomputeLastFrameFromKeys();
}

void SequencerPanel::addKeyAt(int32_t trackIndex, int32_t frame) {
  if (!m_clip)
    return;
  if (trackIndex < 0 || trackIndex >= (int)m_rowEntities.size())
    return;

  const EntityID e = m_rowEntities[(size_t)trackIndex];
  const uint32_t blockId = resolveTargetBlock(e);
  int actualTrack = -1;
  for (int i = 0; i < (int)m_clip->tracks.size(); ++i) {
    const auto &t = m_clip->tracks[(size_t)i];
    if (t.entity == e && t.blockId == blockId &&
        t.channel == AnimChannel::TranslateX) {
      actualTrack = i;
      break;
    }
  }
  if (actualTrack < 0)
    return;

  auto &keys = m_clip->tracks[(size_t)actualTrack].curve.keys;
  const int32_t f = clampFrame(frame);

  for (int i = 0; i < (int)keys.size(); ++i) {
    if ((int32_t)keys[(size_t)i].frame == f) {
      selectSingle(SeqKeyRef{actualTrack, i});
      return;
    }
  }

  AnimKey k{};
  k.frame = (AnimFrame)f;
  k.value = 0.0f;

  keys.push_back(k);
  std::sort(keys.begin(), keys.end(), [](const AnimKey &a, const AnimKey &b) {
    return a.frame < b.frame;
  });

  for (int i = 0; i < (int)keys.size(); ++i) {
    if ((int32_t)keys[(size_t)i].frame == f) {
      selectSingle(SeqKeyRef{actualTrack, i});
      break;
    }
  }

  if (m_autoUpdateLastFrame)
    recomputeLastFrameFromKeys();
}

void SequencerPanel::moveKeyFrame(const SeqKeyRef &k, int32_t newFrame) {
  if (!m_clip)
    return;
  if (k.trackIndex < 0 || k.trackIndex >= (int)m_clip->tracks.size())
    return;

  auto &keys = m_clip->tracks[(size_t)k.trackIndex].curve.keys;
  if (k.keyIndex < 0 || k.keyIndex >= (int)keys.size())
    return;

  const int32_t nf = clampFrame(newFrame);
  const float value = keys[(size_t)k.keyIndex].value;

  keys.erase(keys.begin() + (ptrdiff_t)k.keyIndex);

  int existing = -1;
  for (int i = 0; i < (int)keys.size(); ++i) {
    if ((int32_t)keys[(size_t)i].frame == nf) {
      existing = i;
      break;
    }
  }

  if (existing >= 0) {
    keys[(size_t)existing].value = value;
  } else {
    AnimKey nk{};
    nk.frame = (AnimFrame)nf;
    nk.value = value;
    keys.push_back(nk);
  }

  std::sort(keys.begin(), keys.end(), [](const AnimKey &a, const AnimKey &b) {
    return a.frame < b.frame;
  });

  std::vector<SeqKeyRef> newSel;
  newSel.reserve(m_selectedKeys.size());
  for (auto &s : m_selectedKeys) {
    if (s.trackIndex != k.trackIndex)
      newSel.push_back(s);
  }
  m_selectedKeys = newSel;

  for (int i = 0; i < (int)keys.size(); ++i) {
    if ((int32_t)keys[(size_t)i].frame == nf) {
      addSelect(SeqKeyRef{k.trackIndex, i});
      break;
    }
  }

  if (m_autoUpdateLastFrame)
    recomputeLastFrameFromKeys();
}

void SequencerPanel::setKeyValue(const SeqKeyRef &k, float value) {
  if (!m_clip)
    return;
  if (k.trackIndex < 0 || k.trackIndex >= (int)m_clip->tracks.size())
    return;
  auto &keys = m_clip->tracks[(size_t)k.trackIndex].curve.keys;
  if (k.keyIndex < 0 || k.keyIndex >= (int)keys.size())
    return;
  keys[(size_t)k.keyIndex].value = value;
}

bool SequencerPanel::hitTestKey(const ImRect &r, int32_t firstFrame,
                                const ImVec2 &mouse,
                                SeqKeyRef &outKey) const {
  if (!m_clip)
    return false;

  const float laneH = m_rulerHeight;
  ImRect tracks = r;
  tracks.Min.y += laneH;

  if (mouse.x < (tracks.Min.x + m_labelGutter) || mouse.x > tracks.Max.x ||
      mouse.y < tracks.Min.y || mouse.y > tracks.Max.y)
    return false;

  const int32_t row = (int32_t)((mouse.y - tracks.Min.y) / m_rowHeight);
  if (row < 0 || row >= (int32_t)m_rows.size())
    return false;

  const float xStart = tracks.Min.x + m_labelGutter;
  const SeqRow &rrow = m_rows[(size_t)row];
  if (rrow.type != SeqRowType::Property)
    return false;

  const float y0 = tracks.Min.y + float(row) * m_rowHeight;
  const float y1 = y0 + m_rowHeight;
  const float cy = (y0 + y1) * 0.5f;

  std::vector<int32_t> frames;
  if (!findPropertyKeys(rrow.entity, rrow.prop, frames))
    return false;

  AnimChannel ch[3];
  propertyChannels(rrow.prop, ch);

  for (int32_t f : frames) {
    const float x = frameToX(f, firstFrame, xStart);
    if (!vecNear(mouse, ImVec2(x, cy), 6.0f))
      continue;
    // pick first matching key on any channel
    for (int ci = 0; ci < 3; ++ci) {
      for (int ti = 0; ti < (int)m_clip->tracks.size(); ++ti) {
        const auto &t = m_clip->tracks[(size_t)ti];
        if (t.entity != rrow.entity || t.channel != ch[ci])
          continue;
        const auto &keys = t.curve.keys;
        for (int ki = 0; ki < (int)keys.size(); ++ki) {
          if ((int32_t)keys[(size_t)ki].frame == f) {
            outKey = SeqKeyRef{ti, ki};
            return true;
          }
        }
      }
    }
  }

  return false;
}

void SequencerPanel::ensureTracksForWorld() {
  if (!m_world || !m_clip)
    return;

  const AnimChannel channels[] = {
      AnimChannel::TranslateX, AnimChannel::TranslateY, AnimChannel::TranslateZ,
      AnimChannel::RotateX,    AnimChannel::RotateY,    AnimChannel::RotateZ,
      AnimChannel::ScaleX,     AnimChannel::ScaleY,     AnimChannel::ScaleZ};

  for (EntityID e : m_world->alive()) {
    if (!m_world->isAlive(e))
      continue;
    if (m_trackExclude.find(e) != m_trackExclude.end())
      continue;
    bool hasRange = false;
    for (const auto &r : m_clip->entityRanges) {
      if (r.entity == e) {
        hasRange = true;
        break;
      }
    }
    if (!hasRange) {
      AnimEntityRange r{};
      r.entity = e;
      r.blockId = std::max<uint32_t>(1u, m_clip->nextBlockId++);
      r.start = 0;
      r.end = std::max<int32_t>(0, m_clip->lastFrame);
      m_clip->entityRanges.push_back(r);
    }
    for (const auto &r : m_clip->entityRanges) {
      if (r.entity != e)
        continue;
      for (AnimChannel ch : channels) {
        normalizeTrackPair(e, r.blockId, ch);
        bool found = false;
        for (const auto &t : m_clip->tracks) {
          if (t.entity == e && t.blockId == r.blockId && t.channel == ch) {
            found = true;
            break;
          }
        }
        if (!found) {
          AnimTrack nt{};
          nt.entity = e;
          nt.blockId = r.blockId;
          nt.channel = ch;
          m_clip->tracks.push_back(nt);
        }
      }
    }
  }
}

void SequencerPanel::buildRowEntities() {
  m_rowEntities.clear();
  if (!m_world)
    return;
  auto toLower = [](std::string s) {
    for (char &c : s)
      c = (char)std::tolower((unsigned char)c);
    return s;
  };
  const std::string filter = toLower(std::string(m_searchBuf));
  for (EntityID e : m_world->alive()) {
    if (!m_world->isAlive(e))
      continue;
    if (m_trackExclude.find(e) != m_trackExclude.end())
      continue;
    if (!filter.empty()) {
      const std::string &name = m_world->name(e).name;
      if (toLower(name).find(filter) == std::string::npos)
        continue;
    }
    m_rowEntities.push_back(e);
  }

  auto nameKey = [this](EntityID e) -> const std::string & {
    return m_world->name(e).name;
  };
  auto parentNameKey = [this](EntityID e) -> std::string {
    EntityID p = m_world->parentOf(e);
    if (p != InvalidEntity && m_world->isAlive(p))
      return m_world->name(p).name;
    return {};
  };
  auto typeKey = [this](EntityID e) -> int {
    if (m_world->hasCamera(e))
      return 0;
    if (m_world->hasLight(e))
      return 1;
    if (m_world->hasMesh(e))
      return 2;
    return 3;
  };

  switch (m_sortMode) {
  case SeqSortMode::NameAZ:
    std::sort(m_rowEntities.begin(), m_rowEntities.end(),
              [&](EntityID a, EntityID b) { return nameKey(a) < nameKey(b); });
    break;
  case SeqSortMode::NameZA:
    std::sort(m_rowEntities.begin(), m_rowEntities.end(),
              [&](EntityID a, EntityID b) { return nameKey(a) > nameKey(b); });
    break;
  case SeqSortMode::Parent:
    std::sort(m_rowEntities.begin(), m_rowEntities.end(),
              [&](EntityID a, EntityID b) {
                const std::string pa = parentNameKey(a);
                const std::string pb = parentNameKey(b);
                if (pa != pb)
                  return pa < pb;
                return nameKey(a) < nameKey(b);
              });
    break;
  case SeqSortMode::Type:
    std::sort(m_rowEntities.begin(), m_rowEntities.end(),
              [&](EntityID a, EntityID b) {
                const int ta = typeKey(a);
                const int tb = typeKey(b);
                if (ta != tb)
                  return ta < tb;
                return nameKey(a) < nameKey(b);
              });
    break;
  case SeqSortMode::SceneOrder:
  default:
    std::sort(m_rowEntities.begin(), m_rowEntities.end(),
              [](EntityID a, EntityID b) { return a.index < b.index; });
    break;
  }

  // Ensure duration entries
  if (m_clip) {
    const int32_t clipEnd = std::max<int32_t>(0, m_clip->lastFrame);
    const int32_t defaultStart = 0;
    const int32_t defaultEnd = clipEnd;
    for (EntityID e : m_rowEntities) {
      // Ensure clip range entry exists
      bool hasRange = false;
      for (auto &r : m_clip->entityRanges) {
        if (r.entity == e) {
          hasRange = true;
          break;
        }
      }
      if (!hasRange) {
        AnimEntityRange r{};
        r.entity = e;
        r.blockId = std::max<uint32_t>(1u, m_clip->nextBlockId++);
        r.start = defaultStart;
        r.end = defaultEnd;
        m_clip->entityRanges.push_back(r);
      }

      // Sync cache from clip ranges (or defaults)
      int32_t start = defaultStart;
      int32_t end = defaultEnd;
      for (auto &r : m_clip->entityRanges) {
        if (r.entity == e) {
          start = r.start;
          end = r.end;
          break;
        }
      }
      start = std::max(0, start);
      end = std::max(start, end);
      m_entityStartFrame[e] = start;
      m_entityEndFrame[e] = end;
    }
  }
}

void SequencerPanel::buildRows() {
  m_rows.clear();
  auto ensureStopwatchFromKeys = [this](EntityID e, SeqProperty p) {
    const uint64_t k = rowKey(e, SeqRowType::Property, p);
    if (m_stopwatchState.find(k) != m_stopwatchState.end())
      return;
    std::vector<int32_t> frames;
    m_stopwatchState[k] = findPropertyKeys(e, p, frames);
  };

  for (EntityID e : m_rowEntities) {
    const uint64_t layerKey =
        rowKey(e, SeqRowType::Layer, SeqProperty::Position);
    if (m_expandState.find(layerKey) == m_expandState.end())
      m_expandState[layerKey] = true;
    const bool expanded = m_expandState[layerKey];
    m_rows.push_back(
        SeqRow{SeqRowType::Layer, e, SeqProperty::Position, 0, expanded});

    if (!expanded)
      continue;

    // Transform group
    const uint64_t groupKey =
        rowKey(e, SeqRowType::Group, SeqProperty::Position);
    if (m_expandState.find(groupKey) == m_expandState.end())
      m_expandState[groupKey] = true;
    const bool transformExpanded = m_expandState[groupKey];
    m_rows.push_back(
        SeqRow{SeqRowType::Group, e, SeqProperty::Position, 1,
               transformExpanded});

    if (transformExpanded) {
      ensureStopwatchFromKeys(e, SeqProperty::Position);
      ensureStopwatchFromKeys(e, SeqProperty::Rotation);
      ensureStopwatchFromKeys(e, SeqProperty::Scale);
      ensureStopwatchFromKeys(e, SeqProperty::Opacity);
      m_rows.push_back(
          SeqRow{SeqRowType::Property, e, SeqProperty::Position, 2, false});
      m_rows.push_back(
          SeqRow{SeqRowType::Property, e, SeqProperty::Rotation, 2, false});
      m_rows.push_back(
          SeqRow{SeqRowType::Property, e, SeqProperty::Scale, 2, false});
      m_rows.push_back(
          SeqRow{SeqRowType::Property, e, SeqProperty::Opacity, 2, false});
    }

    m_rows.push_back(
        SeqRow{SeqRowType::Stub, e, SeqProperty::Audio, 1, false});
    m_rows.push_back(
        SeqRow{SeqRowType::Stub, e, SeqProperty::Masks, 1, false});
  }
}

void SequencerPanel::applyIsolation() {
  if (!m_world)
    return;
  const bool anyIso = !m_isolated.empty();
  for (EntityID e : m_world->alive()) {
    if (!m_world->isAlive(e))
      continue;
    if (m_hiddenExclude.find(e) != m_hiddenExclude.end())
      continue;
    auto &tr = m_world->transform(e);
    tr.hiddenEditor = anyIso && (m_isolated.find(e) == m_isolated.end());
  }
}

void SequencerPanel::onTransformEditEnd(EntityID e, uint32_t mask,
                                        const float *rotationEulerDeg) {
  if (!m_anim || !m_world)
    return;
  if (!m_world->isAlive(e))
    return;

  const int32_t frame = m_anim->frame();

  const bool nlaActive = !m_anim->strips().empty();
  if (nlaActive && m_nlaKeying.autoKey) {
    ActionID actionId = m_nlaKeyAction;
    if (actionId == 0) {
      for (const auto &s : m_anim->strips()) {
        if (s.target == e) {
          actionId = s.action;
          break;
        }
      }
    }
    AnimAction *a = m_anim->action(actionId);
    if (a) {
      const auto &tr = m_world->transform(e);
      if ((mask & EditTranslate) && m_nlaKeying.keyTranslate) {
        keyValue(*a, AnimChannel::TranslateX, frame, tr.translation.x,
                 m_nlaKeying.mode);
        keyValue(*a, AnimChannel::TranslateY, frame, tr.translation.y,
                 m_nlaKeying.mode);
        keyValue(*a, AnimChannel::TranslateZ, frame, tr.translation.z,
                 m_nlaKeying.mode);
      }
      if ((mask & EditRotate) && m_nlaKeying.keyRotate) {
        glm::vec3 deg = rotationEulerDeg
                            ? glm::vec3(rotationEulerDeg[0], rotationEulerDeg[1],
                                        rotationEulerDeg[2])
                            : glm::degrees(glm::eulerAngles(tr.rotation));
        keyValue(*a, AnimChannel::RotateX, frame, deg.x, m_nlaKeying.mode);
        keyValue(*a, AnimChannel::RotateY, frame, deg.y, m_nlaKeying.mode);
        keyValue(*a, AnimChannel::RotateZ, frame, deg.z, m_nlaKeying.mode);
      }
      if ((mask & EditScale) && m_nlaKeying.keyScale) {
        keyValue(*a, AnimChannel::ScaleX, frame, tr.scale.x, m_nlaKeying.mode);
        keyValue(*a, AnimChannel::ScaleY, frame, tr.scale.y, m_nlaKeying.mode);
        keyValue(*a, AnimChannel::ScaleZ, frame, tr.scale.z, m_nlaKeying.mode);
      }
      m_anim->setFrame(frame);
      return;
    }
  }

  if (!m_clip)
    return;

  auto shouldKey = [&](SeqProperty prop) -> bool {
    if (stopwatchEnabled(e, prop))
      return true;
    std::vector<int32_t> frames;
    return findPropertyKeys(e, prop, frames);
  };
  bool wrote = false;
  if ((mask & EditTranslate) && shouldKey(SeqProperty::Position))
    wrote |= addOrOverwritePropertyKeys(e, SeqProperty::Position, frame);
  if ((mask & EditRotate) && shouldKey(SeqProperty::Rotation))
    wrote |= addOrOverwritePropertyKeys(e, SeqProperty::Rotation, frame,
                                        rotationEulerDeg);
  if ((mask & EditScale) && shouldKey(SeqProperty::Scale))
    wrote |= addOrOverwritePropertyKeys(e, SeqProperty::Scale, frame);

  if (wrote)
    m_anim->setFrame(frame);
}

void SequencerPanel::setHiddenExclusions(const std::vector<EntityID> &ents) {
  m_hiddenExclude.clear();
  m_hiddenExclude.reserve(ents.size());
  for (EntityID e : ents) {
    if (e != InvalidEntity)
      m_hiddenExclude.insert(e);
  }
}

void SequencerPanel::setTrackExclusions(const std::vector<EntityID> &ents) {
  m_trackExclude.clear();
  m_trackExclude.reserve(ents.size());
  for (EntityID e : ents) {
    if (e != InvalidEntity)
      m_trackExclude.insert(e);
  }
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
  m_labelGutter = clampf(in.labelGutter, m_labelGutterMin, m_labelGutterMax);
  m_viewFirstFrame = std::max<int32_t>(0, in.viewFirstFrame);
  m_autoUpdateLastFrame = in.autoUpdateLastFrame;
  m_sortMode = (SeqSortMode)std::clamp(in.sortMode, 0, (int)SeqSortMode::Type);
  m_showGraphPanel = in.showGraphPanel;
  std::snprintf(m_searchBuf, sizeof(m_searchBuf), "%s", in.search.c_str());

  m_expandState.clear();
  m_stopwatchState.clear();
  m_selectedLayerBlocks.clear();
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
  r.blockId = (blockId != 0) ? blockId : std::max<uint32_t>(1u, m_clip->nextBlockId++);
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
  r.blockId = (blockId != 0) ? blockId : std::max<uint32_t>(1u, m_clip->nextBlockId++);
  r.start = clamped;
  r.end = std::max<int32_t>(clamped, entityEndFrame(e));
  m_clip->entityRanges.push_back(r);
  if (m_autoUpdateLastFrame)
    recomputeLastFrameFromKeys();
}

void SequencerPanel::drawTransportBar() {
  if (!m_anim || !m_clip) {
    ImGui::TextUnformatted("Sequencer: (no animation clip bound)");
    return;
  }

  if (!m_iconInit) {
    m_iconInit = true;
    const std::filesystem::path iconDir = Paths::engineRes() / "icons";
    const std::filesystem::path jsonPath =
        Paths::engineRes() / "icon_atlas.json";
    const std::filesystem::path pngPath = Paths::engineRes() / "icon_atlas.png";
    if (std::filesystem::exists(jsonPath) && std::filesystem::exists(pngPath)) {
      m_iconReady = m_iconAtlas.loadFromJson(jsonPath.string());
      if (m_iconReady) {
        if (!m_iconAtlas.find("clock") || !m_iconAtlas.find("hide") ||
            !m_iconAtlas.find("show")) {
          m_iconReady = m_iconAtlas.buildFromFolder(
              iconDir.string(), jsonPath.string(), pngPath.string(), 64, 0);
        }
      }
    } else {
      m_iconReady = m_iconAtlas.buildFromFolder(
          iconDir.string(), jsonPath.string(), pngPath.string(), 64, 0);
    }
  }

  const int32_t fpsFrames =
      std::max<int32_t>(1, (int32_t)std::round(m_anim->fps()));
  const int32_t frame = m_anim->frame();
  const int32_t secTotal = frame / fpsFrames;
  const int32_t frameInSec = frame % fpsFrames;
  const int32_t hours = secTotal / 3600;
  const int32_t mins = (secTotal / 60) % 60;
  const int32_t secs = secTotal % 60;

  char timeBuf[64];
  std::snprintf(timeBuf, sizeof(timeBuf), "%d:%02d:%02d:%02d", hours, mins,
                secs, frameInSec);
  ImGui::TextUnformatted(timeBuf);

  ImGui::SameLine();
  ImGui::Text("Frame: %d", frame);
  ImGui::SameLine();
  ImGui::Text("FPS: %.2f", m_anim->fps());

  ImGui::SameLine();
  ImGui::Checkbox("Auto Last", &m_autoUpdateLastFrame);

  ImGui::SameLine();
  int lastFrameInput = std::max(0, m_clip->lastFrame);
  ImGui::BeginDisabled(m_autoUpdateLastFrame);
  ImGui::SetNextItemWidth(120.0f);
  if (ImGui::InputInt("Last Frame", &lastFrameInput)) {
    m_clip->lastFrame = std::max(0, lastFrameInput);
    if (m_anim->frame() > m_clip->lastFrame)
      m_anim->setFrame(m_clip->lastFrame);
  }
  ImGui::EndDisabled();

  ImGui::SameLine();
  ImGui::SetNextItemWidth(180.0f);
  ImGui::InputTextWithHint("##SeqSearch", "Search layers", m_searchBuf,
                           sizeof(m_searchBuf));

  ImGui::SameLine();
  ImGui::SetNextItemWidth(140.0f);
  const char *sortItems[] = {"Scene", "Name A-Z", "Name Z-A", "Parent", "Type"};
  int sortIndex = (int)m_sortMode;
  if (ImGui::Combo("##SeqSort", &sortIndex, sortItems,
                   (int)(sizeof(sortItems) / sizeof(sortItems[0])))) {
    m_sortMode = (SeqSortMode)sortIndex;
  }

  ImGui::SameLine();
  if (ImGui::Button("Graph")) {
    m_showGraphPanel = !m_showGraphPanel;
  }

  ImGui::SameLine();
  ImGui::SetNextItemWidth(140.0f);
  ImGui::SliderFloat("Zoom", &m_pixelsPerFrame, m_minPixelsPerFrame, 40.0f,
                     "%.1f px/f");

  drawNlaControls();
}

void SequencerPanel::buildNlaFromClip() {
  if (!m_anim || !m_clip)
    return;

  m_anim->clearNla();
  if (!m_world)
    return;

  for (const auto &range : m_clip->entityRanges) {
    if (range.entity == InvalidEntity || !m_world->isAlive(range.entity))
      continue;

    AnimAction a{};
    if (m_world->isAlive(range.entity)) {
      a.name = m_world->name(range.entity).name + " [B" +
               std::to_string(range.blockId) + "]";
    } else {
      a.name = "Action B" + std::to_string(range.blockId);
    }

    a.start = range.start;
    a.end = range.end;
    for (const auto &t : m_clip->tracks) {
      if (t.entity != range.entity || t.blockId != range.blockId)
        continue;
      AnimActionTrack at{};
      at.channel = t.channel;
      at.curve = t.curve;
      a.tracks.push_back(std::move(at));
      if (!t.curve.keys.empty()) {
        a.start = std::min(a.start, t.curve.keys.front().frame);
        a.end = std::max(a.end, t.curve.keys.back().frame);
      }
    }

    if (a.tracks.empty())
      continue;

    const ActionID id = m_anim->createAction(std::move(a));
    NlaStrip s{};
    s.action = id;
    s.target = range.entity;
    s.start = range.start;
    s.end = range.end;
    const AnimAction *aa = m_anim->action(id);
    if (aa) {
      s.inFrame = aa->start;
      s.outFrame = aa->end;
    } else {
      s.inFrame = range.start;
      s.outFrame = range.end;
    }
    s.timeScale = 1.0f;
    s.reverse = false;
    s.blend = NlaBlendMode::Replace;
    s.influence = 1.0f;
    s.layer = 0;
    s.muted = false;
    m_anim->addStrip(s);
  }

  m_anim->setFrame(m_anim->frame());
}

void SequencerPanel::drawNlaControls() {
  if (!m_anim)
    return;
  if (!ImGui::CollapsingHeader("NLA", ImGuiTreeNodeFlags_DefaultOpen))
    return;

  const auto &stripsConst = m_anim->strips();
  const auto &actionsConst = m_anim->actions();
  ImGui::Text("Actions: %d  Strips: %d", (int)actionsConst.size(),
              (int)stripsConst.size());
  ImGui::SameLine();
  ImGui::TextDisabled(stripsConst.empty() ? "(Clip mode)" : "(NLA mode)");

  if (ImGui::Button("Build NLA From Clip")) {
    buildNlaFromClip();
  }
  ImGui::SameLine();
  if (ImGui::Button("Clear NLA")) {
    m_anim->clearNla();
    m_anim->setFrame(m_anim->frame());
  }

  if (!actionsConst.empty()) {
    ImGui::SeparatorText("Keying");
    int actionIdx = (m_nlaKeyAction > 0) ? (int)m_nlaKeyAction - 1 : 0;
    actionIdx = std::clamp(actionIdx, 0, (int)actionsConst.size() - 1);
    ImGui::SetNextItemWidth(220.0f);
    const char *preview = actionsConst[(size_t)actionIdx].name.c_str();
    if (ImGui::BeginCombo("Target Action", preview)) {
      for (int i = 0; i < (int)actionsConst.size(); ++i) {
        const bool sel = i == actionIdx;
        const char *label = actionsConst[(size_t)i].name.c_str();
        if (ImGui::Selectable(label, sel)) {
          actionIdx = i;
          m_nlaKeyAction = (ActionID)(i + 1);
        }
        if (sel)
          ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }
    if (m_nlaKeyAction == 0)
      m_nlaKeyAction = (ActionID)(actionIdx + 1);

    ImGui::Checkbox("Auto Key (NLA)", &m_nlaKeying.autoKey);
    ImGui::SameLine();
    ImGui::Checkbox("T", &m_nlaKeying.keyTranslate);
    ImGui::SameLine();
    ImGui::Checkbox("R", &m_nlaKeying.keyRotate);
    ImGui::SameLine();
    ImGui::Checkbox("S", &m_nlaKeying.keyScale);
    ImGui::SameLine();
    int mode = (m_nlaKeying.mode == KeyingMode::Add) ? 1 : 0;
    ImGui::SetNextItemWidth(110.0f);
    if (ImGui::Combo("Mode", &mode, "Replace\0Add\0")) {
      m_nlaKeying.mode = (mode == 1) ? KeyingMode::Add : KeyingMode::Replace;
    }
  }

  auto &strips = m_anim->strips();
  if (strips.empty())
    return;

  ImGui::SeparatorText("Strips");
  for (int i = 0; i < (int)strips.size(); ++i) {
    ImGui::PushID(i);
    NlaStrip &s = strips[(size_t)i];
    const AnimAction *a = m_anim->action(s.action);
    const char *aname = (a && !a->name.empty()) ? a->name.c_str() : "Action";
    const char *tname = "Entity";
    if (m_world && m_world->isAlive(s.target))
      tname = m_world->name(s.target).name.c_str();

    ImGui::Text("%s -> %s", aname, tname);
    ImGui::SameLine();
    if (ImGui::SmallButton("Delete")) {
      m_anim->removeStrip((uint32_t)i);
      ImGui::PopID();
      m_anim->setFrame(m_anim->frame());
      break;
    }

    int start = s.start;
    int end = s.end;
    int inFrame = s.inFrame;
    int outFrame = s.outFrame;
    int layer = s.layer;
    float influence = s.influence;
    float timeScale = s.timeScale;
    bool reverse = s.reverse;
    bool muted = s.muted;
    int blend = (int)s.blend;

    bool changed = false;
    ImGui::SetNextItemWidth(90.0f);
    changed |= ImGui::InputInt("Start", &start);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    changed |= ImGui::InputInt("End", &end);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    changed |= ImGui::InputInt("Layer", &layer);

    ImGui::SetNextItemWidth(90.0f);
    changed |= ImGui::InputInt("In", &inFrame);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    changed |= ImGui::InputInt("Out", &outFrame);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    changed |= ImGui::DragFloat("Influence", &influence, 0.01f, 0.0f, 1.0f);

    ImGui::SetNextItemWidth(120.0f);
    changed |= ImGui::DragFloat("TimeScale", &timeScale, 0.01f, 0.01f, 32.0f);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Reverse", &reverse);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Mute", &muted);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110.0f);
    const char *blendItems[] = {"Replace", "Add"};
    changed |= ImGui::Combo("Blend", &blend, blendItems, 2);

    if (changed) {
      s.start = std::max(0, start);
      s.end = std::max(s.start, end);
      s.inFrame = std::max(0, inFrame);
      s.outFrame = std::max(s.inFrame, outFrame);
      s.layer = layer;
      s.influence = std::clamp(influence, 0.0f, 1.0f);
      s.timeScale = std::max(0.01f, timeScale);
      s.reverse = reverse;
      s.muted = muted;
      s.blend = (blend == 1) ? NlaBlendMode::Add : NlaBlendMode::Replace;
      m_anim->setFrame(m_anim->frame());
    }

    ImGui::Separator();
    ImGui::PopID();
  }
}

void SequencerPanel::drawMarkers(const ImRect &r, int32_t firstFrame,
                                 int32_t lastFrame) {
  ImDrawList *dl = ImGui::GetWindowDrawList();

  // Top marker lane
  const float laneH = m_rulerHeight;
  ImRect lane = r;
  lane.Max.y = std::min(r.Max.y, r.Min.y + laneH);

  const float xStart = r.Min.x + m_labelGutter;

  for (const auto &m : m_markers) {
    if (m.frame < firstFrame || m.frame > lastFrame)
      continue;

    const float x = frameToX(m.frame, firstFrame, xStart);
    dl->AddLine(ImVec2(x, lane.Min.y), ImVec2(x, lane.Max.y),
                IM_COL32(255, 220, 80, 255), 2.0f);
    if (!m.label.empty()) {
      dl->AddText(ImVec2(x + 4.0f, lane.Min.y + 2.0f),
                  IM_COL32(255, 220, 80, 255), m.label.c_str());
    }
  }
}

void SequencerPanel::drawKeysAndTracks(const ImRect &r, int32_t firstFrame,
                                       int32_t lastFrame) {
  if (!m_clip)
    return;

  ImDrawList *dl = ImGui::GetWindowDrawList();

  // Track area
  const float laneH = m_rulerHeight;
  ImRect tracks = r;
  tracks.Min.y += laneH;

  const float xStart = tracks.Min.x + m_labelGutter;
  // Prevent key circles/lines from drawing outside the timeline track viewport.
  const ImVec2 clipMin(xStart, tracks.Min.y);
  const ImVec2 clipMax(tracks.Max.x, tracks.Max.y);
  dl->PushClipRect(clipMin, clipMax, true);
  const bool nlaActive = (m_anim && !m_anim->strips().empty());

  for (int row = 0; row < (int)m_rows.size(); ++row) {
    const float y0 = tracks.Min.y + float(row) * m_rowHeight;
    const float y1 = y0 + m_rowHeight;
    if (y0 > tracks.Max.y)
      break;

    const SeqRow &rrow = m_rows[(size_t)row];
    if (rrow.type == SeqRowType::Layer) {
      const EntityID e = rrow.entity;
      if (nlaActive) {
        const auto &strips = m_anim->strips();
        for (int si = 0; si < (int)strips.size(); ++si) {
          const auto &s = strips[(size_t)si];
          if (s.target != e)
            continue;
          const int32_t startF = s.start;
          const int32_t endF = s.end;
          if (endF < firstFrame || startF > lastFrame)
            continue;
          const float x0 = frameToX(startF, firstFrame, xStart);
          const float x1 = frameToX(endF + 1, firstFrame, xStart);
          const ImRect blockRect(ImVec2(x0, y0 + 2.0f), ImVec2(x1, y1 - 2.0f));
          const bool blockHovered =
              ImGui::IsMouseHoveringRect(blockRect.Min, blockRect.Max);
          const uint32_t sid = nlaSelectIdFromIndex(si);
          const bool blockSelected =
              (m_selectedRangeBlocks.find(sid) != m_selectedRangeBlocks.end());
          ImU32 col = layerColor(e);
          if (blockSelected)
            col = brightenColor(col, 1.45f);
          else if (blockHovered)
            col = brightenColor(col, 1.2f);
          dl->AddRectFilled(blockRect.Min, blockRect.Max, col);
          dl->AddLine(ImVec2(x0, y0 + 2.0f), ImVec2(x0, y1 - 2.0f),
                      IM_COL32(255, 255, 255, 80), 2.0f);
          dl->AddLine(ImVec2(x1, y0 + 2.0f), ImVec2(x1, y1 - 2.0f),
                      IM_COL32(255, 255, 255, 80), 2.0f);
          if (blockSelected) {
            dl->AddRect(blockRect.Min, blockRect.Max,
                        IM_COL32(255, 245, 180, 220), 0.0f, 0, 2.0f);
          }
        }
      } else {
        for (const auto &er : m_clip->entityRanges) {
          if (er.entity != e)
            continue;
          const int32_t startF = er.start;
          const int32_t endF = er.end;
          if (endF < firstFrame || startF > lastFrame)
            continue;
          const float x0 = frameToX(startF, firstFrame, xStart);
          const float x1 = frameToX(endF + 1, firstFrame, xStart);
          const ImRect blockRect(ImVec2(x0, y0 + 2.0f), ImVec2(x1, y1 - 2.0f));
          const bool blockHovered =
              ImGui::IsMouseHoveringRect(blockRect.Min, blockRect.Max);
          const bool blockSelected =
              (m_selectedRangeBlocks.find(er.blockId) !=
               m_selectedRangeBlocks.end());
          ImU32 col = layerColor(e);
          if (blockSelected)
            col = brightenColor(col, 1.45f);
          else if (blockHovered)
            col = brightenColor(col, 1.2f);
          dl->AddRectFilled(blockRect.Min, blockRect.Max, col);
          dl->AddLine(ImVec2(x0, y0 + 2.0f), ImVec2(x0, y1 - 2.0f),
                      IM_COL32(255, 255, 255, 80), 2.0f);
          dl->AddLine(ImVec2(x1, y0 + 2.0f), ImVec2(x1, y1 - 2.0f),
                      IM_COL32(255, 255, 255, 80), 2.0f);
          if (blockSelected) {
            dl->AddRect(blockRect.Min, blockRect.Max,
                        IM_COL32(255, 245, 180, 220), 0.0f, 0, 2.0f);
          }
        }
      }
    } else if (rrow.type == SeqRowType::Property) {
      std::vector<int32_t> frames;
      if (findPropertyKeys(rrow.entity, rrow.prop, frames)) {
        AnimChannel propCh[3];
        propertyChannels(rrow.prop, propCh);
        auto isSelectedFrame = [&](int32_t frame) -> bool {
          for (const auto &sel : m_selectedKeys) {
            if (sel.trackIndex < 0 || sel.trackIndex >= (int)m_clip->tracks.size())
              continue;
            const auto &t = m_clip->tracks[(size_t)sel.trackIndex];
            if (t.entity != rrow.entity)
              continue;
            bool channelMatch = false;
            for (int ci = 0; ci < 3; ++ci) {
              if (t.channel == propCh[ci]) {
                channelMatch = true;
                break;
              }
            }
            if (!channelMatch)
              continue;
            if (sel.keyIndex < 0 || sel.keyIndex >= (int)t.curve.keys.size())
              continue;
            if ((int32_t)t.curve.keys[(size_t)sel.keyIndex].frame == frame)
              return true;
          }
          return false;
        };
        for (int32_t f : frames) {
          if (f < firstFrame || f > lastFrame)
            continue;
          const float x = frameToX(f, firstFrame, xStart);
          const float cy = (y0 + y1) * 0.5f;
          const bool selected = isSelectedFrame(f);
          const float r = selected ? 4.8f : 4.0f;
          ImVec2 p0(x, cy - r);
          ImVec2 p1(x + r, cy);
          ImVec2 p2(x, cy + r);
          ImVec2 p3(x - r, cy);
          dl->AddQuadFilled(
              p0, p1, p2, p3,
              selected ? IM_COL32(255, 238, 170, 255)
                       : IM_COL32(230, 230, 230, 255));
          dl->AddQuad(
              p0, p1, p2, p3,
              selected ? IM_COL32(255, 170, 60, 255) : IM_COL32(60, 60, 60, 255),
              selected ? 2.0f : 1.0f);
        }
      }
    }
  }

  // Current frame line
  const float frameX = frameToX((int32_t)m_anim->frame(), firstFrame, xStart);
  dl->AddLine(ImVec2(frameX, r.Min.y), ImVec2(frameX, r.Max.y),
              IM_COL32(255, 80, 80, 255), 2.0f);
  dl->PopClipRect();
}

void SequencerPanel::drawTimeline() {
  if (!m_anim || !m_clip)
    return;

  ensureTracksForWorld();
  buildRowEntities();
  buildRows();
  applyIsolation();

  updateHiddenEntities();

  // Timeline rect
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  const float height = std::min(avail.y, m_timelineHeight);
  const float contentH =
      std::max(height, m_rulerHeight + m_rowHeight * (float)m_rows.size());

  ImVec2 p0 = ImGui::GetCursorScreenPos();
  ImVec2 p1 = ImVec2(p0.x + avail.x, p0.y + contentH);

  ImRect r(p0, p1);

  // Frame window visible
  const int32_t lastFrame = std::max<int32_t>(0, m_clip->lastFrame);

  // Dynamic min zoom so the whole clip fits in view
  const float timelineW =
      std::max(1.0f, (r.Max.x - r.Min.x) - m_labelGutter);
  m_minPixelsPerFrame =
      std::max(1.0f, timelineW / std::max(1, (int)lastFrame + 1));
  if (m_pixelsPerFrame < m_minPixelsPerFrame)
    m_pixelsPerFrame = m_minPixelsPerFrame;

  const float usableW =
      std::max(1.0f, (r.Max.x - r.Min.x) - m_labelGutter);
  const int32_t framesVisible =
      std::max<int32_t>(1, (int32_t)(usableW / m_pixelsPerFrame));
  const int32_t maxFirstFrame = std::max(0, lastFrame - framesVisible);
  m_viewFirstFrame = clampi(m_viewFirstFrame, 0, maxFirstFrame);
  int32_t firstFrame = m_viewFirstFrame;
  int32_t lastVisible =
      std::min(lastFrame, firstFrame + std::max<int32_t>(0, framesVisible - 1));

  // Background
  ImDrawList *dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(r.Min, r.Max, IM_COL32(10, 10, 10, 255));
  dl->AddRect(r.Min, r.Max, IM_COL32(70, 70, 70, 255));

  // Ruler + tracks background
  const float rulerH = m_rulerHeight;
  ImRect ruler = r;
  ruler.Max.y = std::min(r.Max.y, r.Min.y + rulerH);
  dl->AddRectFilled(ruler.Min, ruler.Max, IM_COL32(18, 18, 18, 255));
  dl->AddRect(ruler.Min, ruler.Max, IM_COL32(60, 60, 60, 255));

  ImRect tracks = r;
  tracks.Min.y += rulerH;
  dl->AddRectFilled(tracks.Min, tracks.Max, IM_COL32(12, 12, 12, 255));
  dl->AddRect(tracks.Min, tracks.Max, IM_COL32(55, 55, 55, 255));

  // Grid lines + time labels
  const float xStart = r.Min.x + m_labelGutter;
  const int32_t fpsFrames =
      std::max<int32_t>(1, (int32_t)std::round(m_anim->fps()));

  // Alternating row background
  for (int row = 0; row < (int)m_rows.size(); ++row) {
    const float y0 = tracks.Min.y + float(row) * m_rowHeight;
    const float y1 = y0 + m_rowHeight;
    if (y0 > tracks.Max.y)
      break;
    if ((row & 1) == 0) {
      dl->AddRectFilled(ImVec2(tracks.Min.x, y0), ImVec2(tracks.Max.x, y1),
                        IM_COL32(14, 14, 14, 255));
    }
  }

  // Adaptive ruler steps
  const float minLabelPx = 70.0f;
  float stepFrames = 1.0f;
  const float fpsF = (float)fpsFrames;
  const float stepCandidates[] = {1,   2,   5,   10,  0.25f * fpsF,
                                  0.5f * fpsF, 1.0f * fpsF, 2.0f * fpsF,
                                  5.0f * fpsF, 10.0f * fpsF, 30.0f * fpsF,
                                  60.0f * fpsF, 120.0f * fpsF,
                                  300.0f * fpsF};
  for (float s : stepCandidates) {
    if (s < 1.0f)
      continue;
    if (s * m_pixelsPerFrame >= minLabelPx) {
      stepFrames = s;
      break;
    }
    stepFrames = s;
  }

  const int32_t stepI = std::max(1, (int32_t)std::round(stepFrames));
  const int32_t firstStep = (firstFrame / stepI) * stepI;
  for (int32_t f = firstStep; f <= lastVisible; f += stepI) {
    const float x = frameToX(f, firstFrame, xStart);
    dl->AddLine(ImVec2(x, r.Min.y), ImVec2(x, r.Max.y),
                IM_COL32(35, 35, 35, 255), 1.0f);

    double seconds = (double)f / (double)fpsFrames;
    char buf[64];
    if (stepFrames >= fpsFrames * 60.0f) {
      const int total = (int)seconds;
      const int mm = total / 60;
      const int ss = total % 60;
      std::snprintf(buf, sizeof(buf), "%d:%02d", mm, ss);
    } else if (stepFrames >= fpsFrames) {
      std::snprintf(buf, sizeof(buf), "%.0f s", seconds);
    } else {
      std::snprintf(buf, sizeof(buf), "%.2f s", seconds);
    }
    dl->AddText(ImVec2(x + 2.0f, r.Min.y + 2.0f),
                IM_COL32(140, 140, 140, 255), buf);
  }

  // Markers lane + tracks
  drawMarkers(r, firstFrame, lastVisible);
  drawKeysAndTracks(r, firstFrame, lastVisible);

  // Label gutter overlay (does not shift timeline)
  if (m_labelGutter > 0.0f) {
    ImRect tracks = r;
    tracks.Min.y += rulerH;

    const float gx0 = r.Min.x;
    const float gx1 = std::min(r.Max.x, r.Min.x + m_labelGutter);
    if (gx1 > gx0) {
      dl->AddRectFilled(ImVec2(gx0, tracks.Min.y), ImVec2(gx1, tracks.Max.y),
                        IM_COL32(12, 12, 12, 230));
      dl->AddLine(ImVec2(gx1, tracks.Min.y), ImVec2(gx1, tracks.Max.y),
                  IM_COL32(55, 55, 55, 255));

      ImVec2 clipMin(gx0, tracks.Min.y);
      ImVec2 clipMax(gx1, tracks.Max.y);
      dl->PushClipRect(clipMin, clipMax, true);

      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 2.0f));
      const float iconW = 16.0f;
      const float iconGap = 4.0f;
      const float indentW = 12.0f;
      const float textPad = 4.0f;
      const float labelStartX =
          gx0 + 4.0f + (iconW + iconGap) * 3.0f + textPad;
      for (int row = 0; row < (int)m_rows.size(); ++row) {
        const SeqRow &rr = m_rows[(size_t)row];
        const float y0 = tracks.Min.y + float(row) * m_rowHeight;
        const float y1 = y0 + m_rowHeight;
        if (y0 > tracks.Max.y)
          break;

        ImGui::PushID(row);
        const float baseX = gx0 + 4.0f + rr.depth * indentW;
        ImGui::SetCursorScreenPos(ImVec2(baseX, y0 + 2.0f));

        if (rr.type == SeqRowType::Layer) {
          bool expanded =
              m_expandState[rowKey(rr.entity, SeqRowType::Layer,
                                   SeqProperty::Position)];
          if (ImGui::SmallButton(expanded ? "v" : ">")) {
            expanded = !expanded;
            m_expandState[rowKey(rr.entity, SeqRowType::Layer,
                                 SeqProperty::Position)] = expanded;
          }
          ImGui::SameLine();
          bool hidden = isLayerHidden(rr.entity);
          if (m_iconReady) {
            if (drawAtlasIconButton(
                    m_iconAtlas, hidden ? "hide" : "show",
                    ImVec2(14.0f, 14.0f), IM_COL32(255, 255, 255, 255))) {
              if (m_world && m_world->isAlive(rr.entity))
                m_world->transform(rr.entity).hidden = !hidden;
            }
          } else {
            if (ImGui::SmallButton(hidden ? "o" : "O")) {
              if (m_world && m_world->isAlive(rr.entity))
                m_world->transform(rr.entity).hidden = !hidden;
            }
          }
          ImGui::SameLine();
          const bool iso = (m_isolated.find(rr.entity) != m_isolated.end());
          const ImVec2 isoSize(16.0f, 16.0f);
          ImGui::InvisibleButton("##iso", isoSize);
          if (ImGui::IsItemClicked()) {
            if (iso)
              m_isolated.erase(rr.entity);
            else
              m_isolated.insert(rr.entity);
          }
          {
            ImDrawList *dl = ImGui::GetWindowDrawList();
            const ImVec2 p0 = ImGui::GetItemRectMin();
            const ImVec2 p1 = ImGui::GetItemRectMax();
            const ImVec2 c((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);
            const float r = 6.0f;
            const ImU32 fill = iso ? IM_COL32(255, 200, 80, 220)
                                   : IM_COL32(80, 80, 80, 200);
            const ImU32 line = IM_COL32(200, 200, 200, 200);
            dl->AddCircleFilled(c, r, fill);
            dl->AddCircle(c, r, line, 0, 1.5f);
          }
          // Align label to a fixed column to avoid overlap with icons
          ImGui::SetCursorScreenPos(
              ImVec2(labelStartX + rr.depth * indentW, y0 + 2.0f));
          const char *ename = "Entity";
          if (m_world && m_world->isAlive(rr.entity))
            ename = m_world->name(rr.entity).name.c_str();
          ImGui::TextUnformatted(ename);
          ImGui::SameLine();
          EntityID p = m_world ? m_world->parentOf(rr.entity) : InvalidEntity;
          if (m_world && p != InvalidEntity && m_world->isAlive(p)) {
            ImGui::TextDisabled("(%s)", m_world->name(p).name.c_str());
          }
        } else if (rr.type == SeqRowType::Group) {
          bool expanded =
              m_expandState[rowKey(rr.entity, SeqRowType::Group,
                                   SeqProperty::Position)];
          if (ImGui::SmallButton(expanded ? "v" : ">")) {
            expanded = !expanded;
            m_expandState[rowKey(rr.entity, SeqRowType::Group,
                                 SeqProperty::Position)] = expanded;
          }
          ImGui::SetCursorScreenPos(
              ImVec2(labelStartX + rr.depth * indentW, y0 + 2.0f));
          ImGui::TextUnformatted("Transform");
        } else if (rr.type == SeqRowType::Property) {
          const bool sw = stopwatchEnabled(rr.entity, rr.prop);
          if (m_iconReady) {
            if (drawAtlasIconButton(m_iconAtlas, "clock",
                                    ImVec2(14.0f, 14.0f),
                                    sw ? IM_COL32(255, 220, 120, 255)
                                       : IM_COL32(120, 120, 120, 255))) {
              const bool applyMulti =
                  m_selectedLayerBlocks.size() > 1 &&
                  m_selectedLayerBlocks.find(rr.entity) !=
                      m_selectedLayerBlocks.end();
              if (applyMulti) {
                for (EntityID eSel : m_selectedLayerBlocks)
                  setStopwatch(eSel, rr.prop, !sw);
              } else {
                setStopwatch(rr.entity, rr.prop, !sw);
              }
            }
          } else {
            if (ImGui::SmallButton(sw ? "O" : "o")) {
              const bool applyMulti =
                  m_selectedLayerBlocks.size() > 1 &&
                  m_selectedLayerBlocks.find(rr.entity) !=
                      m_selectedLayerBlocks.end();
              if (applyMulti) {
                for (EntityID eSel : m_selectedLayerBlocks)
                  setStopwatch(eSel, rr.prop, !sw);
              } else {
                setStopwatch(rr.entity, rr.prop, !sw);
              }
            }
          }
          if (sw) {
            ImGui::SameLine();
            if (ImGui::SmallButton("<")) {
              std::vector<int32_t> frames;
              if (findPropertyKeys(rr.entity, rr.prop, frames)) {
                const int32_t curF = m_anim->frame();
                int32_t best = -1;
                for (int32_t f : frames) {
                  if (f < curF)
                    best = f;
                  else
                    break;
                }
                if (best >= 0)
                  m_anim->setFrame(best);
              }
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(">")) {
              std::vector<int32_t> frames;
              if (findPropertyKeys(rr.entity, rr.prop, frames)) {
                const int32_t curF = m_anim->frame();
                for (int32_t f : frames) {
                  if (f > curF) {
                    m_anim->setFrame(f);
                    break;
                  }
                }
              }
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("*")) {
              const int32_t f = m_anim->frame();
              std::vector<int32_t> frames;
              if (findPropertyKeys(rr.entity, rr.prop, frames) &&
                  std::find(frames.begin(), frames.end(), f) != frames.end()) {
                deletePropertyKeysAtFrame(rr.entity, rr.prop, f);
              } else {
                addOrOverwritePropertyKeys(rr.entity, rr.prop, f);
              }
            }
          }
          ImGui::SetCursorScreenPos(
              ImVec2(labelStartX + rr.depth * indentW, y0 + 2.0f));
          const char *label = "Property";
          switch (rr.prop) {
          case SeqProperty::Position:
            label = "Position";
            break;
          case SeqProperty::Rotation:
            label = "Rotation";
            break;
          case SeqProperty::Scale:
            label = "Scale";
            break;
          case SeqProperty::Opacity:
            label = "Opacity";
            break;
          default:
            break;
          }
          if (ImGui::Selectable(label, false, ImGuiSelectableFlags_AllowDoubleClick,
                                ImVec2(96.0f, 0.0f))) {
            m_selectedKeys.clear();
            m_activeKey = SeqKeyRef{};
            if (m_clip) {
              if (!propertyHasAnimChannels(rr.prop)) {
                m_graphTrackIndex = -1;
                ImGui::PopID();
                continue;
              }
              AnimChannel ch[3];
              propertyChannels(rr.prop, ch);
              const uint32_t blockId = resolveTargetBlock(rr.entity);
              for (int ti = 0; ti < (int)m_clip->tracks.size(); ++ti) {
                const auto &t = m_clip->tracks[(size_t)ti];
                if (t.entity != rr.entity || t.blockId != blockId)
                  continue;
                bool channelMatch = false;
                for (int ci = 0; ci < 3; ++ci) {
                  if (t.channel == ch[ci]) {
                    channelMatch = true;
                    break;
                  }
                }
                if (!channelMatch)
                  continue;
                for (int ki = 0; ki < (int)t.curve.keys.size(); ++ki)
                  m_selectedKeys.push_back(SeqKeyRef{ti, ki});
              }
              m_graphTrackIndex = graphTrackForPropertyBest(rr.entity, rr.prop);
              if (!m_selectedKeys.empty())
                m_activeKey = m_selectedKeys.front();
            }
          }
        } else if (rr.type == SeqRowType::Stub) {
          const char *label = (rr.prop == SeqProperty::Audio) ? "Audio" : "Masks";
          ImGui::TextDisabled("%s", label);
        }

        ImGui::PopID();
      }
      ImGui::PopStyleVar();

      dl->PopClipRect();
    }
  }

  // Reset cursor so interaction button covers the timeline properly
  ImGui::SetCursorScreenPos(p0);

  // Gutter resize handle (drag to resize; double-click to toggle)
  {
    const float handleX = r.Min.x + m_labelGutter;
    const float handlePad = 4.0f;
    const ImRect handle(ImVec2(handleX - handlePad, r.Min.y),
                        ImVec2(handleX + handlePad, r.Max.y));
    const ImVec2 mp = ImGui::GetMousePos();
    const bool handleHover =
        mp.x >= handle.Min.x && mp.x <= handle.Max.x && mp.y >= handle.Min.y &&
        mp.y <= handle.Max.y;

    if (handleHover && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
      m_labelGutterDragging = true;
    if (m_labelGutterDragging && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      m_labelGutter += ImGui::GetIO().MouseDelta.x;
      m_labelGutter = clampf(m_labelGutter, m_labelGutterMin, m_labelGutterMax);
    } else {
      m_labelGutterDragging = false;
    }

    if (handleHover && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
      m_labelGutter = (m_labelGutter > 1.0f) ? 0.0f : 200.0f;
    }

    if (handleHover || m_labelGutterDragging) {
      dl->AddLine(ImVec2(handleX, r.Min.y), ImVec2(handleX, r.Max.y),
                  IM_COL32(120, 120, 120, 255), 2.0f);
    }
  }

  // Interaction area
  ImGui::InvisibleButton("##SequencerTimeline", ImVec2(avail.x, contentH),
                         ImGuiButtonFlags_MouseButtonLeft |
                             ImGuiButtonFlags_MouseButtonRight |
                             ImGuiButtonFlags_MouseButtonMiddle);

  const bool active = ImGui::IsItemActive();
  const bool hovered = ImGui::IsItemHovered();
  m_timelineHovered = hovered;
  m_timelineActive = active;

  const ImVec2 mp = ImGui::GetMousePos();
  const float interactionX0 = r.Min.x + m_labelGutter;
  const bool ctrl = ImGui::GetIO().KeyCtrl;
  const bool shift = ImGui::GetIO().KeyShift;
  const bool nlaActive = (m_anim && !m_anim->strips().empty());
  auto hitLayerRange = [this, &r, rulerH, interactionX0, &mp, firstFrame, xStart](
                           EntityID &outEntity, int &outRangeIndex,
                           int32_t &outStartF, int32_t &outEndF, float &outStartX,
                           float &outEndX) -> bool {
    outEntity = InvalidEntity;
    outRangeIndex = -1;
    outStartF = 0;
    outEndF = 0;
    outStartX = 0.0f;
    outEndX = 0.0f;
    const float tracksTop = r.Min.y + rulerH;
    const int row = (int)((mp.y - tracksTop) / m_rowHeight);
    if (row < 0 || row >= (int)m_rows.size())
      return false;
    const SeqRow &rr = m_rows[(size_t)row];
    if (rr.type != SeqRowType::Layer)
      return false;
    if ((!m_clip && (!m_anim || m_anim->strips().empty())) ||
        mp.x < interactionX0 || mp.x > r.Max.x)
      return false;
    if (m_anim && !m_anim->strips().empty()) {
      const auto &strips = m_anim->strips();
      for (int i = 0; i < (int)strips.size(); ++i) {
        const auto &s = strips[(size_t)i];
        if (s.target != rr.entity)
          continue;
        const float sx = frameToX(s.start, firstFrame, xStart);
        const float ex = frameToX(s.end + 1, firstFrame, xStart);
        if (mp.x >= sx && mp.x <= ex) {
          outEntity = rr.entity;
          outRangeIndex = i;
          outStartF = s.start;
          outEndF = s.end;
          outStartX = sx;
          outEndX = ex;
          return true;
        }
      }
      return false;
    }
    for (int i = 0; i < (int)m_clip->entityRanges.size(); ++i) {
      const auto &er = m_clip->entityRanges[(size_t)i];
      if (er.entity != rr.entity)
        continue;
      const float sx = frameToX(er.start, firstFrame, xStart);
      const float ex = frameToX(er.end + 1, firstFrame, xStart);
      if (mp.x >= sx && mp.x <= ex) {
        outEntity = rr.entity;
        outRangeIndex = i;
        outStartF = er.start;
        outEndF = er.end;
        outStartX = sx;
        outEndX = ex;
        return true;
      }
    }
    return false;
  };
  auto cutLayerRangeAtFrame = [this](int rangeIndex, int32_t cutFrame) -> bool {
    if (m_anim && !m_anim->strips().empty()) {
      if (rangeIndex < 0 || rangeIndex >= (int)m_anim->strips().size())
        return false;
      auto &s = m_anim->strips()[(size_t)rangeIndex];
      if (cutFrame < s.start || cutFrame >= s.end)
        return false;
      NlaStrip right = s;
      right.start = cutFrame + 1;
      s.end = cutFrame;
      m_anim->strips().insert(m_anim->strips().begin() + (ptrdiff_t)rangeIndex + 1,
                              right);
      return true;
    }
    if (!m_clip)
      return false;
    if (rangeIndex < 0 || rangeIndex >= (int)m_clip->entityRanges.size())
      return false;
    auto &r = m_clip->entityRanges[(size_t)rangeIndex];
    if (cutFrame < r.start || cutFrame >= r.end)
      return false;
    const uint32_t srcBlock = r.blockId;
    uint32_t newBlock = std::max<uint32_t>(1u, m_clip->nextBlockId++);
    for (const auto &er : m_clip->entityRanges)
      newBlock = std::max(newBlock, er.blockId + 1);
    for (const auto &t : m_clip->tracks)
      newBlock = std::max(newBlock, t.blockId + 1);
    m_clip->nextBlockId = std::max<uint32_t>(m_clip->nextBlockId, newBlock + 1);
    AnimEntityRange right = r;
    right.blockId = newBlock;
    right.start = cutFrame + 1;
    r.end = cutFrame;
    m_clip->entityRanges.insert(m_clip->entityRanges.begin() + (ptrdiff_t)rangeIndex + 1,
                                right);
    const size_t trackCount = m_clip->tracks.size();
    for (size_t i = 0; i < trackCount; ++i) {
      const auto &t = m_clip->tracks[i];
      if (t.entity != r.entity || t.blockId != srcBlock)
        continue;
      AnimTrack nt = t;
      nt.blockId = newBlock;
      m_clip->tracks.push_back(std::move(nt));
    }
    for (uint8_t p = (uint8_t)SeqProperty::Position;
         p <= (uint8_t)SeqProperty::Opacity; ++p) {
      const uint64_t srcKey = ((uint64_t)srcBlock << 32) | (uint64_t)p;
      auto it = m_stopwatchState.find(srcKey);
      if (it == m_stopwatchState.end())
        continue;
      const uint64_t dstKey = ((uint64_t)newBlock << 32) | (uint64_t)p;
      m_stopwatchState[dstKey] = it->second;
    }
    m_rangeUserEdited.insert(r.entity);
    return true;
  };

  if (hovered) {
    ImGuiIO &io = ImGui::GetIO();
    if (io.KeyAlt && io.MouseWheel != 0.0f) {
      const float zoom = (io.MouseWheel > 0.0f) ? 1.1f : 0.9f;
      m_pixelsPerFrame *= zoom;
      if (m_pixelsPerFrame < m_minPixelsPerFrame)
        m_pixelsPerFrame = m_minPixelsPerFrame;
      const int32_t maxFirstAfterZoom = std::max(0, lastFrame - framesVisible);
      m_viewFirstFrame = clampi(m_viewFirstFrame, 0, maxFirstAfterZoom);
    } else {
      float scroll = 0.0f;
      if (io.MouseWheelH != 0.0f) {
        scroll = io.MouseWheelH;
      } else if (io.KeyShift && io.MouseWheel != 0.0f) {
        scroll = io.MouseWheel;
      }
      if (scroll != 0.0f) {
        const int32_t step = std::max<int32_t>(1, framesVisible / 10);
        m_viewFirstFrame -= (int32_t)std::round(scroll * (float)step);
        m_viewFirstFrame = clampi(m_viewFirstFrame, 0, maxFirstFrame);
      }
    }
  }

  if (hovered) {
    if (ImGui::IsKeyPressed(ImGuiKey_Escape))
      m_cutToolActive = false;
    if (ImGui::IsKeyPressed(ImGuiKey_Delete) ||
        ImGui::IsKeyPressed(ImGuiKey_Backspace) ||
        ImGui::IsKeyPressed(ImGuiKey_X)) {
      if (!m_selectedKeys.empty()) {
        deleteSelectedKeys();
      } else if (!m_selectedRangeBlocks.empty() && m_clip) {
        if (nlaActive) {
          for (int i = (int)m_anim->strips().size() - 1; i >= 0; --i) {
            const uint32_t sid = nlaSelectIdFromIndex(i);
            if (m_selectedRangeBlocks.find(sid) != m_selectedRangeBlocks.end())
              m_anim->removeStrip((uint32_t)i);
          }
        } else {
          for (int i = (int)m_clip->tracks.size() - 1; i >= 0; --i) {
            if (m_selectedRangeBlocks.find(m_clip->tracks[(size_t)i].blockId) !=
                m_selectedRangeBlocks.end()) {
              m_clip->tracks.erase(m_clip->tracks.begin() + (ptrdiff_t)i);
            }
          }
          for (int i = (int)m_clip->entityRanges.size() - 1; i >= 0; --i) {
            if (m_selectedRangeBlocks.find(
                    m_clip->entityRanges[(size_t)i].blockId) !=
                m_selectedRangeBlocks.end()) {
              m_clip->entityRanges.erase(m_clip->entityRanges.begin() +
                                         (ptrdiff_t)i);
            }
          }
          if (m_autoUpdateLastFrame)
            recomputeLastFrameFromKeys();
        }
        m_selectedRangeBlocks.clear();
      } else {
        deleteSelectedKeys();
      }
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_A) && m_clip) {
      m_selectedKeys.clear();
      m_selectedKeys.reserve(m_clip->tracks.size() * 4);
      for (int ti = 0; ti < (int)m_clip->tracks.size(); ++ti) {
        const auto &t = m_clip->tracks[(size_t)ti];
        for (int ki = 0; ki < (int)t.curve.keys.size(); ++ki) {
          m_selectedKeys.push_back(SeqKeyRef{ti, ki});
        }
      }
      if (!m_selectedKeys.empty())
        m_activeKey = m_selectedKeys.front();
    }
    if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_C)) {
      if (shift) {
        if (nlaActive) {
          const int32_t fAll = clampFrame(m_anim->frame());
          for (int i = (int)m_anim->strips().size() - 1; i >= 0; --i)
            cutLayerRangeAtFrame(i, fAll);
        } else if (m_clip) {
          const int32_t fAll = clampFrame(m_anim->frame());
          for (int i = (int)m_clip->entityRanges.size() - 1; i >= 0; --i) {
            cutLayerRangeAtFrame(i, fAll);
          }
          if (m_autoUpdateLastFrame)
            recomputeLastFrameFromKeys();
        }
      } else {
        m_cutToolActive = !m_cutToolActive;
      }
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_C))
      copySelectedKeys();
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_V))
      pasteKeysAtFrame(m_anim->frame());
    if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_I) && m_anim &&
        !m_anim->strips().empty() && !ImGui::GetIO().WantTextInput) {
      ActionID actionId = m_nlaKeyAction;
      if (actionId == 0 && !m_anim->actions().empty())
        actionId = 1;
      AnimAction *a = m_anim->action(actionId);
      if (a) {
        const int32_t frame = m_anim->frame();
        for (EntityID eSel : m_selectedLayerBlocks) {
          if (!m_world || !m_world->isAlive(eSel))
            continue;
          const auto &tr = m_world->transform(eSel);
          if (m_nlaKeying.keyTranslate) {
            keyValue(*a, AnimChannel::TranslateX, frame, tr.translation.x,
                     m_nlaKeying.mode);
            keyValue(*a, AnimChannel::TranslateY, frame, tr.translation.y,
                     m_nlaKeying.mode);
            keyValue(*a, AnimChannel::TranslateZ, frame, tr.translation.z,
                     m_nlaKeying.mode);
          }
          if (m_nlaKeying.keyRotate) {
            const glm::vec3 deg = glm::degrees(glm::eulerAngles(tr.rotation));
            keyValue(*a, AnimChannel::RotateX, frame, deg.x, m_nlaKeying.mode);
            keyValue(*a, AnimChannel::RotateY, frame, deg.y, m_nlaKeying.mode);
            keyValue(*a, AnimChannel::RotateZ, frame, deg.z, m_nlaKeying.mode);
          }
          if (m_nlaKeying.keyScale) {
            keyValue(*a, AnimChannel::ScaleX, frame, tr.scale.x, m_nlaKeying.mode);
            keyValue(*a, AnimChannel::ScaleY, frame, tr.scale.y, m_nlaKeying.mode);
            keyValue(*a, AnimChannel::ScaleZ, frame, tr.scale.z, m_nlaKeying.mode);
          }
        }
        m_anim->setFrame(frame);
      }
    }
  }

  if (hovered) {
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
      m_panningTimeline = true;
      m_panStartMouseX = mp.x;
      m_panStartFirstFrame = m_viewFirstFrame;
    }
    if (m_panningTimeline && ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
      const float dx = mp.x - m_panStartMouseX;
      const int32_t df = (int32_t)std::round(-dx / std::max(1.0f, m_pixelsPerFrame));
      m_viewFirstFrame = clampi(m_panStartFirstFrame + df, 0, maxFirstFrame);
      firstFrame = m_viewFirstFrame;
      lastVisible =
          std::min(lastFrame, firstFrame + std::max<int32_t>(0, framesVisible - 1));
    } else if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
      m_panningTimeline = false;
    }

    SeqKeyRef hit{};
    EntityID hitEnt = InvalidEntity;
    SeqProperty hitProp = SeqProperty::Position;
    int32_t hitFrame = 0;
    const bool hitPropKey =
        hitTestPropertyKey(r, firstFrame, mp, hitEnt, hitProp, hitFrame, hit);
    const bool hitKey = hitPropKey || hitTestKey(r, firstFrame, mp, hit);
    if (m_boxSelecting) {
      m_boxSelectEnd = mp;
      const ImVec2 bmin(std::min(m_boxSelectStart.x, m_boxSelectEnd.x),
                        std::min(m_boxSelectStart.y, m_boxSelectEnd.y));
      const ImVec2 bmax(std::max(m_boxSelectStart.x, m_boxSelectEnd.x),
                        std::max(m_boxSelectStart.y, m_boxSelectEnd.y));
      dl->AddRectFilled(bmin, bmax, IM_COL32(5, 130, 255, 64));
      dl->AddRect(bmin, bmax, IM_COL32(5, 130, 255, 128), 0.0f, 0, 1.0f);
    }
    if (m_cutToolActive && mp.x >= interactionX0 && mp.x <= r.Max.x &&
        mp.y >= (r.Min.y + rulerH) && mp.y <= r.Max.y) {
      const float cutX =
          frameToX(clampFrame(xToFrame(mp.x, firstFrame, xStart)), firstFrame,
                   xStart);
      dl->AddLine(ImVec2(cutX, r.Min.y + rulerH), ImVec2(cutX, r.Max.y),
                  IM_COL32(255, 120, 80, 220), 2.0f);
    }

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      if (m_cutToolActive) {
        EntityID cutEnt = InvalidEntity;
        int cutRange = -1;
        int32_t cutStart = 0;
        int32_t cutEnd = 0;
        float cutStartX = 0.0f;
        float cutEndX = 0.0f;
        if (hitLayerRange(cutEnt, cutRange, cutStart, cutEnd, cutStartX,
                          cutEndX)) {
          const int32_t cutF = clampFrame(xToFrame(mp.x, firstFrame, xStart));
          if (cutLayerRangeAtFrame(cutRange, cutF) && m_autoUpdateLastFrame)
            recomputeLastFrameFromKeys();
        }
        return;
      }
      // Frame cursor drag only on ruler
      if (mp.y >= r.Min.y && mp.y <= (r.Min.y + rulerH) &&
          mp.x >= interactionX0 && mp.x <= r.Max.x) {
        m_draggingFrameLine = true;
        const int32_t f = clampFrame(xToFrame(mp.x, firstFrame, xStart));
        m_anim->setFrame(f);
      } else {
        // box select from empty space in track area
        if (!hitKey && mp.y > (r.Min.y + rulerH) && mp.y <= r.Max.y &&
            mp.x >= interactionX0 && mp.x <= r.Max.x) {
          EntityID hoverEnt = InvalidEntity;
          int hoverRange = -1;
          int32_t hs = 0, he = 0;
          float hsx = 0.0f, hex = 0.0f;
          const bool hitLayerBlock =
              hitLayerRange(hoverEnt, hoverRange, hs, he, hsx, hex);
          if (!hitLayerBlock) {
            m_boxSelecting = true;
            m_boxSelectStart = mp;
            m_boxSelectEnd = mp;
            m_boxSelectAdditive = ctrl || shift;
            return;
          }
        }
        // duration drag (edge crop or move) only on layer rows
        const float tracksTop = r.Min.y + rulerH;
        const int row = (int)((mp.y - tracksTop) / m_rowHeight);
        if (!hitKey && row >= 0 && row < (int)m_rows.size() &&
            mp.x >= interactionX0 && mp.x <= r.Max.x) {
          const SeqRow &rr = m_rows[(size_t)row];
          if (rr.type == SeqRowType::Layer) {
            EntityID hitLayerEntity = InvalidEntity;
            int hitRangeIndex = -1;
            int32_t startF = 0;
            int32_t endF = 0;
            float startX = 0.0f;
            float endX = 0.0f;
            if (hitLayerRange(hitLayerEntity, hitRangeIndex, startF, endF,
                              startX, endX)) {
              const EntityID e = hitLayerEntity;
              if (!ctrl && !shift) {
                m_selectedLayerBlocks.clear();
                m_selectedLayerBlocks.insert(e);
                m_selectedRangeBlocks.clear();
                if (nlaActive) {
                  if (hitRangeIndex >= 0 &&
                      hitRangeIndex < (int)m_anim->strips().size())
                    m_selectedRangeBlocks.insert(
                        nlaSelectIdFromIndex(hitRangeIndex));
                } else if (hitRangeIndex >= 0 &&
                           hitRangeIndex < (int)m_clip->entityRanges.size()) {
                  m_selectedRangeBlocks.insert(
                      m_clip->entityRanges[(size_t)hitRangeIndex].blockId);
                }
              } else if (ctrl) {
                if (m_selectedLayerBlocks.find(e) != m_selectedLayerBlocks.end())
                  m_selectedLayerBlocks.erase(e);
                else
                  m_selectedLayerBlocks.insert(e);
                if (hitRangeIndex >= 0 &&
                    ((nlaActive &&
                      hitRangeIndex < (int)m_anim->strips().size()) ||
                     (!nlaActive &&
                      hitRangeIndex < (int)m_clip->entityRanges.size()))) {
                  const uint32_t bid = nlaActive
                                           ? nlaSelectIdFromIndex(hitRangeIndex)
                                           : m_clip->entityRanges[(size_t)hitRangeIndex]
                                                 .blockId;
                  if (m_selectedRangeBlocks.find(bid) !=
                      m_selectedRangeBlocks.end())
                    m_selectedRangeBlocks.erase(bid);
                  else
                    m_selectedRangeBlocks.insert(bid);
                }
              } else if (shift) {
                m_selectedLayerBlocks.insert(e);
                if (nlaActive) {
                  if (hitRangeIndex >= 0 &&
                      hitRangeIndex < (int)m_anim->strips().size())
                    m_selectedRangeBlocks.insert(
                        nlaSelectIdFromIndex(hitRangeIndex));
                } else if (hitRangeIndex >= 0 &&
                           hitRangeIndex < (int)m_clip->entityRanges.size()) {
                  m_selectedRangeBlocks.insert(
                      m_clip->entityRanges[(size_t)hitRangeIndex].blockId);
                }
              }
              m_draggingDuration = true;
              m_dragDurationEntity = hitLayerEntity;
              m_dragDurationRangeIndex = hitRangeIndex;
              m_dragDurationStartFrame = xToFrame(mp.x, firstFrame, xStart);
              m_dragDurationOrigStart = startF;
              m_dragDurationOrigEnd = endF;
              m_dragDurationTargets.clear();
              m_dragDurationTrackSnapshots.clear();
              if (!m_selectedRangeBlocks.empty()) {
                if (nlaActive) {
                  for (int si = 0; si < (int)m_anim->strips().size(); ++si) {
                    const uint32_t sid = nlaSelectIdFromIndex(si);
                    if (m_selectedRangeBlocks.find(sid) ==
                        m_selectedRangeBlocks.end())
                      continue;
                    const auto &s = m_anim->strips()[(size_t)si];
                    LayerDragTarget t{};
                    t.e = s.target;
                    t.blockId = sid;
                    t.action = s.action;
                    t.start = s.start;
                    t.end = s.end;
                    t.inFrame = s.inFrame;
                    t.outFrame = s.outFrame;
                    m_dragDurationTargets.push_back(t);
                  }
                } else {
                  for (const auto &er : m_clip->entityRanges) {
                    if (m_selectedRangeBlocks.find(er.blockId) ==
                        m_selectedRangeBlocks.end())
                      continue;
                    LayerDragTarget t{};
                    t.e = er.entity;
                    t.blockId = er.blockId;
                    t.start = er.start;
                    t.end = er.end;
                    m_dragDurationTargets.push_back(t);
                  }
                }
              }
              if (m_dragDurationTargets.empty()) {
                LayerDragTarget t{};
                t.e = e;
                t.blockId =
                    nlaActive
                        ? nlaSelectIdFromIndex(hitRangeIndex)
                        : ((hitRangeIndex >= 0 &&
                            hitRangeIndex < (int)m_clip->entityRanges.size())
                               ? m_clip->entityRanges[(size_t)hitRangeIndex].blockId
                               : resolveTargetBlock(e));
                t.start = startF;
                t.end = endF;
                if (nlaActive && hitRangeIndex >= 0 &&
                    hitRangeIndex < (int)m_anim->strips().size()) {
                  const auto &s = m_anim->strips()[(size_t)hitRangeIndex];
                  t.action = s.action;
                  t.inFrame = s.inFrame;
                  t.outFrame = s.outFrame;
                }
                m_dragDurationTargets.push_back(t);
              }
              {
                std::unordered_set<uint32_t> movedBlocks;
                movedBlocks.reserve(m_dragDurationTargets.size());
                for (const LayerDragTarget &t : m_dragDurationTargets) {
                  if (t.blockId != 0)
                    movedBlocks.insert(t.blockId);
                }
                if (!nlaActive) {
                  for (int ti = 0; ti < (int)m_clip->tracks.size(); ++ti) {
                    const auto &tr = m_clip->tracks[(size_t)ti];
                    if (movedBlocks.find(tr.blockId) == movedBlocks.end())
                      continue;
                    DragTrackSnapshot s{};
                    s.trackIndex = ti;
                    s.frames.reserve(tr.curve.keys.size());
                    for (const auto &k : tr.curve.keys)
                      s.frames.push_back((int32_t)k.frame);
                    m_dragDurationTrackSnapshots.push_back(std::move(s));
                  }
                } else {
                  std::unordered_set<ActionID> movedActions;
                  movedActions.reserve(m_dragDurationTargets.size());
                  for (const LayerDragTarget &t : m_dragDurationTargets) {
                    if (t.action != 0)
                      movedActions.insert(t.action);
                  }
                  m_dragDurationActionSnapshots.clear();
                  m_dragDurationActionSnapshots.reserve(movedActions.size());
                  for (ActionID id : movedActions) {
                    const AnimAction *a = m_anim->action(id);
                    if (!a)
                      continue;
                    DragActionSnapshot snap{};
                    snap.action = id;
                    snap.start = a->start;
                    snap.end = a->end;
                    snap.trackFrames.reserve(a->tracks.size());
                    for (const auto &tr : a->tracks) {
                      std::vector<int32_t> frames;
                      frames.reserve(tr.curve.keys.size());
                      for (const auto &k : tr.curve.keys)
                        frames.push_back((int32_t)k.frame);
                      snap.trackFrames.push_back(std::move(frames));
                    }
                    m_dragDurationActionSnapshots.push_back(std::move(snap));
                  }
                }
              }
              if (std::fabs(mp.x - startX) <= 6.0f) {
                m_dragDurationMode = 2; // crop start
              } else if (std::fabs(mp.x - endX) <= 6.0f) {
                m_dragDurationMode = 3; // crop end
              } else {
                m_dragDurationMode = 1; // move block
              }
            }
          }
        }

        if (hitKey && !m_draggingDuration) {
          if (!ctrl && !shift) {
            selectSingle(hit);
          } else if (ctrl) {
            toggleSelect(hit);
          } else if (shift) {
            addSelect(hit);
          }

          // Move playhead to clicked keyframe
          if (hitPropKey) {
            m_anim->setFrame(hitFrame);
          } else if (m_clip && hit.trackIndex >= 0 &&
                     hit.trackIndex < (int)m_clip->tracks.size()) {
            const auto &keys =
                m_clip->tracks[(size_t)hit.trackIndex].curve.keys;
            if (hit.keyIndex >= 0 && hit.keyIndex < (int)keys.size())
              m_anim->setFrame((int32_t)keys[(size_t)hit.keyIndex].frame);
          }

          m_dragStartFrame = xToFrame(mp.x, firstFrame, xStart);
          if (hitPropKey) {
            m_draggingProperty = true;
            m_dragPropEntity = hitEnt;
            m_dragProp = hitProp;
            m_dragPropStartFrame = m_dragStartFrame;
            m_dragPropOrigFrame = hitFrame;
          } else {
            m_draggingKey = true;
            if (m_clip && hit.trackIndex >= 0 &&
                hit.trackIndex < (int)m_clip->tracks.size()) {
              const auto &keys =
                  m_clip->tracks[(size_t)hit.trackIndex].curve.keys;
              if (hit.keyIndex >= 0 && hit.keyIndex < (int)keys.size())
                m_dragOrigKeyFrame =
                    (int32_t)keys[(size_t)hit.keyIndex].frame;
              else
                m_dragOrigKeyFrame = m_dragStartFrame;
            }
          }
        } else if (!m_draggingDuration) {
          if (!ctrl && !shift) {
            clearSelection();
            const float tracksTop = r.Min.y + rulerH;
            const int row = (int)((mp.y - tracksTop) / m_rowHeight);
            if (row >= 0 && row < (int)m_rows.size()) {
              const SeqRow &rr = m_rows[(size_t)row];
              if (rr.type != SeqRowType::Layer)
                m_selectedLayerBlocks.clear();
            } else {
              m_selectedLayerBlocks.clear();
            }
            m_selectedRangeBlocks.clear();
          }
        }
      }
    }

    if (m_draggingDuration && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      const int32_t nowF = xToFrame(mp.x, firstFrame, xStart);
      const int32_t delta = nowF - m_dragDurationStartFrame;
      if (nlaActive && !m_dragDurationTargets.empty()) {
        for (int si = 0; si < (int)m_anim->strips().size(); ++si) {
          auto &s = m_anim->strips()[(size_t)si];
          const uint32_t sid = nlaSelectIdFromIndex(si);
          bool selected = false;
          for (const auto &t : m_dragDurationTargets) {
            if (t.blockId == sid) {
              selected = true;
              if (m_dragDurationMode == 1) {
                s.start = std::max<int32_t>(0, t.start + delta);
                s.end = std::max(s.start, t.end + delta);
                s.inFrame = std::max<int32_t>(0, t.inFrame + delta);
                s.outFrame = std::max(s.inFrame, t.outFrame + delta);
              } else if (m_dragDurationMode == 2) {
                const int32_t newStart = std::max<int32_t>(0, t.start + delta);
                s.start = newStart;
                if (s.end < s.start)
                  s.end = s.start;
              } else if (m_dragDurationMode == 3) {
                const int32_t newEnd = std::max<int32_t>(0, t.end + delta);
                s.end = std::max(newEnd, s.start);
              }
              break;
            }
          }
          (void)selected;
        }
        if (m_dragDurationMode == 1) {
          for (const DragActionSnapshot &snap : m_dragDurationActionSnapshots) {
            AnimAction *a = m_anim->action(snap.action);
            if (!a)
              continue;
            a->start = std::max<int32_t>(0, snap.start + delta);
            a->end = std::max(a->start, snap.end + delta);
            const int nt = std::min((int)a->tracks.size(),
                                    (int)snap.trackFrames.size());
            for (int ti = 0; ti < nt; ++ti) {
              auto &keys = a->tracks[(size_t)ti].curve.keys;
              const auto &frames = snap.trackFrames[(size_t)ti];
              const int nk = std::min((int)keys.size(), (int)frames.size());
              for (int ki = 0; ki < nk; ++ki) {
                keys[(size_t)ki].frame =
                    std::max<int32_t>(0, frames[(size_t)ki] + delta);
              }
            }
          }
        }
      } else if (m_clip && !m_dragDurationTargets.empty()) {
        for (const LayerDragTarget &t : m_dragDurationTargets) {
          for (auto &rr : m_clip->entityRanges) {
            if (rr.blockId != t.blockId || rr.entity != t.e)
              continue;
            if (m_dragDurationMode == 1) {
              rr.start = std::max<int32_t>(0, t.start + delta);
              rr.end = std::max(rr.start, t.end + delta);
            } else if (m_dragDurationMode == 2) {
              const int32_t newStart = std::max<int32_t>(0, t.start + delta);
              rr.start = newStart;
              if (rr.end < rr.start)
                rr.end = rr.start;
            } else if (m_dragDurationMode == 3) {
              const int32_t newEnd = std::max<int32_t>(0, t.end + delta);
              rr.end = std::max(newEnd, rr.start);
            }
            break;
          }
        }
        if (m_dragDurationMode == 1) {
          for (const DragTrackSnapshot &s : m_dragDurationTrackSnapshots) {
            if (s.trackIndex < 0 || s.trackIndex >= (int)m_clip->tracks.size())
              continue;
            auto &keys = m_clip->tracks[(size_t)s.trackIndex].curve.keys;
            const int n = std::min((int)keys.size(), (int)s.frames.size());
            for (int i = 0; i < n; ++i) {
              const int32_t nf = std::max<int32_t>(0, s.frames[(size_t)i] + delta);
              keys[(size_t)i].frame = nf;
            }
          }
          if (m_autoUpdateLastFrame)
            recomputeLastFrameFromKeys();
        }
      } else {
        for (const LayerDragTarget &t : m_dragDurationTargets) {
          if (t.e == InvalidEntity)
            continue;
          if (m_dragDurationMode == 1) {
            setEntityStartFrame(t.e, t.start + delta);
            setEntityEndFrame(t.e, t.end + delta);
          } else if (m_dragDurationMode == 2) {
            const int32_t newStart = t.start + delta;
            setEntityStartFrame(t.e, newStart);
            if (entityEndFrame(t.e) < newStart)
              setEntityEndFrame(t.e, newStart);
          } else if (m_dragDurationMode == 3) {
            const int32_t newEnd = t.end + delta;
            setEntityEndFrame(t.e, newEnd);
            if (entityStartFrame(t.e) > newEnd)
              setEntityStartFrame(t.e, newEnd);
          }
        }
      }
      if (m_anim)
        m_anim->setFrame(m_anim->frame());
    } else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      m_draggingDuration = false;
      m_dragDurationEntity = InvalidEntity;
      m_dragDurationRangeIndex = -1;
      m_dragDurationMode = 0;
      m_dragDurationTargets.clear();
      m_dragDurationTrackSnapshots.clear();
      m_dragDurationActionSnapshots.clear();
      if (m_anim)
        m_anim->setFrame(m_anim->frame());
    }

    if (m_draggingProperty) {
      if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const int32_t nowF = xToFrame(mp.x, firstFrame, xStart);
        int32_t delta = nowF - m_dragPropStartFrame;
        if (ctrl) {
          const int snap = 5;
          delta = (delta / snap) * snap;
        }
        if (delta != 0) {
          const int32_t newFrame = m_dragPropOrigFrame + delta;
          if (movePropertyKeys(m_dragPropEntity, m_dragProp,
                               m_dragPropOrigFrame, newFrame)) {
            m_dragPropOrigFrame = newFrame;
            m_dragPropStartFrame = nowF;
          }
        }
      } else {
        m_draggingProperty = false;
      }
    } else if (m_draggingKey) {
      if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const int32_t nowF = xToFrame(mp.x, firstFrame, xStart);
        int32_t delta = nowF - m_dragStartFrame;
        if (ctrl) {
          const int snap = 5;
          delta = (delta / snap) * snap;
        }
        if (m_activeKey.trackIndex >= 0 && m_activeKey.keyIndex >= 0) {
          moveKeyFrame(m_activeKey, m_dragOrigKeyFrame + delta);
        }
      } else {
        m_draggingKey = false;
      }
    } else if (m_draggingFrameLine &&
               ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      if (mp.x >= interactionX0 && mp.x <= r.Max.x) {
        const int32_t f = clampFrame(xToFrame(mp.x, firstFrame, xStart));
        m_anim->setFrame(f);
      }
    } else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      m_draggingFrameLine = false;
    }
  }

  if (m_boxSelecting) {
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      m_boxSelectEnd = mp;
      const ImVec2 bmin(std::min(m_boxSelectStart.x, m_boxSelectEnd.x),
                        std::min(m_boxSelectStart.y, m_boxSelectEnd.y));
      const ImVec2 bmax(std::max(m_boxSelectStart.x, m_boxSelectEnd.x),
                        std::max(m_boxSelectStart.y, m_boxSelectEnd.y));
      dl->AddRectFilled(bmin, bmax, IM_COL32(5, 130, 255, 64));
      dl->AddRect(bmin, bmax, IM_COL32(5, 130, 255, 128), 0.0f, 0, 1.0f);
    } else {
      const ImVec2 bmin(std::min(m_boxSelectStart.x, m_boxSelectEnd.x),
                        std::min(m_boxSelectStart.y, m_boxSelectEnd.y));
      const ImVec2 bmax(std::max(m_boxSelectStart.x, m_boxSelectEnd.x),
                        std::max(m_boxSelectStart.y, m_boxSelectEnd.y));
      const bool validBox =
          (std::fabs(bmax.x - bmin.x) > 2.0f && std::fabs(bmax.y - bmin.y) > 2.0f);
      if (validBox && m_clip) {
        if (!m_boxSelectAdditive) {
          m_selectedLayerBlocks.clear();
          m_selectedRangeBlocks.clear();
          m_selectedKeys.clear();
          m_activeKey = SeqKeyRef{};
        }
        const float tracksTop = r.Min.y + rulerH;
        for (int row = 0; row < (int)m_rows.size(); ++row) {
          const SeqRow &rr = m_rows[(size_t)row];
          const float y0 = tracksTop + float(row) * m_rowHeight + 2.0f;
          const float y1 = y0 + m_rowHeight - 4.0f;
          if (rr.type == SeqRowType::Layer) {
            if (nlaActive) {
              for (int si = 0; si < (int)m_anim->strips().size(); ++si) {
                const auto &s = m_anim->strips()[(size_t)si];
                if (s.target != rr.entity)
                  continue;
                const float x0 = frameToX(s.start, firstFrame, xStart);
                const float x1 = frameToX(s.end + 1, firstFrame, xStart);
                if (x1 < bmin.x || x0 > bmax.x || y1 < bmin.y || y0 > bmax.y)
                  continue;
                m_selectedLayerBlocks.insert(rr.entity);
                m_selectedRangeBlocks.insert(nlaSelectIdFromIndex(si));
              }
            } else {
              for (const auto &er : m_clip->entityRanges) {
                if (er.entity != rr.entity)
                  continue;
                const float x0 = frameToX(er.start, firstFrame, xStart);
                const float x1 = frameToX(er.end + 1, firstFrame, xStart);
                if (x1 < bmin.x || x0 > bmax.x || y1 < bmin.y || y0 > bmax.y)
                  continue;
                m_selectedLayerBlocks.insert(rr.entity);
                m_selectedRangeBlocks.insert(er.blockId);
              }
            }
          } else if (rr.type == SeqRowType::Property) {
            if (!propertyHasAnimChannels(rr.prop))
              continue;
            const float cy = (y0 + y1) * 0.5f;
            if (cy < bmin.y || cy > bmax.y)
              continue;
            const uint32_t blockId = resolveTargetBlock(rr.entity);
            AnimChannel ch[3];
            propertyChannels(rr.prop, ch);
            for (int ti = 0; ti < (int)m_clip->tracks.size(); ++ti) {
              const auto &t = m_clip->tracks[(size_t)ti];
              if (t.entity != rr.entity || t.blockId != blockId)
                continue;
              bool channelMatch = false;
              for (int ci = 0; ci < 3; ++ci) {
                if (t.channel == ch[ci]) {
                  channelMatch = true;
                  break;
                }
              }
              if (!channelMatch)
                continue;
              for (int ki = 0; ki < (int)t.curve.keys.size(); ++ki) {
                const int32_t f = (int32_t)t.curve.keys[(size_t)ki].frame;
                const float x = frameToX(f, firstFrame, xStart);
                if (x < bmin.x || x > bmax.x)
                  continue;
                const SeqKeyRef kr{ti, ki};
                if (!isSelected(kr))
                  m_selectedKeys.push_back(kr);
                if (m_activeKey.trackIndex < 0)
                  m_activeKey = kr;
              }
            }
          }
        }
      }
      m_boxSelecting = false;
    }
  }
}

void SequencerPanel::drawLayerBarPane() {
  if (!m_world || !m_anim || !m_clip)
    return;

  const bool ctrl = ImGui::GetIO().KeyCtrl;
  const bool shift = ImGui::GetIO().KeyShift;
  const float indentW = 12.0f;

  for (int row = 0; row < (int)m_rows.size(); ++row) {
    const SeqRow &rr = m_rows[(size_t)row];
    ImGui::PushID(row);

    if (rr.type == SeqRowType::Layer) {
      bool expanded =
          m_expandState[rowKey(rr.entity, SeqRowType::Layer,
                               SeqProperty::Position)];
      if (ImGui::SmallButton(expanded ? "v" : ">")) {
        expanded = !expanded;
        m_expandState[rowKey(rr.entity, SeqRowType::Layer,
                             SeqProperty::Position)] = expanded;
      }
      ImGui::SameLine();

      bool hidden = isLayerHidden(rr.entity);
      if (m_iconReady) {
        if (drawAtlasIconButton(
                m_iconAtlas, hidden ? "hide" : "show", ImVec2(14.0f, 14.0f),
                IM_COL32(255, 255, 255, 255))) {
          if (m_world && m_world->isAlive(rr.entity))
            m_world->transform(rr.entity).hidden = !hidden;
        }
      } else {
        if (ImGui::SmallButton(hidden ? "o" : "O")) {
          if (m_world && m_world->isAlive(rr.entity))
            m_world->transform(rr.entity).hidden = !hidden;
        }
      }
      ImGui::SameLine();

      const bool iso = (m_isolated.find(rr.entity) != m_isolated.end());
      const ImVec2 isoSize(16.0f, 16.0f);
      ImGui::InvisibleButton("##iso", isoSize);
      if (ImGui::IsItemClicked()) {
        if (iso)
          m_isolated.erase(rr.entity);
        else
          m_isolated.insert(rr.entity);
      }
      {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        const ImVec2 p0 = ImGui::GetItemRectMin();
        const ImVec2 p1 = ImGui::GetItemRectMax();
        const ImVec2 c((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);
        const float r = 6.0f;
        const ImU32 fill = iso ? IM_COL32(255, 200, 80, 220)
                               : IM_COL32(80, 80, 80, 200);
        const ImU32 line = IM_COL32(200, 200, 200, 200);
        dl->AddCircleFilled(c, r, fill);
        dl->AddCircle(c, r, line, 0, 1.5f);
      }
      ImGui::SameLine();
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() + rr.depth * indentW);
      const char *ename = "Entity";
      if (m_world->isAlive(rr.entity))
        ename = m_world->name(rr.entity).name.c_str();
      ImGui::TextUnformatted(ename);
      ImGui::SameLine();
      EntityID p = m_world->parentOf(rr.entity);
      if (p != InvalidEntity && m_world->isAlive(p)) {
        ImGui::TextDisabled("(%s)", m_world->name(p).name.c_str());
      }
    } else if (rr.type == SeqRowType::Group) {
      bool expanded =
          m_expandState[rowKey(rr.entity, SeqRowType::Group,
                               SeqProperty::Position)];
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() + rr.depth * indentW);
      if (ImGui::SmallButton(expanded ? "v" : ">")) {
        expanded = !expanded;
        m_expandState[rowKey(rr.entity, SeqRowType::Group,
                             SeqProperty::Position)] = expanded;
      }
      ImGui::SameLine();
      ImGui::TextUnformatted("Transform");
    } else if (rr.type == SeqRowType::Property) {
      const bool sw = stopwatchEnabled(rr.entity, rr.prop);
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() + rr.depth * indentW);
      if (m_iconReady) {
        if (drawAtlasIconButton(m_iconAtlas, "clock", ImVec2(14.0f, 14.0f),
                                sw ? IM_COL32(255, 220, 120, 255)
                                   : IM_COL32(120, 120, 120, 255))) {
          const bool applyMulti =
              m_selectedLayerBlocks.size() > 1 &&
              m_selectedLayerBlocks.find(rr.entity) !=
                  m_selectedLayerBlocks.end();
          if (applyMulti) {
            for (EntityID eSel : m_selectedLayerBlocks)
              setStopwatch(eSel, rr.prop, !sw);
          } else {
            setStopwatch(rr.entity, rr.prop, !sw);
          }
        }
      } else {
        if (ImGui::SmallButton(sw ? "O" : "o")) {
          const bool applyMulti =
              m_selectedLayerBlocks.size() > 1 &&
              m_selectedLayerBlocks.find(rr.entity) !=
                  m_selectedLayerBlocks.end();
          if (applyMulti) {
            for (EntityID eSel : m_selectedLayerBlocks)
              setStopwatch(eSel, rr.prop, !sw);
          } else {
            setStopwatch(rr.entity, rr.prop, !sw);
          }
        }
      }
      if (sw) {
        ImGui::SameLine();
        if (ImGui::SmallButton("<")) {
          std::vector<int32_t> frames;
          if (findPropertyKeys(rr.entity, rr.prop, frames)) {
            const int32_t curF = m_anim->frame();
            int32_t best = -1;
            for (int32_t f : frames) {
              if (f < curF)
                best = f;
              else
                break;
            }
            if (best >= 0)
              m_anim->setFrame(best);
          }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(">")) {
          std::vector<int32_t> frames;
          if (findPropertyKeys(rr.entity, rr.prop, frames)) {
            const int32_t curF = m_anim->frame();
            for (int32_t f : frames) {
              if (f > curF) {
                m_anim->setFrame(f);
                break;
              }
            }
          }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("*")) {
          const int32_t f = m_anim->frame();
          std::vector<int32_t> frames;
          if (findPropertyKeys(rr.entity, rr.prop, frames) &&
              std::find(frames.begin(), frames.end(), f) != frames.end()) {
            deletePropertyKeysAtFrame(rr.entity, rr.prop, f);
          } else {
            addOrOverwritePropertyKeys(rr.entity, rr.prop, f);
          }
        }
      }
      ImGui::SameLine();
      const char *label = "Property";
      switch (rr.prop) {
      case SeqProperty::Position:
        label = "Position";
        break;
      case SeqProperty::Rotation:
        label = "Rotation";
        break;
      case SeqProperty::Scale:
        label = "Scale";
        break;
      case SeqProperty::Opacity:
        label = "Opacity";
        break;
      default:
        break;
      }
      if (ImGui::Selectable(label, false, ImGuiSelectableFlags_AllowDoubleClick,
                            ImVec2(90.0f, 0.0f))) {
        m_graphTrackIndex = graphTrackForPropertyBest(rr.entity, rr.prop);
      }
      if (rr.prop == SeqProperty::Position || rr.prop == SeqProperty::Rotation ||
          rr.prop == SeqProperty::Scale) {
        const char *xyz[3] = {"X", "Y", "Z"};
        for (int ci = 0; ci < 3; ++ci) {
          ImGui::SameLine();
          const int ti = graphTrackForProperty(rr.entity, rr.prop, ci);
          if (ti < 0) {
            ImGui::BeginDisabled();
            ImGui::SmallButton(xyz[ci]);
            ImGui::EndDisabled();
          } else {
            const bool active = (m_graphTrackIndex == ti);
            if (active)
              ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(110, 140, 220, 255));
            if (ImGui::SmallButton(xyz[ci]))
              m_graphTrackIndex = ti;
            if (active)
              ImGui::PopStyleColor();
          }
        }
      }
    } else {
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() + rr.depth * indentW);
      const char *label = (rr.prop == SeqProperty::Audio) ? "Audio" : "Masks";
      ImGui::TextDisabled("%s", label);
    }

    if (ctrl || shift) {
      // keep modifiers consumed similarly to timeline row interactions
    }
    ImGui::PopID();
  }
}

void SequencerPanel::draw() {
  ImGui::Begin("Sequencer");

  if (m_autoUpdateLastFrame && m_clip) {
    recomputeLastFrameFromKeys();
  }

  drawTransportBar();
  ImGui::Separator();
  const float timelineH = std::min(ImGui::GetContentRegionAvail().y,
                                   m_timelineHeight);
  ImGui::BeginChild("##SequencerTimelineScroll",
                    ImVec2(0.0f, timelineH), false,
                    ImGuiWindowFlags_HorizontalScrollbar |
                        ImGuiWindowFlags_AlwaysVerticalScrollbar);
  if (m_showGraphPanel) {
    ensureTracksForWorld();
    buildRowEntities();
    buildRows();
    applyIsolation();
    updateHiddenEntities();

    const float totalW = ImGui::GetContentRegionAvail().x;
    const float splitterW = 6.0f;
    const float minLeftW = 180.0f;
    const float minMainW = 220.0f;
    float leftW = m_labelGutter > 1.0f ? m_labelGutter : 240.0f;
    leftW = clampf(leftW, minLeftW,
                   std::max(minLeftW, totalW - minMainW - splitterW));
    m_labelGutter = leftW;

    ImGui::BeginChild("##GraphLayerBar", ImVec2(leftW, 0.0f), true,
                      ImGuiWindowFlags_AlwaysVerticalScrollbar |
                          ImGuiWindowFlags_HorizontalScrollbar);
    drawLayerBarPane();
    ImGui::EndChild();

    ImGui::SameLine(0.0f, 0.0f);
    const float splitH = ImGui::GetContentRegionAvail().y;
    ImGui::InvisibleButton("##GraphSidebarSplitter", ImVec2(splitterW, splitH));
    const bool splitHovered = ImGui::IsItemHovered();
    const bool splitActive = ImGui::IsItemActive();
    if (splitHovered || splitActive)
      ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (splitActive) {
      m_labelGutter += ImGui::GetIO().MouseDelta.x;
      m_labelGutter =
          clampf(m_labelGutter, minLeftW,
                 std::max(minLeftW, totalW - minMainW - splitterW));
    }
    {
      ImDrawList *dl = ImGui::GetWindowDrawList();
      const ImVec2 s0 = ImGui::GetItemRectMin();
      const ImVec2 s1 = ImGui::GetItemRectMax();
      dl->AddRectFilled(s0, s1, splitActive ? IM_COL32(120, 120, 120, 110)
                                            : (splitHovered ? IM_COL32(95, 95, 95, 80)
                                                            : IM_COL32(70, 70, 70, 55)));
    }

    ImGui::SameLine(0.0f, 0.0f);
    ImGui::BeginChild("##GraphMain", ImVec2(0.0f, 0.0f), true,
                      ImGuiWindowFlags_NoScrollbar);
    const ImVec2 graphAvail = ImGui::GetContentRegionAvail();
    const ImVec2 gp0 = ImGui::GetCursorScreenPos();
    const float rulerH = std::min(m_rulerHeight, std::max(0.0f, graphAvail.y));
    const ImRect rulerRect(gp0, ImVec2(gp0.x + graphAvail.x, gp0.y + rulerH));

    const int32_t lastFrame = m_clip ? std::max<int32_t>(0, m_clip->lastFrame) : 0;
    const float timelineW = std::max(1.0f, rulerRect.GetWidth());
    m_minPixelsPerFrame =
        std::max(1.0f, timelineW / std::max(1, (int)lastFrame + 1));
    if (m_pixelsPerFrame < m_minPixelsPerFrame)
      m_pixelsPerFrame = m_minPixelsPerFrame;
    const int32_t framesVisible =
        std::max<int32_t>(1, (int32_t)(timelineW / m_pixelsPerFrame));
    const int32_t maxFirstFrame = std::max(0, lastFrame - framesVisible);
    m_viewFirstFrame = clampi(m_viewFirstFrame, 0, maxFirstFrame);
    const int32_t firstFrame = m_viewFirstFrame;
    const int32_t lastVisible =
        std::min(lastFrame, firstFrame + std::max<int32_t>(0, framesVisible - 1));

    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(rulerRect.Min, rulerRect.Max, IM_COL32(18, 18, 18, 255));
    dl->AddRect(rulerRect.Min, rulerRect.Max, IM_COL32(60, 60, 60, 255));

    const int32_t fpsFrames =
        std::max<int32_t>(1, (int32_t)std::round(m_anim->fps()));
    const float minLabelPx = 70.0f;
    float stepFrames = 1.0f;
    const float fpsF = (float)fpsFrames;
    const float stepCandidates[] = {1,   2,   5,   10,  0.25f * fpsF,
                                    0.5f * fpsF, 1.0f * fpsF, 2.0f * fpsF,
                                    5.0f * fpsF, 10.0f * fpsF, 30.0f * fpsF,
                                    60.0f * fpsF, 120.0f * fpsF,
                                    300.0f * fpsF};
    for (float s : stepCandidates) {
      if (s < 1.0f)
        continue;
      if (s * m_pixelsPerFrame >= minLabelPx) {
        stepFrames = s;
        break;
      }
      stepFrames = s;
    }
    const int32_t stepI = std::max(1, (int32_t)std::round(stepFrames));
    const int32_t firstStep = (firstFrame / stepI) * stepI;
    for (int32_t f = firstStep; f <= lastVisible; f += stepI) {
      const float x = gp0.x + float(f - firstFrame) * m_pixelsPerFrame;
      dl->AddLine(ImVec2(x, rulerRect.Min.y), ImVec2(x, rulerRect.Max.y),
                  IM_COL32(35, 35, 35, 255), 1.0f);
      double seconds = (double)f / (double)fpsFrames;
      char buf[64];
      if (stepFrames >= fpsFrames * 60.0f) {
        const int total = (int)seconds;
        const int mm = total / 60;
        const int ss = total % 60;
        std::snprintf(buf, sizeof(buf), "%d:%02d", mm, ss);
      } else if (stepFrames >= fpsFrames) {
        std::snprintf(buf, sizeof(buf), "%.0f s", seconds);
      } else {
        std::snprintf(buf, sizeof(buf), "%.2f s", seconds);
      }
      dl->AddText(ImVec2(x + 2.0f, rulerRect.Min.y + 2.0f),
                  IM_COL32(140, 140, 140, 255), buf);
    }
    if (m_anim) {
      const float frameX =
          gp0.x + float(m_anim->frame() - firstFrame) * m_pixelsPerFrame;
      dl->AddLine(ImVec2(frameX, rulerRect.Min.y), ImVec2(frameX, rulerRect.Max.y),
                  IM_COL32(255, 80, 80, 255), 2.0f);
    }

    ImGui::SetCursorScreenPos(rulerRect.Min);
    ImGui::InvisibleButton(
        "##GraphSharedRuler", ImVec2(rulerRect.GetWidth(), rulerRect.GetHeight()),
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    const bool rulerHovered = ImGui::IsItemHovered();
    const ImVec2 mp = ImGui::GetMousePos();
    if (rulerHovered && m_anim) {
      ImGuiIO &io = ImGui::GetIO();
      if (io.KeyAlt && io.MouseWheel != 0.0f) {
        const float zoom = (io.MouseWheel > 0.0f) ? 1.1f : 0.9f;
        m_pixelsPerFrame *= zoom;
        if (m_pixelsPerFrame < m_minPixelsPerFrame)
          m_pixelsPerFrame = m_minPixelsPerFrame;
        const int32_t maxFirstAfterZoom = std::max(0, lastFrame - framesVisible);
        m_viewFirstFrame = clampi(m_viewFirstFrame, 0, maxFirstAfterZoom);
      } else {
        float scroll = 0.0f;
        if (io.MouseWheelH != 0.0f)
          scroll = io.MouseWheelH;
        else if (io.KeyShift && io.MouseWheel != 0.0f)
          scroll = io.MouseWheel;
        if (scroll != 0.0f) {
          const int32_t step = std::max<int32_t>(1, framesVisible / 10);
          m_viewFirstFrame -= (int32_t)std::round(scroll * (float)step);
          m_viewFirstFrame = clampi(m_viewFirstFrame, 0, maxFirstFrame);
        }
      }

      if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
          (ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
           ImGui::IsItemActive())) {
        const int32_t f = clampFrame(firstFrame + (int32_t)std::round(
                                                    (mp.x - gp0.x) /
                                                    std::max(1.0f, m_pixelsPerFrame)));
        m_anim->setFrame(f);
      }

      if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
        m_panningTimeline = true;
        m_panStartMouseX = mp.x;
        m_panStartFirstFrame = m_viewFirstFrame;
      }
      if (m_panningTimeline && ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
        const float dx = mp.x - m_panStartMouseX;
        const int32_t df =
            (int32_t)std::round(-dx / std::max(1.0f, m_pixelsPerFrame));
        m_viewFirstFrame = clampi(m_panStartFirstFrame + df, 0, maxFirstFrame);
      } else if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
        m_panningTimeline = false;
      }
    }

    ImGui::SetCursorScreenPos(ImVec2(gp0.x, gp0.y + rulerH));
    if (m_clip) {
      const bool valid =
          m_graphTrackIndex >= 0 && m_graphTrackIndex < (int)m_clip->tracks.size();
      if (!valid) {
        m_graphTrackIndex = -1;
        for (int ti = 0; ti < (int)m_clip->tracks.size(); ++ti) {
          if (!m_clip->tracks[(size_t)ti].curve.keys.empty()) {
            m_graphTrackIndex = ti;
            break;
          }
        }
        if (m_graphTrackIndex < 0 && !m_clip->tracks.empty())
          m_graphTrackIndex = 0;
      }
    } else {
      m_graphTrackIndex = -1;
    }
    m_curveEditor.setClip(m_clip);
    m_curveEditor.setFrameWindow(m_viewFirstFrame, m_pixelsPerFrame);
    m_curveEditor.setCurrentFrame(m_anim ? m_anim->frame() : 0);
    m_curveEditor.setActiveTrack(m_graphTrackIndex);
    m_curveEditor.onImGui();
    m_graphTrackIndex = m_curveEditor.activeTrack();
    ImGui::EndChild();
  } else {
    drawTimeline();
  }
  ImGui::EndChild();

  ImGui::End();

  // Sequencer inspector removed: inspector panel is source of truth
}

void SequencerPanel::updateHiddenEntities() {
  if (!m_anim || !m_clip)
    return;

  ensureTracksForWorld();
  buildRowEntities();
  buildRows();
  applyIsolation();

  if (m_anim->frame() > m_clip->lastFrame)
    m_anim->setFrame(m_clip->lastFrame);

  m_hiddenEntities.clear();
  // disabledAnim is handled by AnimationSystem using clip ranges.
}

void SequencerPanel::handleStepRepeat(const InputSystem &input, float dt) {
  if (!timelineHot() || !m_anim || !m_clip)
    return;

  const bool leftDown = input.isDown(Key::ArrowLeft);
  const bool rightDown = input.isDown(Key::ArrowRight);
  int dir = 0;
  if (leftDown && !rightDown)
    dir = -1;
  else if (rightDown && !leftDown)
    dir = 1;

  if (dir == 0) {
    m_repeatDir = 0;
    m_repeatTimer = 0.0f;
    return;
  }

  const bool ctrl = input.isDown(Key::LeftCtrl) || input.isDown(Key::RightCtrl);
  const int stepSize = ctrl ? 10 : 1;

  const bool justPressed =
      (dir < 0 && input.isPressed(Key::ArrowLeft)) ||
      (dir > 0 && input.isPressed(Key::ArrowRight));

  if (justPressed || dir != m_repeatDir) {
    step(dir * stepSize);
    m_repeatDir = dir;
    m_repeatTimer = m_repeatDelay;
    return;
  }

  m_repeatTimer -= std::max(0.0f, dt);
  while (m_repeatTimer <= 0.0f) {
    step(dir * stepSize);
    m_repeatTimer += m_repeatRate;
  }
}

} // namespace Nyx
