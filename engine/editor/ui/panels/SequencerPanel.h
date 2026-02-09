#pragma once

#include "imgui_internal.h"
#include "animation/AnimNLA.h"
#include "animation/AnimKeying.h"
#include "animation/AnimationTypes.h"
#include "editor/SequencerState.h"
#include "editor/tools/IconAtlas.h"
#include "CurveEditorPanel.h"
#include "scene/EntityID.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "scene/EntityID.h"

namespace Nyx {

class World;
class AnimationSystem;
struct AnimationClip;
class InputSystem;

// Simple marker (for "significant events")
struct SeqMarker {
  int32_t frame = 0;
  std::string label;
};

enum class SeqRowType : uint8_t { Layer, Group, Property, Stub };
enum class SeqProperty : uint8_t {
  Position,
  Rotation,
  Scale,
  Opacity,
  Audio,
  Masks
};

enum class SeqSortMode : uint8_t {
  SceneOrder,
  NameAZ,
  NameZA,
  Parent,
  Type
};

struct SeqRow {
  SeqRowType type = SeqRowType::Layer;
  EntityID entity = InvalidEntity;
  SeqProperty prop = SeqProperty::Position;
  int depth = 0;
  bool expanded = false;
};

// Track/key selection + clipboard
struct SeqKeyRef {
  int32_t trackIndex = -1;
  int32_t keyIndex = -1;
  bool operator==(const SeqKeyRef &o) const {
    return trackIndex == o.trackIndex && keyIndex == o.keyIndex;
  }
};

struct SeqKeyCopy {
  int32_t trackIndex = -1; // absolute (simple for now)
  int32_t frame = 0;
  float value = 0.0f;
};

// Bottom timeline panel (Blender-ish)
// - play/pause (space handled outside, but panel exposes helpers)
// - scrub frames
// - dynamic lastFrame
// - show tracks + key dots (no curve editor yet)
class SequencerPanel final {
public:
  enum TransformEditMask : uint32_t {
    EditTranslate = 1u << 0,
    EditRotate = 1u << 1,
    EditScale = 1u << 2
  };
  void setWorld(World *w) { m_world = w; }
  void setAnimationSystem(AnimationSystem *anim) { m_anim = anim; }
  void setAnimationClip(AnimationClip *clip) {
    if (m_clip != clip) {
      m_clip = clip;
      clearSelection();
      m_rangeUserEdited.clear();
      m_viewFirstFrame = 0;
      markLayoutDirty();
      invalidateTrackIndexCache();
    }
  }

  // Optional external shortcuts can call these
  void togglePlay();
  void stop();
  void step(int32_t delta);

  // ImGui draw
  void draw();

  // Markers API (optional)
  std::vector<SeqMarker> &markers() { return m_markers; }
  bool timelineHot() const { return m_timelineHovered || m_timelineActive; }
  void handleStepRepeat(const InputSystem &input, float dt);
  void updateHiddenEntities();
  void onTransformEditEnd(EntityID e, uint32_t mask,
                          const float *rotationEulerDeg = nullptr);
  void setHiddenExclusions(const std::vector<EntityID> &ents);
  void setTrackExclusions(const std::vector<EntityID> &ents);
  const std::vector<EntityID> &hiddenEntities() const {
    return m_hiddenEntities;
  }
  void capturePersistState(SequencerPersistState &out) const;
  void applyPersistState(const SequencerPersistState &in);

private:
  World *m_world = nullptr;
  AnimationSystem *m_anim = nullptr;
  AnimationClip *m_clip = nullptr;

  std::vector<SeqMarker> m_markers;

  // UI state
  float m_rowHeight = 20.0f;
  float m_headerHeight = 56.0f;
  float m_timelineHeight = 220.0f;
  float m_rulerHeight = 22.0f;

  bool m_autoUpdateLastFrame = true;

  // Selection + editing
  std::vector<SeqKeyRef> m_selectedKeys;
  SeqKeyRef m_activeKey{};
  bool m_draggingKey = false;
  int32_t m_dragStartFrame = 0;
  int32_t m_dragOrigKeyFrame = 0;

