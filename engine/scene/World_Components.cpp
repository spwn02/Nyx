#include "World.h"

namespace Nyx {

bool World::hasMesh(EntityID e) const { return m_mesh.find(e) != m_mesh.end(); }

CMesh &World::ensureMesh(EntityID e) {
  auto it = m_mesh.find(e);
  if (it != m_mesh.end())
    return it->second;

  CMesh mc{};
  mc.submeshes.push_back(MeshSubmesh{});
  m_mesh.emplace(e, mc);

  m_events.push({WorldEventType::MeshChanged, e});
  return m_mesh.at(e);
}

CMesh &World::mesh(EntityID e) { return m_mesh.at(e); }
const CMesh &World::mesh(EntityID e) const { return m_mesh.at(e); }

void World::removeMesh(EntityID e) {
  auto it = m_mesh.find(e);
  if (it == m_mesh.end())
    return;
  m_mesh.erase(it);
  m_events.push({WorldEventType::MeshChanged, e});
}

uint32_t World::submeshCount(EntityID e) const {
  auto it = m_mesh.find(e);
  if (it == m_mesh.end())
    return 0;
  return (uint32_t)it->second.submeshes.size();
}

MeshSubmesh &World::submesh(EntityID e, uint32_t si) {
  auto &mc = ensureMesh(e);
  if (mc.submeshes.empty())
    mc.submeshes.push_back(MeshSubmesh{});
  if (si >= (uint32_t)mc.submeshes.size())
    mc.submeshes.resize((size_t)si + 1, MeshSubmesh{});
  return mc.submeshes[(size_t)si];
}

bool World::hasRenderableAsset(EntityID e) const {
  return m_renderableAsset.find(e) != m_renderableAsset.end();
}

CRenderableAsset &World::ensureRenderableAsset(EntityID e) {
  auto it = m_renderableAsset.find(e);
  if (it != m_renderableAsset.end())
    return it->second;
  m_renderableAsset.emplace(e, CRenderableAsset{});
  return m_renderableAsset.at(e);
}

CRenderableAsset &World::renderableAsset(EntityID e) {
  return m_renderableAsset.at(e);
}

const CRenderableAsset &World::renderableAsset(EntityID e) const {
  return m_renderableAsset.at(e);
}

void World::removeRenderableAsset(EntityID e) { m_renderableAsset.erase(e); }

bool World::hasCamera(EntityID e) const { return m_cam.find(e) != m_cam.end(); }

CCamera &World::ensureCamera(EntityID e) {
  auto it = m_cam.find(e);
  if (it != m_cam.end())
    return it->second;

  m_cam[e] = CCamera{};
  m_camMat[e] = CCameraMatrices{};
  m_events.push({WorldEventType::CameraCreated, e});

  if (m_activeCamera == InvalidEntity)
    setActiveCamera(e);
  return m_cam.at(e);
}

CCamera &World::camera(EntityID e) { return m_cam.at(e); }
const CCamera &World::camera(EntityID e) const { return m_cam.at(e); }

CCameraMatrices &World::cameraMatrices(EntityID e) { return m_camMat.at(e); }
const CCameraMatrices &World::cameraMatrices(EntityID e) const {
  return m_camMat.at(e);
}

void World::removeCamera(EntityID e) {
  auto it = m_cam.find(e);
  if (it == m_cam.end())
    return;
  m_cam.erase(it);
  m_camMat.erase(e);
  m_events.push({WorldEventType::CameraDestroyed, e});
  if (m_activeCamera == e) {
    EntityID old = m_activeCamera;
    m_activeCamera = InvalidEntity;
    m_events.push({WorldEventType::ActiveCameraChanged, InvalidEntity, old});
  }
}

bool World::hasLight(EntityID e) const {
  return m_light.find(e) != m_light.end();
}

CLight &World::ensureLight(EntityID e) {
  auto it = m_light.find(e);
  if (it != m_light.end())
    return it->second;

  m_light[e] = CLight{};
  if (!hasMesh(e)) {
    auto &mc = ensureMesh(e);
    if (mc.submeshes.empty())
      mc.submeshes.push_back(MeshSubmesh{});
    mc.submeshes[0].name = "Light";
    mc.submeshes[0].type = ProcMeshType::Sphere;
  }
  return m_light.at(e);
}

CLight &World::light(EntityID e) { return m_light.at(e); }
const CLight &World::light(EntityID e) const { return m_light.at(e); }

void World::removeLight(EntityID e) {
  auto it = m_light.find(e);
  if (it == m_light.end())
    return;
  m_light.erase(it);
  m_events.push({WorldEventType::LightChanged, e});
}

bool World::hasSky(EntityID e) const { return m_sky.find(e) != m_sky.end(); }

CSky &World::ensureSky(EntityID e) {
  auto it = m_sky.find(e);
  if (it != m_sky.end())
    return it->second;

  m_sky[e] = CSky{};
  return m_sky.at(e);
}

CSky &World::sky(EntityID e) { return m_sky.at(e); }
const CSky &World::sky(EntityID e) const { return m_sky.at(e); }

CSky &World::skySettings() { return m_skySettings; }
const CSky &World::skySettings() const { return m_skySettings; }

void World::setActiveCamera(EntityID cam) {
  if (cam != InvalidEntity && !isAlive(cam))
    return;
  if (cam != InvalidEntity && !hasCamera(cam))
    return;
  if (cam != InvalidEntity) {
    const auto &tr = transform(cam);
    if (tr.hidden || tr.hiddenEditor || tr.disabledAnim)
      return;
  }

  if (m_activeCamera == cam)
    return;

  const EntityID old = m_activeCamera;
  m_activeCamera = cam;

  if (m_activeCamera != InvalidEntity) {
    auto it = m_cam.find(m_activeCamera);
    if (it != m_cam.end())
      it->second.dirty = true;
    auto mit = m_camMat.find(m_activeCamera);
    if (mit != m_camMat.end())
      mit->second.dirty = true;
  }

  m_events.push({WorldEventType::ActiveCameraChanged, cam, old});
}

} // namespace Nyx
