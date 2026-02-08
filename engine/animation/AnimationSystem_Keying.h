#pragma once

#include "AnimKeying.h"

namespace Nyx {

class World;
class AnimationSystem;

class AnimationSystemKeying final {
public:
  void setWorld(World *w) { m_world = w; }
  void setAnim(AnimationSystem *a) { m_anim = a; }

  void setKeyingTarget(KeyingTarget t) { m_target = t; }
  const KeyingTarget &keyingTarget() const { return m_target; }

  void setSettings(KeyingSettings s) { m_settings = s; }
  KeyingSettings &settings() { return m_settings; }
  const KeyingSettings &settings() const { return m_settings; }

  void keyAllTRS(EntityID e, AnimFrame frame, const float *rotationEulerDeg = nullptr);
  void onTransformEdited(EntityID e, AnimFrame frame,
                         const float *rotationEulerDeg = nullptr);

private:
  World *m_world = nullptr;
  AnimationSystem *m_anim = nullptr;

  KeyingTarget m_target{};
  KeyingSettings m_settings{};

  void keyTranslate(EntityID e, AnimFrame frame);
  void keyRotate(EntityID e, AnimFrame frame, const float *rotationEulerDeg);
  void keyScale(EntityID e, AnimFrame frame);
};

} // namespace Nyx

