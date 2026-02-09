#include "editor/ui/panels/SequencerPanel.h"

#include "animation/AnimKeying.h"
#include "animation/AnimationSystem.h"
#include "scene/World.h"

#include <algorithm>
#include <cctype>
#include <glm/gtx/quaternion.hpp>

namespace Nyx {

void SequencerPanel::ensureTracksForWorld() {
  if (!m_world || !m_clip)
    return;
  bool tracksChanged = false;

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
          tracksChanged = true;
        }
      }
    }
  }
  if (tracksChanged)
    invalidateTrackIndexCache();
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

  if (m_clip) {
    const int32_t clipEnd = std::max<int32_t>(0, m_clip->lastFrame);
    const int32_t defaultStart = 0;
    const int32_t defaultEnd = clipEnd;
    for (EntityID e : m_rowEntities) {
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
    m_stopwatchState[k] = findPropertyKeys(e, p, m_frameScratch);
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
    return findPropertyKeys(e, prop, m_frameScratch);
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

} // namespace Nyx
