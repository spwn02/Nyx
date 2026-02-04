// #include "Gizmo.h"
// #include "editor/EditorCamera.h"
// #include "editor/Selection.h"
// #include "glm/fwd.hpp"
// #include "scene/EntityID.h"
// #include "scene/Pick.h"
// #include <cstdint>
// #include <glm/gtc/matrix_transform.hpp>
// #include <imgui.h>
//
// namespace Nyx {
//
// static glm::vec2 projectToScreen(const glm::vec3 &p, const glm::mat4 &VP,
//                                  const glm::vec2 &vmin, const glm::vec2 vmax) {
//   glm::vec4 clip = VP * glm::vec4(p, 1.0f);
//   if (clip.w == 0.0f)
//     clip.w = 1.0f;
//   glm::vec3 ndc = glm::vec3(clip) / clip.w; // -1..1
//   glm::vec2 uv = glm::vec2(ndc.x, -ndc.y) * 0.5f + 0.5f;
//   glm::vec2 sz = vmax - vmin;
//   return vmin + uv * sz;
// }
//
// static float distToSegment2D(const glm::vec2 &p, const glm::vec2 &a,
//                              const glm::vec2 &b) {
//   const glm::vec2 ab = b - a;
//   const float t =
//       glm::clamp(glm::dot(p - a, ab) / glm::dot(ab, ab), 0.0f, 1.0f);
//   const glm::vec2 q = a + t * ab;
//   return glm::length(p - q);
// }
//
// void Gizmo::drawAndInteract(World &world, Selection &sel,
//                             const EditorCameraState &cam,
//                             const glm::vec2 &viewportMin,
//                             const glm::vec2 &viewportMax) {
//   if (sel.kind != SelectionKind::Picks || sel.picks.empty())
//     return;
//
//   const uint32_t pick = sel.activePick ? sel.activePick : sel.picks.front();
//   const EntityID slot = pickEntity(pick);
//   const EntityID e = world.entityFromSlotIndex(slot);
//   if (!world.isAlive(e))
//     return;
//
//   auto &t = world.transform(e);
//   const glm::vec3 origin = t.translation;
//
//   const glm::mat4 VP = cam.proj * cam.view;
//
//   // axis endpoints (world)
//   const float axisLen = 1.0f;
//   const glm::vec3 ax[3] = {origin + glm::vec3(axisLen, 0, 0),
//                            origin + glm::vec3(0, axisLen, 0),
//                            origin + glm::vec3(0, 0, axisLen)};
//
//   // screen points
//   const glm::vec2 s0 = projectToScreen(origin, VP, viewportMin, viewportMax);
//   const glm::vec2 sx = projectToScreen(ax[0], VP, viewportMin, viewportMax);
//   const glm::vec2 sy = projectToScreen(ax[1], VP, viewportMin, viewportMax);
//   const glm::vec2 sz = projectToScreen(ax[2], VP, viewportMin, viewportMax);
//
//   ImDrawList *dl = ImGui::GetForegroundDrawList();
//
//   // draw
//   dl->AddLine(ImVec2(s0.x, s0.y), ImVec2(sx.x, sx.y),
//               IM_COL32(255, 80, 80, 255), 2.0f);
//   dl->AddLine(ImVec2(s0.x, s0.y), ImVec2(sy.x, sy.y),
//               IM_COL32(80, 255, 80, 255), 2.0f);
//   dl->AddLine(ImVec2(s0.x, s0.y), ImVec2(sz.x, sz.y),
//               IM_COL32(80, 80, 255, 255), 2.0f);
//
//   // interaction
//   const glm::vec2 mouse = {ImGui::GetIO().MousePos.x,
//                            ImGui::GetIO().MousePos.y};
//   const bool mouseInViewport =
//       mouse.x >= viewportMin.x && mouse.x <= viewportMax.x &&
//       mouse.y >= viewportMin.y && mouse.y <= viewportMax.y;
//
//   if (!mouseInViewport)
//     return;
//
//   const float hitPx = 8.0f;
//   const float dx = distToSegment2D(mouse, s0, sx);
//   const float dy = distToSegment2D(mouse, s0, sy);
//   const float dz = distToSegment2D(mouse, s0, sz);
//
//   int hotAxis = -1;
//   float best = hitPx;
//   if (dx < best) {
//     best = dx;
//     hotAxis = 0;
//   }
//   if (dy < best) {
//     best = dy;
//     hotAxis = 1;
//   }
//   if (dz < best) {
//     best = dz;
//     hotAxis = 2;
//   }
//
//   // start drag
//   if (!m_dragging && hotAxis >= 0 &&
//       ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
//     m_dragging = true;
//     m_axis = hotAxis;
//     m_startTranslation = t.translation;
//   }
//
//   // drag update (screen-space approximation: move along axis proportional to
//   // mouse delta)
//   if (m_dragging) {
//     if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
//       const glm::vec2 d = {ImGui::GetIO().MouseDelta.x,
//                            ImGui::GetIO().MouseDelta.y};
//       // convrt pixels to world units
//       const float scale = 0.01f;
//       glm::vec3 delta(0);
//       if (m_axis == 0)
//         delta.x = d.x * scale;
//       else if (m_axis == 1)
//         delta.y = -d.y * scale;
//       else if (m_axis == 2)
//         delta.z = d.x * scale;
//       t.translation += delta;
//       t.dirty = true;
//       if (world.hasWorldTransform(e))
//         world.worldTransform(e).dirty = true;
//     } else {
//       m_dragging = false;
//       m_axis = -1;
//     }
//   }
// }
//
// } // namespace Nyx
