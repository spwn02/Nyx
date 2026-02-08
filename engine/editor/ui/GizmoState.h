#pragma once

#include <cstdint>

namespace Nyx {

enum class GizmoOp : uint8_t { Translate = 0, Rotate, Scale };
enum class GizmoMode : uint8_t { Local = 0, World };

struct GizmoState {
  GizmoOp op = GizmoOp::Translate;
  GizmoMode mode = GizmoMode::Local;

  bool useSnap = false;
  float snapTranslate = 0.5f;
  float snapRotateDeg = 15.0f;
  float snapScale = 0.1f;

  bool propagateChildren = true;
};

} // namespace Nyx