  // Clipboard
  std::vector<SeqKeyCopy> m_clipboard;
  mutable std::vector<int32_t> m_frameScratch;
  mutable std::unordered_map<int32_t, SeqKeyRef> m_frameToKeyScratch;

  // Cached layout
  float m_pixelsPerFrame = 12.0f;
  float m_minPixelsPerFrame = 1.0f;
  bool m_timelineHovered = false;
  bool m_timelineActive = false;
  float m_lastDrawMs = 0.0f;
  bool m_layoutDirty = true;
  uint64_t m_layoutSignature = 0;
  float m_repeatDelay = 0.35f;
  float m_repeatRate = 0.06f;
  float m_repeatTimer = 0.0f;
  int m_repeatDir = 0;
  int32_t m_viewFirstFrame = 0;
  bool m_panningTimeline = false;
  float m_panStartMouseX = 0.0f;
  int32_t m_panStartFirstFrame = 0;

  // Layout constants
  float m_labelGutter = 200.0f;
  float m_labelGutterMin = 0.0f;
  float m_labelGutterMax = 400.0f;
  bool m_labelGutterDragging = false;
  bool m_draggingFrameLine = false;
  std::vector<EntityID> m_rowEntities;
  std::vector<SeqRow> m_rows;
  std::unordered_map<uint64_t, bool> m_expandState;
  std::unordered_map<uint64_t, bool> m_stopwatchState;
  mutable std::unordered_map<uint64_t, int> m_trackIndexCache;
  mutable bool m_trackIndexCacheDirty = true;
  std::unordered_set<EntityID, EntityHash> m_isolated;
  char m_searchBuf[128]{};
  SeqSortMode m_sortMode = SeqSortMode::SceneOrder;
  bool m_showGraphPanel = false;
  bool m_iconInit = false;
  bool m_iconReady = false;
  IconAtlas m_iconAtlas{};
  std::unordered_map<EntityID, int32_t, EntityHash> m_entityEndFrame;
  std::unordered_map<EntityID, int32_t, EntityHash> m_entityStartFrame;
  std::unordered_set<EntityID, EntityHash> m_rangeUserEdited;
  std::vector<EntityID> m_hiddenEntities;
  std::unordered_set<EntityID, EntityHash> m_hiddenExclude;
  std::unordered_set<EntityID, EntityHash> m_trackExclude;
  bool m_draggingDuration = false;
  EntityID m_dragDurationEntity = InvalidEntity;
  int m_dragDurationRangeIndex = -1;
  int32_t m_dragDurationStartFrame = 0;
  int32_t m_dragDurationOrigStart = 0;
  int32_t m_dragDurationOrigEnd = 0;
  int m_dragDurationMode = 0; // 0 none, 1 move, 2 crop-start, 3 crop-end
  bool m_cutToolActive = false;
  bool m_draggingProperty = false;
  EntityID m_dragPropEntity = InvalidEntity;
  SeqProperty m_dragProp = SeqProperty::Position;
  int32_t m_dragPropStartFrame = 0;
  int32_t m_dragPropOrigFrame = 0;
  bool m_boxSelecting = false;
  ImVec2 m_boxSelectStart = ImVec2(0.0f, 0.0f);
  ImVec2 m_boxSelectEnd = ImVec2(0.0f, 0.0f);
  bool m_boxSelectAdditive = false;
  std::unordered_set<EntityID, EntityHash> m_selectedLayerBlocks;
  std::unordered_set<uint32_t> m_selectedRangeBlocks;
  int m_graphTrackIndex = -1;
  KeyingSettings m_nlaKeying{};
  ActionID m_nlaKeyAction = 0;
  CurveEditorPanel m_curveEditor{};
  struct LayerDragTarget {
    EntityID e = InvalidEntity;
    uint32_t blockId = 0;
    ActionID action = 0;
    int32_t start = 0;
    int32_t end = 0;
    int32_t inFrame = 0;
    int32_t outFrame = 0;
  };
  struct DragTrackSnapshot {
    int trackIndex = -1;
    std::vector<int32_t> frames;
  };
  struct DragActionSnapshot {
    ActionID action = 0;
    int32_t start = 0;
    int32_t end = 0;
    std::vector<std::vector<int32_t>> trackFrames;
  };
  std::vector<LayerDragTarget> m_dragDurationTargets;
  std::vector<DragTrackSnapshot> m_dragDurationTrackSnapshots;
  std::vector<DragActionSnapshot> m_dragDurationActionSnapshots;

