#pragma once

#include "editor/Selection.h"
#include "animation/AnimNLA.h"
#include "scene/World.h"
#include "scene/WorldEvents.h"
#include "render/material/MaterialSystem.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace Nyx {

class World;
class AnimationSystem;

struct PersistedAnimTrackHist {
  EntityUUID entity = {};
  uint32_t blockId = 0;
  AnimChannel channel{};
  AnimCurve curve{};
};

struct PersistedAnimRangeHist {
  EntityUUID entity = {};
  uint32_t blockId = 0;
  AnimFrame start = 0;
  AnimFrame end = 0;
};

struct PersistedActionTrackHist {
  AnimChannel channel{};
  AnimCurve curve{};
};

struct PersistedActionHist {
  std::string name;
  AnimFrame start = 0;
  AnimFrame end = 0;
  std::vector<PersistedActionTrackHist> tracks;
};

struct PersistedNlaStripHist {
  ActionID action = 0;
  EntityUUID target = {};
  AnimFrame start = 0;
  AnimFrame end = 0;
  AnimFrame inFrame = 0;
  AnimFrame outFrame = 0;
  float timeScale = 1.0f;
  bool reverse = false;
  NlaBlendMode blend = NlaBlendMode::Replace;
  float influence = 1.0f;
  AnimFrame fadeIn = 0;
  AnimFrame fadeOut = 0;
  int32_t layer = 0;
  bool muted = false;
};

struct PersistedAnimationStateHist {
  bool valid = false;
  std::string name;
  AnimFrame lastFrame = 0;
  bool loop = true;
  uint32_t nextBlockId = 1;
  std::vector<PersistedAnimTrackHist> tracks;
  std::vector<PersistedAnimRangeHist> ranges;
  std::vector<PersistedActionHist> actions;
  std::vector<PersistedNlaStripHist> strips;
  AnimFrame frame = 0;
  bool playing = false;
  float fps = 30.0f;
};

using MaterialSystemSnapshot = MaterialSystem::MaterialSystemSnapshot;

struct HistorySelectionSnapshot {
  SelectionKind kind = SelectionKind::None;
  std::vector<std::pair<EntityUUID, uint32_t>> picks; // uuid + submesh
  std::pair<EntityUUID, uint32_t> activePick{};
  EntityUUID activeEntity{};
  MaterialHandle activeMaterial = InvalidMaterial;
};

struct EntitySnapshot {
  EntityUUID uuid{};
  EntityUUID parent{};
  CName name{};
  CTransform transform{};
  bool hasMesh = false;
  CMesh mesh{};
  bool hasCamera = false;
  CCamera camera{};
  CCameraMatrices cameraMatrices{};
  bool hasLight = false;
  CLight light{};
  bool hasSky = false;
  CSky sky{};
  std::vector<uint32_t> categories;
};

struct CategorySnapshot {
  std::vector<World::Category> categories;
  std::unordered_map<uint64_t, std::vector<uint32_t>> entityCategoriesByUUID;
};

struct OpEntityCreate {
  EntitySnapshot snap;
};
struct OpEntityDestroy {
  EntitySnapshot snap;
};
struct OpTransform {
  EntityUUID uuid{};
  CTransform before{};
  CTransform after{};
};
struct OpName {
  EntityUUID uuid{};
  std::string before;
  std::string after;
};
struct OpParent {
  EntityUUID uuid{};
  EntityUUID before{};
  EntityUUID after{};
};
struct OpMesh {
  EntityUUID uuid{};
  bool beforeHasMesh = false;
  bool afterHasMesh = false;
  CMesh before{};
  CMesh after{};
};
struct OpLight {
  EntityUUID uuid{};
  bool beforeHasLight = false;
  bool afterHasLight = false;
  CLight before{};
  CLight after{};
};
struct OpCamera {
  EntityUUID uuid{};
  bool beforeHasCamera = false;
  bool afterHasCamera = false;
  CCamera before{};
  CCamera after{};
  CCameraMatrices beforeMat{};
  CCameraMatrices afterMat{};
};
struct OpSky {
  CSky before{};
  CSky after{};
};
struct OpActiveCamera {
  EntityUUID before{};
  EntityUUID after{};
};
struct OpCategories {
  CategorySnapshot before{};
  CategorySnapshot after{};
};
struct OpMaterials {
  MaterialSystemSnapshot before{};
  MaterialSystemSnapshot after{};
};
struct OpAnimation {
  PersistedAnimationStateHist before{};
  PersistedAnimationStateHist after{};
};

