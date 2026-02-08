#pragma once

#include <imgui_internal.h>
#include <cstdint>
#include <vector>

namespace Nyx {

struct AnimationClip;

class CurveEditorPanel final {
public:
  struct PresetDef {
    enum class Family : uint8_t { Bezier, Bounce };
    enum class EaseMode : uint8_t { In, Out, InOut };
    const char *name = "";
    float x1 = 0.0f;
    float y1 = 0.0f;
    float x2 = 1.0f;
    float y2 = 1.0f;
    Family family = Family::Bezier;
    EaseMode mode = EaseMode::InOut;
  };

  void setClip(AnimationClip *clip) { m_clip = clip; }
  void setActiveTrack(int trackIndex);
  int activeTrack() const { return m_trackIndex; }
  void setFrameWindow(int32_t firstFrame, float pixelsPerFrame);
  void setCurrentFrame(int32_t frame) { m_currentFrame = frame; }

  void onImGui();

private:
  AnimationClip *m_clip = nullptr;
  int m_trackIndex = -1;

  float m_pixelsPerFrame = 12.0f;
  float m_pixelsPerValue = 24.0f;
  float m_panY = 0.0f;
  int32_t m_firstFrame = 0;
  int32_t m_currentFrame = 0;

  int m_activeKey = -1;
  bool m_draggingKey = false;
  float m_dragKeyOffsetValue = 0.0f;
  int32_t m_dragKeyOffsetFrame = 0;
  bool m_panning = false;
  enum class HandleHit : uint8_t { None, In, Out };
  HandleHit m_draggingHandle = HandleHit::None;
  float m_dragHandleOffsetDx = 0.0f;
  float m_dragHandleOffsetDy = 0.0f;
  bool m_boxSelecting = false;
  bool m_boxSelectAdditive = false;
  ImVec2 m_boxStart = ImVec2(0.0f, 0.0f);
  ImVec2 m_boxEnd = ImVec2(0.0f, 0.0f);
  std::vector<int> m_selectedKeys;
  bool m_fitPending = true;
  bool m_showPresetPanel = true;

private:
  void drawGrid(const ImRect &r) const;
  void drawCurve(const ImRect &r) const;
  void drawKeys(const ImRect &r) const;
  void drawCurrentFrameLine(const ImRect &r) const;
  void fitViewToKeys(const ImRect &r, bool selectedOnly);
  bool isKeySelected(int keyIndex) const;
  void selectSingleKey(int keyIndex);
  HandleHit hitTestHandle(const ImRect &r, int keyIndex, const ImVec2 &mp) const;
  float frameToX(int32_t frame, float x0) const;
  float frameToXf(float frame, float x0) const;
  float valueToY(float value, float y0) const;
  int32_t xToFrame(float x, float x0) const;
  float yToValue(float y, float y0) const;
  void drawPresetPanel();
  static void drawPresetPreview(const PresetDef &p, const ImVec2 &size);
  void applyPresetToActiveTrack(const PresetDef &p);
};

} // namespace Nyx
