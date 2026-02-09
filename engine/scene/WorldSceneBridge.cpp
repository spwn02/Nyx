#include "scene/WorldSceneBridge.h"

#include "scene/World.h"

#include <algorithm>
#include <vector>

namespace Nyx {

static SceneEntityID toSceneID(const World &w, EntityID e) {
  const EntityUUID u = w.uuid(e);
  if (u)
    return (SceneEntityID)u.value;
  return (SceneEntityID(e.index) << 32u) | SceneEntityID(e.generation);
}

static EntityID fromSceneID(
    SceneEntityID sid,
    const std::unordered_map<SceneEntityID, EntityID> &map) {
  auto it = map.find(sid);
  return (it == map.end()) ? InvalidEntity : it->second;
}

static SceneTransform packTransform(const CTransform &tr) {
  SceneTransform out{};
  out.tx = tr.translation.x;
  out.ty = tr.translation.y;
  out.tz = tr.translation.z;
  out.rx = tr.rotation.x;
  out.ry = tr.rotation.y;
  out.rz = tr.rotation.z;
  out.rw = tr.rotation.w;
  out.sx = tr.scale.x;
  out.sy = tr.scale.y;
  out.sz = tr.scale.z;
  return out;
}

static void unpackTransform(CTransform &tr, const SceneTransform &s) {
  tr.translation = {s.tx, s.ty, s.tz};
  tr.rotation = glm::quat(s.rw, s.rx, s.ry, s.rz);
  tr.scale = {s.sx, s.sy, s.sz};
  tr.dirty = true;
}

static SceneLightType toSceneLightType(LightType t) {
  switch (t) {
  case LightType::Directional:
    return SceneLightType::Directional;
  case LightType::Point:
    return SceneLightType::Point;
  case LightType::Spot:
    return SceneLightType::Spot;
  }
  return SceneLightType::Point;
}

static LightType fromSceneLightType(SceneLightType t) {
  switch (t) {
  case SceneLightType::Directional:
    return LightType::Directional;
  case SceneLightType::Point:
    return LightType::Point;
  case SceneLightType::Spot:
    return LightType::Spot;
  }
  return LightType::Point;
}

WorldToSceneResult WorldSceneBridge::exportWorld(const World &w,
                                                 const std::string &sceneName) {
  WorldToSceneResult res{};
  NyxScene &out = res.scene;

  out.header.versionMajor = 1;
  out.header.versionMinor = 1;
  out.name = sceneName;

  const CSky &sky = w.skySettings();
  out.skyAsset = sky.hdriPath;
  out.exposure = sky.exposure;

  std::vector<EntityID> ents = w.alive();
  std::sort(ents.begin(), ents.end());

  out.entities.reserve(ents.size());

  for (EntityID e : ents) {
    if (!w.isAlive(e))
      continue;

    SceneEntity se{};
    se.id = toSceneID(w, e);
    se.name = w.name(e).name;

    se.transform = packTransform(w.transform(e));

    const EntityID p = w.parentOf(e);
    se.hierarchy.parent = (p == InvalidEntity) ? 0 : toSceneID(w, p);

    if (w.hasCamera(e)) {
      const CCamera &c = w.camera(e);
      se.hasCamera = true;
      se.camera.fovY = c.fovYDeg;
      se.camera.nearZ = c.nearZ;
      se.camera.farZ = c.farZ;
      se.camera.aperture = c.aperture;
      se.camera.focusDistance = c.focusDistance;
      se.camera.sensorWidth = c.sensorWidth;
      se.camera.sensorHeight = c.sensorHeight;
      se.camera.active = (w.activeCamera() == e);
    }

    if (w.hasLight(e)) {
      const CLight &L = w.light(e);
      se.hasLight = true;
      se.light.type = toSceneLightType(L.type);
      se.light.color[0] = L.color.r;
      se.light.color[1] = L.color.g;
      se.light.color[2] = L.color.b;
      se.light.intensity = L.intensity;
      se.light.range = L.radius;
      se.light.spotAngle = L.outerAngle;
    }

    if (w.hasRenderableAsset(e)) {
      const CRenderableAsset &ra = w.renderableAsset(e);
      se.hasRenderable = true;
      se.renderable.meshAsset = ra.meshAsset;
      se.renderable.materialAsset = ra.materialAsset;
    } else if (w.hasMesh(e) && w.submeshCount(e) > 0) {
      const MeshSubmesh &sm = w.mesh(e).submeshes[0];
      if (!sm.materialAssetPath.empty()) {
        se.hasRenderable = true;
        se.renderable.materialAsset = sm.materialAssetPath;
      }
    }

    out.entities.push_back(std::move(se));
    res.worldToScene[e] = se.id;
  }

  return res;
}

SceneToWorldResult WorldSceneBridge::importScene(World &w, const NyxScene &scene,
                                                 bool clearWorldFirst) {
  SceneToWorldResult res{};

  if (clearWorldFirst)
    w.clear();

  if (!scene.skyAsset.empty()) {
    w.skySettings().hdriPath = scene.skyAsset;
  }
  w.skySettings().exposure = scene.exposure;

  res.sceneToWorld.reserve(scene.entities.size());

  std::vector<const SceneEntity *> sorted;
  sorted.reserve(scene.entities.size());
  for (const auto &e : scene.entities)
    sorted.push_back(&e);
  std::sort(sorted.begin(), sorted.end(),
            [](const SceneEntity *a, const SceneEntity *b) {
              return a->id < b->id;
            });

  EntityID pendingActiveCamera = InvalidEntity;

  for (const SceneEntity *se : sorted) {
    EntityID e = InvalidEntity;
    if (se->id != 0) {
      e = w.createEntityWithUUID(EntityUUID{(uint64_t)se->id}, se->name);
    }
    if (e == InvalidEntity)
      e = w.createEntity(se->name);

    res.sceneToWorld[se->id] = e;

    w.name(e).name = se->name;
    unpackTransform(w.transform(e), se->transform);
    w.worldTransform(e).dirty = true;

    if (se->hasCamera) {
      CCamera &c = w.ensureCamera(e);
      c.fovYDeg = se->camera.fovY;
      c.nearZ = se->camera.nearZ;
      c.farZ = se->camera.farZ;
      c.aperture = se->camera.aperture;
      c.focusDistance = se->camera.focusDistance;
      c.sensorWidth = se->camera.sensorWidth;
      c.sensorHeight = se->camera.sensorHeight;
      c.dirty = true;
      if (se->camera.active)
        pendingActiveCamera = e;
    }

    if (se->hasLight) {
      CLight &L = w.ensureLight(e);
      L.type = fromSceneLightType(se->light.type);
      L.color = {se->light.color[0], se->light.color[1], se->light.color[2]};
      L.intensity = se->light.intensity;
      L.radius = se->light.range;
      L.outerAngle = se->light.spotAngle;
    }

    if (se->hasRenderable) {
      CRenderableAsset &ra = w.ensureRenderableAsset(e);
      ra.meshAsset = se->renderable.meshAsset;
      ra.materialAsset = se->renderable.materialAsset;

      if (!ra.materialAsset.empty()) {
        CMesh &m = w.ensureMesh(e);
        if (m.submeshes.empty())
          m.submeshes.push_back(MeshSubmesh{});
        m.submeshes[0].materialAssetPath = ra.materialAsset;
      }
    }
  }

  for (const SceneEntity *se : sorted) {
    EntityID child = fromSceneID(se->id, res.sceneToWorld);
    if (child == InvalidEntity)
      continue;

    EntityID parent = InvalidEntity;
    if (se->hierarchy.parent != 0) {
      parent = fromSceneID(se->hierarchy.parent, res.sceneToWorld);
      if (parent == InvalidEntity)
        parent = InvalidEntity;
    }

    w.setParent(child, parent);
    w.worldTransform(child).dirty = true;
  }

  w.updateTransforms();

  if (pendingActiveCamera != InvalidEntity)
    w.setActiveCamera(pendingActiveCamera);

  return res;
}

} // namespace Nyx
