#pragma once

#include "editor/Selection.h"
#include "editor/tools/IconAtlas.h"
#include "material/MaterialHandle.h"
#include "scene/EntityID.h"
#include "scene/World.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <array>
#include <string>
#include <unordered_map>
#include <vector>

namespace Nyx {

class EngineContext;

class HierarchyPanel {
public:
  void setWorld(World *world);
  void onWorldEvent(World &world, const WorldEvent &e);
  void draw(World &world, EntityID editorCamera, EngineContext &engine,
            Selection &sel);

private:
  void drawEntityNode(World &world, EntityID e, EngineContext &engine,
                      Selection &sel);
  void rebuildRoots(World &world);
  void addRoot(EntityID e);
  void removeRoot(EntityID e);

private:
  struct MatThumb {
    uint32_t tex = 0;
    bool ready = false;
    bool pending = false;
    bool saved = false;
    std::string cachePath;
  };

  MatThumb &getMaterialThumb(EngineContext &engine, MaterialHandle h);
  void copyTransform(World &world, EntityID e);
  void pasteTransform(World &world, EntityID e);

private:
  std::vector<EntityID> m_roots;
  std::vector<EntityID> m_visibleOrder;
  IconAtlas m_iconAtlas{};
  bool m_iconInit = false;
  bool m_iconReady = false;
  EntityID m_editorCamera = InvalidEntity;
  MaterialHandle m_matClipboard = InvalidMaterial;
  std::unordered_map<uint64_t, MatThumb> m_matThumbs;
  uint64_t m_matThumbSettingsHash = 0;
  EntityID m_renameEntity = InvalidEntity;
  std::array<char, 128> m_renameEntityBuf{};
  EntityID m_copyEntity = InvalidEntity;
  bool m_hasCopiedTransform = false;
  glm::vec3 m_copyTranslation{0.0f};
  glm::quat m_copyRotation{1.0f, 0.0f, 0.0f, 0.0f};
  glm::vec3 m_copyScale{1.0f};
};

} // namespace Nyx
