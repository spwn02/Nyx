#include "AnimationSystem_Keying.h"

#include "AnimKeying.h"
#include "AnimationSystem.h"
#include "scene/World.h"

#include <glm/gtx/quaternion.hpp>

namespace Nyx {

static bool allowedEntity(const KeyingTarget &t, EntityID e) {
  return (t.restrictEntity == InvalidEntity) || (t.restrictEntity == e);
}

void AnimationSystemKeying::keyAllTRS(EntityID e, AnimFrame frame,
                                      const float *rotationEulerDeg) {
  if (!m_world || !m_anim || !m_world->isAlive(e))
    return;
  if (!allowedEntity(m_target, e) || m_target.action == 0)
    return;
  if (!m_anim->action(m_target.action))
    return;

  if (m_settings.keyTranslate)
    keyTranslate(e, frame);
  if (m_settings.keyRotate)
    keyRotate(e, frame, rotationEulerDeg);
  if (m_settings.keyScale)
    keyScale(e, frame);
}

void AnimationSystemKeying::onTransformEdited(EntityID e, AnimFrame frame,
                                              const float *rotationEulerDeg) {
  if (!m_settings.autoKey)
    return;
  keyAllTRS(e, frame, rotationEulerDeg);
}

void AnimationSystemKeying::keyTranslate(EntityID e, AnimFrame frame) {
  AnimAction *a = m_anim->action(m_target.action);
  if (!a)
    return;

  const auto &tr = m_world->transform(e);
  keyValue(*a, AnimChannel::TranslateX, frame, tr.translation.x, m_settings.mode);
  keyValue(*a, AnimChannel::TranslateY, frame, tr.translation.y, m_settings.mode);
  keyValue(*a, AnimChannel::TranslateZ, frame, tr.translation.z, m_settings.mode);
}

void AnimationSystemKeying::keyRotate(EntityID e, AnimFrame frame,
                                      const float *rotationEulerDeg) {
  AnimAction *a = m_anim->action(m_target.action);
  if (!a)
    return;

  glm::vec3 deg{};
  if (rotationEulerDeg) {
    deg = glm::vec3(rotationEulerDeg[0], rotationEulerDeg[1], rotationEulerDeg[2]);
  } else {
    const auto &tr = m_world->transform(e);
    deg = glm::degrees(glm::eulerAngles(tr.rotation));
  }
  keyValue(*a, AnimChannel::RotateX, frame, deg.x, m_settings.mode);
  keyValue(*a, AnimChannel::RotateY, frame, deg.y, m_settings.mode);
  keyValue(*a, AnimChannel::RotateZ, frame, deg.z, m_settings.mode);
}

void AnimationSystemKeying::keyScale(EntityID e, AnimFrame frame) {
  AnimAction *a = m_anim->action(m_target.action);
  if (!a)
    return;

  const auto &tr = m_world->transform(e);
  keyValue(*a, AnimChannel::ScaleX, frame, tr.scale.x, m_settings.mode);
  keyValue(*a, AnimChannel::ScaleY, frame, tr.scale.y, m_settings.mode);
  keyValue(*a, AnimChannel::ScaleZ, frame, tr.scale.z, m_settings.mode);
}

} // namespace Nyx

