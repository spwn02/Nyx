#pragma once

#include "AnimNLA.h"
#include "AnimationTypes.h"
#include <vector>

namespace Nyx {

class World;

// Central evaluator (editor + runtime safe)
class AnimationSystem final {
public:
  void setWorld(World *world) { m_world = world; }

  void setActiveClip(AnimationClip *clip) { m_active = clip; }

  // NLA action/strip API (additive to active-clip workflow).
  ActionID createAction(AnimAction a);
  AnimAction *action(ActionID id);
  const AnimAction *action(ActionID id) const;

  uint32_t addStrip(const NlaStrip &s);
  bool removeStrip(uint32_t stripIndex);
  void clearNla();

  std::vector<NlaStrip> &strips() { return m_strips; }
  const std::vector<NlaStrip> &strips() const { return m_strips; }
  std::vector<AnimAction> &actions() { return m_actions; }
  const std::vector<AnimAction> &actions() const { return m_actions; }

  void setFrame(AnimFrame frame);
  void tick(float dt); // advance if playing

  void play();
  void pause();
  void toggle();

  AnimFrame frame() const { return m_frame; }
  bool playing() const { return m_playing; }
  float fps() const { return m_fps; }
  void setFps(float fps);

private:
  World *m_world = nullptr;
  AnimationClip *m_active = nullptr;

  AnimFrame m_frame = 0;
  bool m_playing = false;
  float m_fps = 30.0f;
  float m_accum = 0.0f;

  std::vector<AnimAction> m_actions;
  std::vector<NlaStrip> m_strips;

  void evaluate();
  void evaluateClip();
  void evaluateNla();
  void updateDisabledAnim();
  static float stripWeightAt(const NlaStrip &s, AnimFrame frame);
  static AnimFrame mapToActionFrame(const NlaStrip &s, const AnimAction &a,
                                    AnimFrame globalFrame);
};

} // namespace Nyx
