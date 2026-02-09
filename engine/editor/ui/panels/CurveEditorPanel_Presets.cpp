#include "CurveEditorPanel.h"

#include "animation/AnimationTypes.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <imgui.h>

namespace Nyx {

static float cubicEval(float p0, float p1, float p2, float p3, float t) {
  const float u = 1.0f - t;
  return u * u * u * p0 + 3.0f * u * u * t * p1 + 3.0f * u * t * t * p2 +
         t * t * t * p3;
}

static float easeBounceOut(float t) {
  const float n1 = 7.5625f;
  const float d1 = 2.75f;
  if (t < 1.0f / d1) {
    return n1 * t * t;
  } else if (t < 2.0f / d1) {
    t -= 1.5f / d1;
    return n1 * t * t + 0.75f;
  } else if (t < 2.5f / d1) {
    t -= 2.25f / d1;
    return n1 * t * t + 0.9375f;
  }
  t -= 2.625f / d1;
  return n1 * t * t + 0.984375f;
}

static float easeBounceIn(float t) { return 1.0f - easeBounceOut(1.0f - t); }

static float easeBounceInOut(float t) {
  return (t < 0.5f) ? (1.0f - easeBounceOut(1.0f - 2.0f * t)) * 0.5f
                    : (1.0f + easeBounceOut(2.0f * t - 1.0f)) * 0.5f;
}

static float easeEval(const CurveEditorPanel::PresetDef &p, float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  if (p.family == CurveEditorPanel::PresetDef::Family::Bounce) {
    if (p.mode == CurveEditorPanel::PresetDef::EaseMode::In)
      return easeBounceIn(t);
    if (p.mode == CurveEditorPanel::PresetDef::EaseMode::Out)
      return easeBounceOut(t);
    return easeBounceInOut(t);
  }
  return cubicEval(0.0f, p.y1, p.y2, 1.0f, t);
}

static SegmentEase toSegmentEase(const CurveEditorPanel::PresetDef &p) {
  using Fam = CurveEditorPanel::PresetDef::Family;
  using Mode = CurveEditorPanel::PresetDef::EaseMode;
  if (p.family == Fam::Bounce) {
    if (p.mode == Mode::In)
      return SegmentEase::BounceIn;
    if (p.mode == Mode::Out)
      return SegmentEase::BounceOut;
    return SegmentEase::BounceInOut;
  }
  if (p.name && std::strstr(p.name, "Cubic") == p.name) {
    if (p.mode == Mode::In)
      return SegmentEase::CubicIn;
    if (p.mode == Mode::Out)
      return SegmentEase::CubicOut;
    return SegmentEase::CubicInOut;
  }
  if (p.name && std::strstr(p.name, "Quint") == p.name) {
    if (p.mode == Mode::In)
      return SegmentEase::QuintIn;
    if (p.mode == Mode::Out)
      return SegmentEase::QuintOut;
    return SegmentEase::QuintInOut;
  }
  if (p.name && std::strstr(p.name, "Exponential") == p.name) {
    if (p.mode == Mode::In)
      return SegmentEase::ExponentialIn;
    if (p.mode == Mode::Out)
      return SegmentEase::ExponentialOut;
    return SegmentEase::ExponentialInOut;
  }
  if (p.mode == Mode::In)
    return SegmentEase::BackIn;
  if (p.mode == Mode::Out)
    return SegmentEase::BackOut;
  return SegmentEase::BackInOut;
}

void CurveEditorPanel::drawPresetPreview(const PresetDef &p, const ImVec2 &size) {
  ImGui::InvisibleButton("##PresetPreview", size);
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const ImVec2 a = ImGui::GetItemRectMin();
  const ImVec2 b = ImGui::GetItemRectMax();
  dl->AddRectFilled(a, b, IM_COL32(20, 20, 20, 255));
  dl->AddRect(a, b, IM_COL32(70, 70, 70, 255));
  const float w = std::max(1.0f, b.x - a.x - 6.0f);
  const float h = std::max(1.0f, b.y - a.y - 6.0f);
  ImVec2 prev(a.x + 3.0f, b.y - 3.0f);
  for (int i = 1; i <= 30; ++i) {
    const float t = (float)i / 30.0f;
    const float x = t;
    const float y = easeEval(p, t);
    const ImVec2 cur(a.x + 3.0f + x * w, b.y - 3.0f - y * h);
    dl->AddLine(prev, cur, IM_COL32(255, 200, 110, 255), 1.8f);
    prev = cur;
  }
}

void CurveEditorPanel::applyPresetToActiveTrack(const PresetDef &p) {
  if (!m_clip || m_trackIndex < 0 || m_trackIndex >= (int)m_clip->tracks.size())
    return;
  auto &curve = m_clip->tracks[(size_t)m_trackIndex].curve;
  auto &keys = curve.keys;
  if (keys.size() < 2)
    return;

  std::vector<int> idx;
  if (m_selectedKeys.size() >= 2) {
    idx = m_selectedKeys;
    std::sort(idx.begin(), idx.end());
    idx.erase(std::unique(idx.begin(), idx.end()), idx.end());
  } else {
    idx.reserve(keys.size());
    for (int i = 0; i < (int)keys.size(); ++i)
      idx.push_back(i);
  }
  if (idx.size() < 2)
    return;

  struct Span {
    int32_t af = 0;
    int32_t bf = 0;
    float av = 0.0f;
    float bv = 0.0f;
    int ia = -1;
    int ib = -1;
  };
  std::vector<Span> spans;
  for (size_t ii = 1; ii < idx.size(); ++ii) {
    const int ia = idx[ii - 1];
    const int ib = idx[ii];
    if (ia < 0 || ib < 0 || ia >= (int)keys.size() || ib >= (int)keys.size() ||
        ia == ib)
      continue;
    const AnimKey &a = keys[(size_t)ia];
    const AnimKey &b = keys[(size_t)ib];
    if (b.frame <= a.frame)
      continue;
    spans.push_back(Span{a.frame, b.frame, a.value, b.value, ia, ib});
  }

  const SegmentEase ease = toSegmentEase(p);
  for (const Span &s : spans) {
    AnimKey &a = keys[(size_t)s.ia];
    AnimKey &b = keys[(size_t)s.ib];
    a.easeOut = ease;
    a.out.dx = a.out.dy = 0.0f;
    b.in.dx = b.in.dy = 0.0f;
  }
  curve.interp = InterpMode::Linear;
}

void CurveEditorPanel::drawPresetPanel() {
  if (!m_showPresetPanel)
    return;

  static const PresetDef presets[] = {
      {"Cubic In", 0.55f, 0.055f, 0.675f, 0.19f, PresetDef::Family::Bezier,
       PresetDef::EaseMode::In},
      {"Cubic Out", 0.215f, 0.61f, 0.355f, 1.0f, PresetDef::Family::Bezier,
       PresetDef::EaseMode::Out},
      {"Cubic InOut", 0.645f, 0.045f, 0.355f, 1.0f, PresetDef::Family::Bezier,
       PresetDef::EaseMode::InOut},
      {"Quint In", 0.755f, 0.05f, 0.855f, 0.06f, PresetDef::Family::Bezier,
       PresetDef::EaseMode::In},
      {"Quint Out", 0.23f, 1.0f, 0.32f, 1.0f, PresetDef::Family::Bezier,
       PresetDef::EaseMode::Out},
      {"Quint InOut", 0.86f, 0.0f, 0.07f, 1.0f, PresetDef::Family::Bezier,
       PresetDef::EaseMode::InOut},
      {"Exponential In", 0.95f, 0.05f, 0.795f, 0.035f,
       PresetDef::Family::Bezier, PresetDef::EaseMode::In},
      {"Exponential Out", 0.19f, 1.0f, 0.22f, 1.0f, PresetDef::Family::Bezier,
       PresetDef::EaseMode::Out},
      {"Exponential InOut", 1.0f, 0.0f, 0.0f, 1.0f,
       PresetDef::Family::Bezier, PresetDef::EaseMode::InOut},
      {"Back In", 0.6f, -0.28f, 0.735f, 0.045f, PresetDef::Family::Bezier,
       PresetDef::EaseMode::In},
      {"Back Out", 0.175f, 0.885f, 0.32f, 1.275f, PresetDef::Family::Bezier,
       PresetDef::EaseMode::Out},
      {"Back InOut", 0.68f, -0.55f, 0.265f, 1.55f, PresetDef::Family::Bezier,
       PresetDef::EaseMode::InOut},
      {"Bounce In", 0.0f, 0.0f, 1.0f, 1.0f, PresetDef::Family::Bounce,
       PresetDef::EaseMode::In},
      {"Bounce Out", 0.0f, 0.0f, 1.0f, 1.0f, PresetDef::Family::Bounce,
       PresetDef::EaseMode::Out},
      {"Bounce InOut", 0.0f, 0.0f, 1.0f, 1.0f, PresetDef::Family::Bounce,
       PresetDef::EaseMode::InOut},
  };

  if (!ImGui::Begin("Curve Presets", &m_showPresetPanel)) {
    ImGui::End();
    return;
  }

  ImGui::TextUnformatted(
      "Apply to selected key spans (or full track if no multi-key selection).");
  ImGui::Separator();
  ImGui::BeginChild("##PresetList", ImVec2(0.0f, 0.0f), false,
                    ImGuiWindowFlags_AlwaysVerticalScrollbar);

  for (int i = 0; i < (int)(sizeof(presets) / sizeof(presets[0])); ++i) {
    ImGui::PushID(i);
    drawPresetPreview(presets[i], ImVec2(110.0f, 34.0f));
    ImGui::SameLine();
    if (ImGui::Button(presets[i].name, ImVec2(180.0f, 34.0f))) {
      applyPresetToActiveTrack(presets[i]);
    }
    ImGui::PopID();
  }

  ImGui::EndChild();
  ImGui::End();
}

} // namespace Nyx
