#include "editor/ViewportPick.h"

#include <algorithm>
#include <cmath>

namespace Nyx {

ViewportPickResult mapMouseToFramebufferPixel(double mouseX, double mouseY,
                                              const ViewportImageRect &r) {
  ViewportPickResult out{};

  const float minX = r.imageMin.x;
  const float minY = r.imageMin.y;
  const float maxX = r.imageMax.x;
  const float maxY = r.imageMax.y;

  const float w = std::max(1.0f, maxX - minX);
  const float h = std::max(1.0f, maxY - minY);

  const float u = float((mouseX - (double)minX) / (double)w);
  const float v = float((mouseY - (double)minY) / (double)h);

  out.u = u;
  out.v = v;
  out.inside = (u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f);

  if (!out.inside)
    return out;

  const uint32_t fbW = std::max(1u, r.renderedSize.x);
  const uint32_t fbH = std::max(1u, r.renderedSize.y);

  const uint32_t px =
      (uint32_t)std::min<double>(fbW - 1u, std::floor(u * (double)fbW));
  const uint32_t py =
      (uint32_t)std::min<double>(fbH - 1u, std::floor(v * (double)fbH));

  out.px = px;
  out.py = py;
  return out;
}

} // namespace Nyx
