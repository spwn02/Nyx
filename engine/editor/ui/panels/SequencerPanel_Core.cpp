#include "SequencerPanel.h"

#include "animation/AnimationSystem.h"
#include "animation/AnimationTypes.h"
#include "animation/AnimKeying.h"
#include "scene/World.h"
#include "core/Paths.h"

#include "input/InputSystem.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <imgui.h>
#include <glm/gtx/quaternion.hpp>

namespace Nyx {

static int32_t clampi(int32_t v, int32_t a, int32_t b) {
  if (v < a)
    return a;
  if (v > b)
    return b;
  return v;
}

static float clampf(float v, float a, float b) {
  if (v < a)
    return a;
  if (v > b)
    return b;
  return v;
}

static ImU32 brightenColor(ImU32 c, float mul) {
  int r = (int)(float((c >> IM_COL32_R_SHIFT) & 0xFF) * mul);
  int g = (int)(float((c >> IM_COL32_G_SHIFT) & 0xFF) * mul);
  int b = (int)(float((c >> IM_COL32_B_SHIFT) & 0xFF) * mul);
  const int a = (int)((c >> IM_COL32_A_SHIFT) & 0xFF);
  r = std::clamp(r, 0, 255);
  g = std::clamp(g, 0, 255);
  b = std::clamp(b, 0, 255);
  return IM_COL32(r, g, b, a);
}

static uint64_t packRowKey(EntityID e, SeqRowType type, SeqProperty prop) {
  const uint64_t a = (uint64_t)e.index;
  const uint64_t b = (uint64_t)e.generation;
  const uint64_t t = (uint64_t)type;
  const uint64_t p = (uint64_t)prop;
  return (a << 32) ^ (b << 16) ^ (t << 8) ^ p;
}

static bool drawAtlasIconButton(const IconAtlas &atlas, const char *name,
                                const ImVec2 &size, ImU32 tint) {
  const AtlasRegion *r = atlas.find(name);
  if (!r)
    return ImGui::SmallButton("?");
  ImGui::InvisibleButton(name, size);
  ImDrawList *dl = ImGui::GetWindowDrawList();
  ImVec2 p0 = ImGui::GetItemRectMin();
  ImVec2 p1 = ImGui::GetItemRectMax();
  dl->AddImage(atlas.imguiTexId(), p0, p1, r->uv0, r->uv1, tint);
  return ImGui::IsItemClicked();
}

static const char *channelName(AnimChannel c) {
  switch (c) {
  case AnimChannel::TranslateX:
    return "T.X";
  case AnimChannel::TranslateY:
    return "T.Y";
  case AnimChannel::TranslateZ:
    return "T.Z";
  case AnimChannel::RotateX:
    return "R.X";
  case AnimChannel::RotateY:
    return "R.Y";
  case AnimChannel::RotateZ:
    return "R.Z";
  case AnimChannel::ScaleX:
    return "S.X";
  case AnimChannel::ScaleY:
    return "S.Y";
  case AnimChannel::ScaleZ:
    return "S.Z";
  default:
    return "Ch";
  }
}

static bool isNlaSelectId(uint32_t id) { return (id & 0x80000000u) != 0u; }
static uint32_t nlaSelectIdFromIndex(int idx) {
  return 0x80000000u | (uint32_t)(idx + 1);
}
static int nlaIndexFromSelectId(uint32_t id) {
  return (int)((id & 0x7fffffffu) - 1u);
}

float SequencerPanel::frameToX(int32_t frame, int32_t firstFrame,
                               float xStart) const {
  return xStart + float(frame - firstFrame) * m_pixelsPerFrame;
}

int32_t SequencerPanel::xToFrame(float x, int32_t firstFrame,
                                 float xStart) const {
  const float localX = x - xStart;
  return firstFrame +
         (int32_t)std::floor(localX / m_pixelsPerFrame + 0.5f);
}

int32_t SequencerPanel::clampFrame(int32_t f) const {
  if (!m_clip)
    return std::max<int32_t>(0, f);
  const int32_t last = std::max<int32_t>(0, m_clip->lastFrame);
  return clampi(f, 0, last);
}

uint64_t SequencerPanel::rowKey(EntityID e, SeqRowType type,
                                SeqProperty prop) const {
  return packRowKey(e, type, prop);
}

bool SequencerPanel::isLayerHidden(EntityID e) const {
  if (!m_world || !m_world->isAlive(e))
    return false;
  return m_world->transform(e).hidden;
}

ImU32 SequencerPanel::layerColor(EntityID e) const {
  if (!m_world || !m_world->isAlive(e))
    return IM_COL32(90, 90, 90, 255);
  if (m_world->hasCamera(e))
    return IM_COL32(80, 140, 255, 255);
  if (m_world->hasLight(e))
    return IM_COL32(255, 210, 80, 255);
  if (m_world->hasMesh(e))
    return IM_COL32(90, 200, 180, 255);
  return IM_COL32(120, 120, 120, 255);
}

#include "sequencer/SequencerPanel_TimelineMethods.inl"
