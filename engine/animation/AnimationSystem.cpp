#include "AnimationSystem.h"

#include "animation/AnimationTypes.h"
#include "scene/Components.h"
#include "scene/World.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

#include <glm/gtx/quaternion.hpp>

namespace Nyx {

ActionID AnimationSystem::createAction(AnimAction a) {
  if (a.tracks.empty()) {
    a.start = 0;
    a.end = 0;
  } else {
    bool any = false;
    AnimFrame mn = 0;
    AnimFrame mx = 0;
    for (const auto &t : a.tracks) {
      for (const auto &k : t.curve.keys) {
        if (!any) {
          mn = mx = k.frame;
          any = true;
        } else {
          mn = std::min(mn, k.frame);
          mx = std::max(mx, k.frame);
        }
      }
    }
    a.start = any ? mn : 0;
    a.end = any ? mx : 0;
  }

  m_actions.push_back(std::move(a));
  return (ActionID)m_actions.size();
}

AnimAction *AnimationSystem::action(ActionID id) {
  if (id == 0)
    return nullptr;
  const uint32_t idx = id - 1u;
  if (idx >= (uint32_t)m_actions.size())
    return nullptr;
  return &m_actions[idx];
}

const AnimAction *AnimationSystem::action(ActionID id) const {
  if (id == 0)
    return nullptr;
  const uint32_t idx = id - 1u;
  if (idx >= (uint32_t)m_actions.size())
    return nullptr;
  return &m_actions[idx];
}

uint32_t AnimationSystem::addStrip(const NlaStrip &s) {
  m_strips.push_back(s);
  return (uint32_t)m_strips.size() - 1u;
}

bool AnimationSystem::removeStrip(uint32_t stripIndex) {
  if (stripIndex >= (uint32_t)m_strips.size())
    return false;
  m_strips.erase(m_strips.begin() + (ptrdiff_t)stripIndex);
  return true;
}

void AnimationSystem::clearNla() {
  m_strips.clear();
  m_actions.clear();
}

void AnimationSystem::setFrame(AnimFrame frame) {
  m_frame = frame;
  m_accum = 0.0f;
  evaluate();
  updateDisabledAnim();
}

void AnimationSystem::play() { m_playing = true; }

void AnimationSystem::pause() { m_playing = false; }

void AnimationSystem::toggle() { m_playing = !m_playing; }

void AnimationSystem::setFps(float fps) {
  m_fps = std::max(1.0f, fps);
}

void AnimationSystem::tick(float dt) {
  if (!m_active && m_strips.empty())
    return;

  if (m_playing) {
    const float step = 1.0f / std::max(1.0f, m_fps);
    m_accum += std::max(0.0f, dt);

    bool advanced = false;
    while (m_accum >= step) {
      m_accum -= step;
      m_frame++;
      advanced = true;

      if (!m_strips.empty()) {
        if (m_active && m_active->loop) {
          if (m_frame > m_active->lastFrame)
            m_frame = 0;
        }
      } else if (m_active) {
        if (m_active->loop) {
          if (m_frame > m_active->lastFrame)
            m_frame = 0;
        } else if (m_frame > m_active->lastFrame) {
          m_frame = m_active->lastFrame;
          m_playing = false;
          break;
        }
      }
    }

    if (advanced)
      evaluate();
  }

  updateDisabledAnim();
}

void AnimationSystem::updateDisabledAnim() {
  if (!m_world)
    return;

  if (!m_strips.empty()) {
    for (EntityID e : m_world->alive()) {
      if (!m_world->isAlive(e))
        continue;
      m_world->transform(e).disabledAnim = false;
    }

    std::unordered_map<EntityID, bool, EntityHash> hasRange;
    std::unordered_map<EntityID, bool, EntityHash> inAnyRange;
    hasRange.reserve(m_strips.size());
    inAnyRange.reserve(m_strips.size());

    for (const auto &s : m_strips) {
      if (s.target == InvalidEntity || !m_world->isAlive(s.target))
        continue;
      hasRange[s.target] = true;
      if (stripWeightAt(s, m_frame) > 0.0f)
        inAnyRange[s.target] = true;
    }

    for (const auto &it : hasRange) {
      EntityID e = it.first;
      auto &tr = m_world->transform(e);
      tr.disabledAnim = (inAnyRange.find(e) == inAnyRange.end());
    }
    return;
  }

  if (!m_active) {
    for (EntityID e : m_world->alive()) {
      if (!m_world->isAlive(e))
        continue;
      m_world->transform(e).disabledAnim = false;
    }
    return;
  }

  for (EntityID e : m_world->alive()) {
    if (!m_world->isAlive(e))
      continue;
    m_world->transform(e).disabledAnim = false;
  }

  std::unordered_map<EntityID, bool, EntityHash> hasRange;
  std::unordered_map<EntityID, bool, EntityHash> inAnyRange;
  hasRange.reserve(m_active->entityRanges.size());
  inAnyRange.reserve(m_active->entityRanges.size());

  for (const auto &r : m_active->entityRanges) {
    if (r.entity == InvalidEntity)
      continue;
    if (!m_world->isAlive(r.entity))
      continue;
    hasRange[r.entity] = true;
    if (m_frame >= r.start && m_frame <= r.end)
      inAnyRange[r.entity] = true;
  }

  for (const auto &it : hasRange) {
    EntityID e = it.first;
    auto &tr = m_world->transform(e);
    tr.disabledAnim = (inAnyRange.find(e) == inAnyRange.end());
  }
}

void AnimationSystem::evaluate() {
  if (!m_world)
    return;

  if (!m_strips.empty())
    evaluateNla();
  else
    evaluateClip();
}

void AnimationSystem::evaluateClip() {
  if (!m_world || !m_active)
    return;

  std::unordered_map<EntityID, uint32_t, EntityHash> activeBlock;
  std::unordered_map<EntityID, AnimFrame, EntityHash> activeStart;
  activeBlock.reserve(m_active->entityRanges.size());
  activeStart.reserve(m_active->entityRanges.size());
  for (const auto &r : m_active->entityRanges) {
    if (!m_world->isAlive(r.entity))
      continue;
    if (m_frame < r.start || m_frame > r.end)
      continue;
    auto it = activeBlock.find(r.entity);
    if (it == activeBlock.end()) {
      activeBlock[r.entity] = r.blockId;
      activeStart[r.entity] = r.start;
    } else {
      if (r.start >= activeStart[r.entity]) {
        activeBlock[r.entity] = r.blockId;
        activeStart[r.entity] = r.start;
      }
    }
  }

  std::unordered_map<EntityID, glm::vec3, EntityHash> rotDeg;
  std::unordered_map<EntityID, bool, EntityHash> rotInit;

  for (const AnimTrack &t : m_active->tracks) {
    if (!m_world->isAlive(t.entity))
      continue;
    if (t.curve.keys.empty())
      continue;
    auto itBlock = activeBlock.find(t.entity);
    if (itBlock == activeBlock.end())
      continue;
    if (t.blockId != itBlock->second)
      continue;

    auto &tr = m_world->transform(t.entity);
    const float v = t.curve.sample(m_frame);

    switch (t.channel) {
    case AnimChannel::TranslateX:
      tr.translation.x = v;
      tr.dirty = true;
      m_world->worldTransform(t.entity).dirty = true;
      break;
    case AnimChannel::TranslateY:
      tr.translation.y = v;
      tr.dirty = true;
      m_world->worldTransform(t.entity).dirty = true;
      break;
    case AnimChannel::TranslateZ:
      tr.translation.z = v;
      tr.dirty = true;
      m_world->worldTransform(t.entity).dirty = true;
      break;

    case AnimChannel::RotateX: {
      auto &deg = rotDeg[t.entity];
      if (!rotInit[t.entity]) {
        deg = glm::degrees(glm::eulerAngles(tr.rotation));
        rotInit[t.entity] = true;
      }
      deg.x = v;
      break;
    }
    case AnimChannel::RotateY: {
      auto &deg = rotDeg[t.entity];
      if (!rotInit[t.entity]) {
        deg = glm::degrees(glm::eulerAngles(tr.rotation));
        rotInit[t.entity] = true;
      }
      deg.y = v;
      break;
    }
    case AnimChannel::RotateZ: {
      auto &deg = rotDeg[t.entity];
      if (!rotInit[t.entity]) {
        deg = glm::degrees(glm::eulerAngles(tr.rotation));
        rotInit[t.entity] = true;
      }
      deg.z = v;
      break;
    }

    case AnimChannel::ScaleX:
      tr.scale.x = v;
      tr.dirty = true;
      m_world->worldTransform(t.entity).dirty = true;
      break;
    case AnimChannel::ScaleY:
      tr.scale.y = v;
      tr.dirty = true;
      m_world->worldTransform(t.entity).dirty = true;
      break;
    case AnimChannel::ScaleZ:
      tr.scale.z = v;
      tr.dirty = true;
      m_world->worldTransform(t.entity).dirty = true;
      break;
    }
  }

  for (auto &kv : rotDeg) {
    EntityID e = kv.first;
    auto &tr = m_world->transform(e);
    const glm::vec3 rads = glm::radians(kv.second);
    tr.rotation = glm::normalize(glm::quat(rads));
    tr.dirty = true;
    m_world->worldTransform(e).dirty = true;
  }
}

static float saturatef(float x) { return std::max(0.0f, std::min(1.0f, x)); }

float AnimationSystem::stripWeightAt(const NlaStrip &s, AnimFrame frame) {
  if (s.muted)
    return 0.0f;
  if (frame < s.start || frame > s.end)
    return 0.0f;

  float w = s.influence;

  if (s.fadeIn > 0) {
    const AnimFrame fiEnd = s.start + s.fadeIn;
    if (frame < fiEnd) {
      const float t = float(frame - s.start) / float(std::max(1, s.fadeIn));
      w *= saturatef(t);
    }
  }

  if (s.fadeOut > 0) {
    const AnimFrame foStart = s.end - s.fadeOut;
    if (frame > foStart) {
      const float t = float(s.end - frame) / float(std::max(1, s.fadeOut));
      w *= saturatef(t);
    }
  }

  return saturatef(w);
}

AnimFrame AnimationSystem::mapToActionFrame(const NlaStrip &s,
                                            const AnimAction &a,
                                            AnimFrame globalFrame) {
  const AnimFrame localLen = std::max<AnimFrame>(0, s.outFrame - s.inFrame);
  if (localLen == 0)
    return std::clamp(s.inFrame, a.start, a.end);

  const float dt = float(globalFrame - s.start);
  float t = dt * s.timeScale;
  if (!std::isfinite(t))
    t = 0.0f;

  const float stripDur = float(std::max<AnimFrame>(1, s.end - s.start));
  t = std::max(0.0f, std::min(stripDur, t));

  float lf = float(s.inFrame) + t;
  if (s.reverse)
    lf = float(s.outFrame) - t;

  const float mn = float(std::min(s.inFrame, s.outFrame));
  const float mx = float(std::max(s.inFrame, s.outFrame));
  lf = std::max(mn, std::min(mx, lf));
  return std::clamp<AnimFrame>((AnimFrame)std::lround(lf), a.start, a.end);
}

struct ChannelAccum final {
  float v = 0.0f;
  bool has = false;
};

static void applyReplace(ChannelAccum &c, float sample, float w) {
  if (!c.has) {
    c.v = sample;
    c.has = true;
    return;
  }
  c.v = c.v + (sample - c.v) * w;
}

static void applyAdd(ChannelAccum &c, float sample, float w) {
  if (!c.has) {
    c.v = 0.0f;
    c.has = true;
  }
  c.v += sample * w;
}

static int channelIndex(AnimChannel ch) {
  switch (ch) {
  case AnimChannel::TranslateX:
    return 0;
  case AnimChannel::TranslateY:
    return 1;
  case AnimChannel::TranslateZ:
    return 2;
  case AnimChannel::RotateX:
    return 3;
  case AnimChannel::RotateY:
    return 4;
  case AnimChannel::RotateZ:
    return 5;
  case AnimChannel::ScaleX:
    return 6;
  case AnimChannel::ScaleY:
    return 7;
  case AnimChannel::ScaleZ:
    return 8;
  default:
    return 0;
  }
}

void AnimationSystem::evaluateNla() {
  if (!m_world)
    return;

  struct ActiveStrip final {
    const NlaStrip *s = nullptr;
    const AnimAction *a = nullptr;
    float w = 0.0f;
  };

  std::unordered_map<EntityID, std::vector<ActiveStrip>, EntityHash> byEntity;
  byEntity.reserve(256);

  for (const NlaStrip &s : m_strips) {
    if (s.target == InvalidEntity)
      continue;
    if (!m_world->isAlive(s.target))
      continue;
    const AnimAction *a = action(s.action);
    if (!a)
      continue;
    const float w = stripWeightAt(s, m_frame);
    if (w <= 0.0f)
      continue;
    byEntity[s.target].push_back(ActiveStrip{&s, a, w});
  }

  for (auto &kv : byEntity) {
    const EntityID e = kv.first;
    auto &list = kv.second;
    if (list.empty())
      continue;

    std::sort(list.begin(), list.end(),
              [](const ActiveStrip &lhs, const ActiveStrip &rhs) {
                const int32_t la = lhs.s ? lhs.s->layer : 0;
                const int32_t lb = rhs.s ? rhs.s->layer : 0;
                if (la != lb)
                  return la < lb;
                const AnimFrame sa = lhs.s ? lhs.s->start : 0;
                const AnimFrame sb = rhs.s ? rhs.s->start : 0;
                return sa < sb;
              });

    auto &tr = m_world->transform(e);
    float base[9] = {
        tr.translation.x,
        tr.translation.y,
        tr.translation.z,
        glm::degrees(glm::eulerAngles(tr.rotation)).x,
        glm::degrees(glm::eulerAngles(tr.rotation)).y,
        glm::degrees(glm::eulerAngles(tr.rotation)).z,
        tr.scale.x,
        tr.scale.y,
        tr.scale.z,
    };

    ChannelAccum rep[9]{};
    ChannelAccum add[9]{};
    for (int i = 0; i < 9; ++i) {
      rep[i].v = base[i];
      rep[i].has = true;
      add[i].v = 0.0f;
      add[i].has = false;
    }

    for (const ActiveStrip &as : list) {
      const NlaStrip &s = *as.s;
      const AnimAction &a = *as.a;
      const float w = as.w;
      const AnimFrame af = mapToActionFrame(s, a, m_frame);

      for (const AnimActionTrack &t : a.tracks) {
        if (t.curve.keys.empty())
          continue;
        const int ci = channelIndex(t.channel);
        const float v = t.curve.sample(af);
        if (s.blend == NlaBlendMode::Replace)
          applyReplace(rep[ci], v, w);
        else
          applyAdd(add[ci], v, w);
      }
    }

    float out[9];
    for (int i = 0; i < 9; ++i) {
      const float rv = rep[i].v;
      const float av = add[i].has ? add[i].v : 0.0f;
      out[i] = rv + av;
    }

    tr.translation = glm::vec3(out[0], out[1], out[2]);
    const glm::vec3 rotRad = glm::radians(glm::vec3(out[3], out[4], out[5]));
    tr.rotation = glm::normalize(glm::quat(rotRad));
    tr.scale = glm::vec3(out[6], out[7], out[8]);
    tr.dirty = true;
    m_world->worldTransform(e).dirty = true;
  }
}

} // namespace Nyx