  void recomputeLastFrameFromKeys();
  void drawTransportBar();
  void drawNlaControls();
  void drawTimeline();
  void drawLayerBarPane();
  void drawMarkers(const ImRect &r, int32_t firstFrame, int32_t lastFrame);
  void drawKeysAndTracks(const struct ImRect &r, int32_t firstFrame,
                         int32_t lastFrame);
  void ensureTracksForWorld();
  void buildRowEntities();
  void buildRows();
  void applyIsolation();
  int32_t entityEndFrame(EntityID e) const;
  void setEntityEndFrame(EntityID e, int32_t endFrame);
  int32_t entityStartFrame(EntityID e) const;
  void setEntityStartFrame(EntityID e, int32_t startFrame);
  void markLayoutDirty() { m_layoutDirty = true; }
  void invalidateTrackIndexCache() { m_trackIndexCacheDirty = true; }
  uint64_t computeLayoutSignature() const;
  void rebuildLayoutCacheIfNeeded();
  void rebuildTrackIndexCache() const;
  int findTrackIndexCached(EntityID e, uint32_t blockId, AnimChannel ch) const;

  // Key editing helpers
  void clearSelection();
  bool isSelected(const SeqKeyRef &k) const;
  void selectSingle(const SeqKeyRef &k);
  void toggleSelect(const SeqKeyRef &k);
  void addSelect(const SeqKeyRef &k);

  void deleteSelectedKeys();
  void copySelectedKeys();
  void pasteKeysAtFrame(int32_t frame);

  bool hitTestKey(const struct ImRect &r, int32_t firstFrame,
                  const struct ImVec2 &mouse, SeqKeyRef &outKey) const;

  void addKeyAt(int32_t trackIndex, int32_t frame);
  void setKeyAt(int32_t trackIndex, int32_t frame, float value);
  void moveKeyFrame(const SeqKeyRef &k, int32_t newFrame);
  void setKeyValue(const SeqKeyRef &k, float value);

  int32_t clampFrame(int32_t f) const;

  uint64_t rowKey(EntityID e, SeqRowType type, SeqProperty prop) const;
  bool stopwatchEnabled(EntityID e, SeqProperty prop) const;
  void setStopwatch(EntityID e, SeqProperty prop, bool enabled);
  void propertyChannels(SeqProperty prop, AnimChannel out[3]) const;
  bool findPropertyKeys(EntityID e, SeqProperty prop,
                        std::vector<int32_t> &outFrames) const;
  bool buildPropertyFrameCache(
      EntityID e, SeqProperty prop, std::vector<int32_t> &outFrames,
      std::unordered_map<int32_t, SeqKeyRef> *outFrameToKey) const;
  bool addOrOverwritePropertyKeys(EntityID e, SeqProperty prop,
                                  int32_t frame,
                                  const float *rotationEulerDeg = nullptr);
  bool deletePropertyKeysAtFrame(EntityID e, SeqProperty prop, int32_t frame);
  void clearPropertyKeys(EntityID e, SeqProperty prop);
  bool movePropertyKeys(EntityID e, SeqProperty prop, int32_t fromFrame,
                        int32_t toFrame);
  int normalizeTrackPair(EntityID e, uint32_t blockId, AnimChannel ch);
  uint32_t resolveTargetBlock(EntityID e) const;
  int graphTrackForProperty(EntityID e, SeqProperty prop, int component) const;
  int graphTrackForPropertyBest(EntityID e, SeqProperty prop) const;
  void buildNlaFromClip();
  bool hitTestPropertyKey(const ImRect &r, int32_t firstFrame,
                          const ImVec2 &mouse, EntityID &outEntity,
                          SeqProperty &outProp, int32_t &outFrame,
                          SeqKeyRef &outKey) const;
  bool isLayerHidden(EntityID e) const;
  ImU32 layerColor(EntityID e) const;

  // Mapping
  float frameToX(int32_t frame, int32_t firstFrame, float xStart) const;
  int32_t xToFrame(float x, int32_t firstFrame, float xStart) const;
};

} // namespace Nyx
