#include "editor/CameraFrameOverlay.h"

#include <imgui.h>

namespace Nyx {

void CameraFrameOverlay::draw(const glm::vec2 &imgMin, const glm::vec2 &imgMax,
                              bool enabled) {
  if (!enabled)
    return;

  ImDrawList *dl = ImGui::GetWindowDrawList();
  if (!dl)
    return;

  const ImVec2 a(imgMin.x, imgMin.y);
  const ImVec2 b(imgMax.x, imgMax.y);

  dl->AddRectFilled(a, b, IM_COL32(0, 0, 0, 20));
  dl->AddRect(a, b, IM_COL32(255, 255, 255, 140), 0.0f, 0, 2.0f);

  const float w = b.x - a.x;
  const float h = b.y - a.y;
  const ImVec2 c(a.x + w * 0.5f, a.y + h * 0.5f);

  dl->AddLine(ImVec2(c.x, a.y), ImVec2(c.x, b.y), IM_COL32(255, 255, 255, 50),
              1.0f);
  dl->AddLine(ImVec2(a.x, c.y), ImVec2(b.x, c.y), IM_COL32(255, 255, 255, 50),
              1.0f);

  const ImVec2 ia(a.x + w * 0.1f, a.y + h * 0.1f);
  const ImVec2 ib(b.x - w * 0.1f, b.y - h * 0.1f);
  dl->AddRect(ia, ib, IM_COL32(255, 255, 255, 60), 0.0f, 0, 1.0f);
}

} // namespace Nyx