using HistoryOp = std::variant<OpEntityCreate, OpEntityDestroy, OpTransform,
                               OpName, OpParent, OpMesh, OpLight, OpCamera,
                               OpSky, OpActiveCamera, OpCategories, OpMaterials,
                               OpAnimation>;

struct HistoryEntry {
  uint64_t id = 0;
  std::string label;
  double timestampSec = 0.0;
  std::vector<HistoryOp> ops;
  HistorySelectionSnapshot selection;
};

class EditorHistory final {
public:
  void setWorld(World *world, MaterialSystem *materials);
  void setAnimationContext(AnimationSystem *anim, AnimationClip *clip);
  void setRecording(bool on) { m_recording = on; }
  bool recording() const { return m_recording; }
  bool isApplying() const { return m_applying; }
  void setAbsorbMaterialOnlyChanges(bool on) { m_absorbMaterialOnlyChanges = on; }
  void setMaxEntries(size_t maxEntries);
  size_t maxEntries() const { return m_maxEntries; }

  bool saveToFile(const std::string &path) const;
  bool loadFromFile(const std::string &path);
  void beginTransformBatch(const std::string &label, const World &world,
                           const Selection &sel);
  void endTransformBatch(const World &world, const Selection &sel);
  bool transformBatchActive() const { return m_transformBatchActive; }

  void processEvents(const World &world, const WorldEvents &ev,
                     MaterialSystem &materials, const Selection &sel);

  bool canUndo() const { return m_cursor >= 0; }
  bool canRedo() const { return m_cursor + 1 < (int)m_entries.size(); }

  bool undo(World &world, MaterialSystem &materials, Selection &sel);
  bool redo(World &world, MaterialSystem &materials, Selection &sel);
  void clear();

  const std::vector<HistoryEntry> &entries() const { return m_entries; }
  int cursor() const { return m_cursor; }
  uint64_t revision() const { return m_revision; }

private:
  struct EntityState {
    EntityUUID uuid{};
    EntityUUID parent{};
    CName name{};
    CTransform transform{};
    bool hasMesh = false;
    CMesh mesh{};
    bool hasCamera = false;
    CCamera camera{};
    CCameraMatrices cameraMatrices{};
    bool hasLight = false;
    CLight light{};
    bool hasSky = false;
    CSky sky{};
    std::vector<uint32_t> categories;
  };

  void rebuildCache(const World &world);
  EntityState buildState(const World &world, EntityID e) const;

  HistorySelectionSnapshot captureSelection(const World &world,
                                            const Selection &sel) const;
  void restoreSelection(const World &world, Selection &sel,
                        const HistorySelectionSnapshot &snap) const;

  CategorySnapshot captureCategories(const World &world) const;
  void applyCategories(World &world, const CategorySnapshot &snap) const;

  EntityID restoreEntity(World &world, const EntitySnapshot &snap) const;

  std::string labelForEvents(const WorldEvents &ev,
                             bool categoriesChanged,
                             bool materialsChanged) const;
  std::string labelForEntry(const HistoryEntry &entry,
                            const World &world) const;
  std::string labelForAnimationOp(const OpAnimation &op) const;
  PersistedAnimationStateHist captureAnimationState(const World &world) const;
  void applyAnimationState(const PersistedAnimationStateHist &st, World &world);
  bool animationStateEqual(const PersistedAnimationStateHist &a,
                           const PersistedAnimationStateHist &b) const;

private:
  World *m_world = nullptr;
  MaterialSystem *m_materials = nullptr;
  AnimationSystem *m_anim = nullptr;
  AnimationClip *m_animClip = nullptr;
  bool m_recording = true;
  bool m_applying = false;

  std::vector<HistoryEntry> m_entries;
  int m_cursor = -1;
  uint64_t m_nextId = 1;
  size_t m_maxEntries = 200;

  std::unordered_map<EntityID, EntityState, EntityHash> m_cacheById;
  CategorySnapshot m_lastCategories{};
  uint64_t m_lastMaterialSerial = 0;
  MaterialSystemSnapshot m_lastMaterials{};
  CSky m_lastSky{};
  PersistedAnimationStateHist m_lastAnimation{};

  bool m_loadedFromDisk = false;
  uint64_t m_revision = 0;
  bool m_absorbMaterialOnlyChanges = false;
  bool m_transformBatchActive = false;
  std::string m_transformBatchLabel = "Transform";
  std::unordered_map<EntityUUID, CTransform, EntityUUIDHash>
      m_transformBatchBefore;
  std::unordered_map<EntityUUID, CTransform, EntityUUIDHash>
      m_transformBatchAfter;
  HistorySelectionSnapshot m_transformBatchSelection{};
};

} // namespace Nyx
