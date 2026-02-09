#include "EditorHistory.h"

#include "animation/AnimationSystem.h"
#include "scene/World.h"

#include <algorithm>

namespace Nyx {

namespace {

static bool animCurvesEqual(const AnimCurve &a, const AnimCurve &b) {
  if (a.interp != b.interp || a.keys.size() != b.keys.size())
    return false;
  for (size_t i = 0; i < a.keys.size(); ++i) {
    const auto &ka = a.keys[i];
    const auto &kb = b.keys[i];
    if (ka.frame != kb.frame || ka.value != kb.value ||
        ka.in.dx != kb.in.dx || ka.in.dy != kb.in.dy ||
        ka.out.dx != kb.out.dx || ka.out.dy != kb.out.dy ||
        ka.easeOut != kb.easeOut)
      return false;
  }
  return true;
}

} // namespace

std::string EditorHistory::labelForAnimationOp(const OpAnimation &op) const {
  const auto &a = op.before;
  const auto &b = op.after;
  if (!a.valid && b.valid)
    return "Animation: Create Clip";
  if (a.valid && !b.valid)
    return "Animation: Clear Clip";
  if (!a.valid && !b.valid)
    return "Animation";

  if (a.actions.size() != b.actions.size())
    return (b.actions.size() > a.actions.size()) ? "Animation: Add Action"
                                                 : "Animation: Remove Action";
  if (a.strips.size() != b.strips.size())
    return (b.strips.size() > a.strips.size()) ? "Animation: Add Strip"
                                               : "Animation: Remove Strip";
  if (a.ranges.size() != b.ranges.size())
    return "Animation: Layer Range";
  if (a.tracks.size() != b.tracks.size())
    return (b.tracks.size() > a.tracks.size()) ? "Animation: Add Track"
                                               : "Animation: Remove Track";

  if (a.fps != b.fps)
    return "Animation: FPS";
  if (a.lastFrame != b.lastFrame)
    return "Animation: Last Frame";
  if (a.loop != b.loop)
    return "Animation: Loop";
  if (a.playing != b.playing)
    return b.playing ? "Animation: Play" : "Animation: Pause";
  if (a.frame != b.frame)
    return "Animation: Frame";

  for (size_t i = 0; i < a.tracks.size() && i < b.tracks.size(); ++i) {
    const auto &ta = a.tracks[i];
    const auto &tb = b.tracks[i];
    if (ta.entity != tb.entity || ta.blockId != tb.blockId ||
        ta.channel != tb.channel)
      return "Animation: Track Edit";
    if (!animCurvesEqual(ta.curve, tb.curve))
      return "Animation: Keyframes";
  }
  for (size_t i = 0; i < a.strips.size() && i < b.strips.size(); ++i) {
    const auto &sa = a.strips[i];
    const auto &sb = b.strips[i];
    if (sa.action != sb.action || sa.target != sb.target)
      return "Animation: Strip Target";
    if (sa.start != sb.start || sa.end != sb.end)
      return "Animation: Strip Move/Trim";
    if (sa.inFrame != sb.inFrame || sa.outFrame != sb.outFrame)
      return "Animation: Strip In/Out";
    if (sa.layer != sb.layer)
      return "Animation: Strip Layer";
    if (sa.muted != sb.muted)
      return "Animation: Strip Mute";
    if (sa.blend != sb.blend)
      return "Animation: Strip Blend";
    if (sa.timeScale != sb.timeScale)
      return "Animation: Strip Speed";
    if (sa.influence != sb.influence)
      return "Animation: Strip Influence";
    if (sa.reverse != sb.reverse)
      return "Animation: Strip Reverse";
    if (sa.fadeIn != sb.fadeIn || sa.fadeOut != sb.fadeOut)
      return "Animation: Strip Fade";
  }

  return "Animation";
}

PersistedAnimationStateHist
EditorHistory::captureAnimationState(const World &world) const {
  PersistedAnimationStateHist out{};
  if (!m_anim || !m_animClip)
    return out;
  out.valid = true;
  out.name = m_animClip->name;
  out.lastFrame = m_animClip->lastFrame;
  out.loop = m_animClip->loop;
  out.nextBlockId = m_animClip->nextBlockId;
  out.frame = m_anim->frame();
  out.playing = m_anim->playing();
  out.fps = m_anim->fps();

  out.tracks.reserve(m_animClip->tracks.size());
  for (const auto &t : m_animClip->tracks) {
    if (!world.isAlive(t.entity))
      continue;
    const EntityUUID u = world.uuid(t.entity);
    if (!u)
      continue;
    PersistedAnimTrackHist pt{};
    pt.entity = u;
    pt.blockId = t.blockId;
    pt.channel = t.channel;
    pt.curve = t.curve;
    out.tracks.push_back(std::move(pt));
  }

  out.ranges.reserve(m_animClip->entityRanges.size());
  for (const auto &r : m_animClip->entityRanges) {
    if (!world.isAlive(r.entity))
      continue;
    const EntityUUID u = world.uuid(r.entity);
    if (!u)
      continue;
    PersistedAnimRangeHist pr{};
    pr.entity = u;
    pr.blockId = r.blockId;
    pr.start = r.start;
    pr.end = r.end;
    out.ranges.push_back(std::move(pr));
  }

  out.actions.reserve(m_anim->actions().size());
  for (const auto &a : m_anim->actions()) {
    PersistedActionHist pa{};
    pa.name = a.name;
    pa.start = a.start;
    pa.end = a.end;
    pa.tracks.reserve(a.tracks.size());
    for (const auto &t : a.tracks) {
      PersistedActionTrackHist at{};
      at.channel = t.channel;
      at.curve = t.curve;
      pa.tracks.push_back(std::move(at));
    }
    out.actions.push_back(std::move(pa));
  }

  out.strips.reserve(m_anim->strips().size());
  for (const auto &s : m_anim->strips()) {
    PersistedNlaStripHist ps{};
    ps.action = s.action;
    ps.target = world.isAlive(s.target) ? world.uuid(s.target) : EntityUUID{};
    ps.start = s.start;
    ps.end = s.end;
    ps.inFrame = s.inFrame;
    ps.outFrame = s.outFrame;
    ps.timeScale = s.timeScale;
    ps.reverse = s.reverse;
    ps.blend = s.blend;
    ps.influence = s.influence;
    ps.fadeIn = s.fadeIn;
    ps.fadeOut = s.fadeOut;
    ps.layer = s.layer;
    ps.muted = s.muted;
    out.strips.push_back(std::move(ps));
  }

  return out;
}

void EditorHistory::applyAnimationState(const PersistedAnimationStateHist &st,
                                        World &world) {
  if (!m_anim || !m_animClip || !st.valid)
    return;

  auto &clip = *m_animClip;
  clip.name = st.name;
  clip.lastFrame = std::max<AnimFrame>(0, st.lastFrame);
  clip.loop = st.loop;
  clip.nextBlockId = std::max<uint32_t>(1u, st.nextBlockId);
  clip.tracks.clear();
  clip.entityRanges.clear();
  clip.tracks.reserve(st.tracks.size());
  clip.entityRanges.reserve(st.ranges.size());

  for (const auto &t : st.tracks) {
    if (!t.entity)
      continue;
    EntityID e = world.findByUUID(t.entity);
    if (e == InvalidEntity || !world.isAlive(e))
      continue;
    AnimTrack rt{};
    rt.entity = e;
    rt.blockId = t.blockId;
    rt.channel = t.channel;
    rt.curve = t.curve;
    clip.tracks.push_back(std::move(rt));
  }

  for (const auto &r : st.ranges) {
    if (!r.entity)
      continue;
    EntityID e = world.findByUUID(r.entity);
    if (e == InvalidEntity || !world.isAlive(e))
      continue;
    AnimEntityRange rr{};
    rr.entity = e;
    rr.blockId = r.blockId;
    rr.start = r.start;
    rr.end = std::max<AnimFrame>(r.start, r.end);
    clip.entityRanges.push_back(rr);
  }

  m_anim->clearNla();
  for (const auto &a : st.actions) {
    AnimAction na{};
    na.name = a.name;
    na.start = a.start;
    na.end = a.end;
    na.tracks.reserve(a.tracks.size());
    for (const auto &t : a.tracks) {
      AnimActionTrack at{};
      at.channel = t.channel;
      at.curve = t.curve;
      na.tracks.push_back(std::move(at));
    }
    m_anim->createAction(std::move(na));
  }

  for (const auto &s : st.strips) {
    if (!s.target)
      continue;
    EntityID e = world.findByUUID(s.target);
    if (e == InvalidEntity || !world.isAlive(e))
      continue;
    NlaStrip ns{};
    ns.action = s.action;
    ns.target = e;
    ns.start = s.start;
    ns.end = s.end;
    ns.inFrame = s.inFrame;
    ns.outFrame = s.outFrame;
    ns.timeScale = s.timeScale;
    ns.reverse = s.reverse;
    ns.blend = s.blend;
    ns.influence = s.influence;
    ns.fadeIn = s.fadeIn;
    ns.fadeOut = s.fadeOut;
    ns.layer = s.layer;
    ns.muted = s.muted;
    m_anim->addStrip(ns);
  }

  m_anim->setFps(st.fps);
  m_anim->setFrame(std::clamp<int32_t>(st.frame, 0, clip.lastFrame));
  if (st.playing)
    m_anim->play();
  else
    m_anim->pause();
}

bool EditorHistory::animationStateEqual(const PersistedAnimationStateHist &a,
                                        const PersistedAnimationStateHist &b) const {
  if (a.valid != b.valid)
    return false;
  if (!a.valid)
    return true;
  if (a.name != b.name || a.lastFrame != b.lastFrame || a.loop != b.loop ||
      a.nextBlockId != b.nextBlockId || a.frame != b.frame ||
      a.playing != b.playing || a.fps != b.fps)
    return false;
  if (a.tracks.size() != b.tracks.size() || a.ranges.size() != b.ranges.size() ||
      a.actions.size() != b.actions.size() || a.strips.size() != b.strips.size())
    return false;
  for (size_t i = 0; i < a.tracks.size(); ++i) {
    const auto &x = a.tracks[i];
    const auto &y = b.tracks[i];
    if (x.entity != y.entity || x.blockId != y.blockId || x.channel != y.channel ||
        !animCurvesEqual(x.curve, y.curve))
      return false;
  }
  for (size_t i = 0; i < a.ranges.size(); ++i) {
    const auto &x = a.ranges[i];
    const auto &y = b.ranges[i];
    if (x.entity != y.entity || x.blockId != y.blockId || x.start != y.start ||
        x.end != y.end)
      return false;
  }
  for (size_t i = 0; i < a.actions.size(); ++i) {
    const auto &x = a.actions[i];
    const auto &y = b.actions[i];
    if (x.name != y.name || x.start != y.start || x.end != y.end ||
        x.tracks.size() != y.tracks.size())
      return false;
    for (size_t j = 0; j < x.tracks.size(); ++j) {
      if (x.tracks[j].channel != y.tracks[j].channel ||
          !animCurvesEqual(x.tracks[j].curve, y.tracks[j].curve))
        return false;
    }
  }
  for (size_t i = 0; i < a.strips.size(); ++i) {
    const auto &x = a.strips[i];
    const auto &y = b.strips[i];
    if (x.action != y.action || x.target != y.target || x.start != y.start ||
        x.end != y.end || x.inFrame != y.inFrame || x.outFrame != y.outFrame ||
        x.timeScale != y.timeScale || x.reverse != y.reverse ||
        x.blend != y.blend || x.influence != y.influence ||
        x.fadeIn != y.fadeIn || x.fadeOut != y.fadeOut ||
        x.layer != y.layer || x.muted != y.muted)
      return false;
  }
  return true;
}

} // namespace Nyx
