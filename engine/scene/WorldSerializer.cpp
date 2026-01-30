#include "WorldSerializer.h"

#include "scene/JsonLite.h"
#include "scene/World.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace Nyx {

using namespace JsonLite;

static Value jVec3(float x, float y, float z) {
  Array a;
  a.emplace_back((double)x);
  a.emplace_back((double)y);
  a.emplace_back((double)z);
  return Value(std::move(a));
}
static Value jQuatWXYZ(float w, float x, float y, float z) {
  Array a;
  a.emplace_back((double)w);
  a.emplace_back((double)x);
  a.emplace_back((double)y);
  a.emplace_back((double)z);
  return Value(std::move(a));
}

static std::string u64ToString(uint64_t v) { return std::to_string(v); }
static float radToDeg(float r) { return r * 57.2957795f; }
static float degToRad(float d) { return d * 0.0174532925f; }

static uint64_t readU64(const Value *v) {
  if (!v)
    return 0;
  if (v->isString()) {
    const std::string &s = v->asString();
    if (s.empty())
      return 0;
    return (uint64_t)std::stoull(s);
  }
  if (v->isNum())
    return (uint64_t)v->asNum(0.0);
  return 0;
}
static bool readVec3(const Value &v, float &x, float &y, float &z) {
  if (!v.isArray())
    return false;
  const auto &a = v.asArray();
  if (a.size() != 3)
    return false;
  x = (float)a[0].asNum();
  y = (float)a[1].asNum();
  z = (float)a[2].asNum();
  return true;
}
static bool readQuatWXYZ(const Value &v, float &w, float &x, float &y,
                         float &z) {
  if (!v.isArray())
    return false;
  const auto &a = v.asArray();
  if (a.size() != 4)
    return false;
  w = (float)a[0].asNum();
  x = (float)a[1].asNum();
  y = (float)a[2].asNum();
  z = (float)a[3].asNum();
  return true;
}

static std::string readAllText(const std::string &path) {
  std::ifstream f(path, std::ios::in | std::ios::binary);
  if (!f.is_open())
    return {};
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

static bool writeAllText(const std::string &path, const std::string &text) {
  std::filesystem::path p(path);
  if (p.has_parent_path()) {
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);
  }

  std::ofstream f(p, std::ios::out | std::ios::binary | std::ios::trunc);
  if (!f.is_open())
    return false;
  f.write(text.data(), (std::streamsize)text.size());
  return true;
}

bool WorldSerializer::saveToFile(const World &world, const std::string &path) {
  struct ERec {
    EntityUUID uuid;
    EntityID e;
  };
  std::vector<ERec> ents;
  ents.reserve(world.alive().size());

  for (EntityID e : world.alive()) {
    if (!world.isAlive(e))
      continue;
    const EntityUUID id = world.uuid(e);
    if (!id)
      continue;
    ents.push_back({id, e});
  }

  std::sort(ents.begin(), ents.end(), [](const ERec &a, const ERec &b) {
    return a.uuid.value < b.uuid.value;
  });

  Object root;
  root["version"] = 2;
  root["type"] = "NyxScene";
  root["activeCamera"] = u64ToString(world.activeCameraUUID().value);

  Array jEntities;
  jEntities.reserve(ents.size());

  for (const auto &rec : ents) {
    const EntityID e = rec.e;

    const bool isEditorCamera =
        (world.name(e).name == "Editor Camera") && world.hasCamera(e) &&
        !world.hasMesh(e);
    if (isEditorCamera)
      continue;

    Object je;
    je["uuid"] = u64ToString(rec.uuid.value);
    je["name"] = world.name(e).name;

    EntityID p = world.parentOf(e);
    if (p != InvalidEntity && world.isAlive(p)) {
      EntityUUID pu = world.uuid(p);
      je["parent"] = pu ? Value(u64ToString(pu.value)) : Value(nullptr);
    } else {
      je["parent"] = Value(nullptr);
    }

    const auto &tr = world.transform(e);
    Object jt;
    jt["t"] = jVec3(tr.translation.x, tr.translation.y, tr.translation.z);
    jt["r"] =
        jQuatWXYZ(tr.rotation.w, tr.rotation.x, tr.rotation.y, tr.rotation.z);
    jt["s"] = jVec3(tr.scale.x, tr.scale.y, tr.scale.z);
    je["transform"] = Value(std::move(jt));

    if (world.hasMesh(e)) {
      const auto &mc = world.mesh(e);
      Object jm;
      Array subs;
      subs.reserve(mc.submeshes.size());

      for (const auto &sm : mc.submeshes) {
        Object js;
        js["name"] = sm.name;
        js["type"] = (double)(int)sm.type;

        Array mh;
        mh.emplace_back((double)sm.material.slot);
        mh.emplace_back((double)sm.material.gen);
        js["material"] = Value(std::move(mh));

        subs.emplace_back(Value(std::move(js)));
      }

      jm["submeshes"] = Value(std::move(subs));
      je["mesh"] = Value(std::move(jm));
    }

    if (world.hasCamera(e)) {
      const auto &cam = world.camera(e);
      Object jc;
      jc["projection"] = (double)(int)cam.projection;
      jc["fovYDeg"] = (double)cam.fovYDeg;
      jc["orthoHeight"] = (double)cam.orthoHeight;
      jc["nearZ"] = (double)cam.nearZ;
      jc["farZ"] = (double)cam.farZ;
      jc["exposure"] = (double)cam.exposure;
      je["camera"] = Value(std::move(jc));
    }

    if (world.hasLight(e)) {
      const auto &L = world.light(e);
      Object jl;
      jl["type"] = (double)(int)L.type;
      jl["color"] = jVec3(L.color.x, L.color.y, L.color.z);
      jl["intensity"] = (double)L.intensity;
      jl["radius"] = (double)L.radius;
      jl["innerDeg"] = (double)radToDeg(L.innerAngle);
      jl["outerDeg"] = (double)radToDeg(L.outerAngle);
      jl["exposure"] = (double)L.exposure;
      jl["enabled"] = L.enabled;
      je["light"] = Value(std::move(jl));
    }

    jEntities.emplace_back(Value(std::move(je)));
  }

  root["entities"] = Value(std::move(jEntities));

  const std::string out =
      stringify(Value(std::move(root)), /*pretty=*/true, /*indent=*/2);
  return writeAllText(path, out);
}

bool WorldSerializer::loadFromFile(World &world, const std::string &path) {
  const std::string text = readAllText(path);
  if (text.empty())
    return false;

  Value root;
  ParseError err;
  if (!parse(text, root, err))
    return false;
  if (!root.isObject())
    return false;

  const Value *vType = root.get("type");
  if (!vType || !vType->isString() || vType->asString() != "NyxScene")
    return false;

  int version = 1;
  if (const Value *vVer = root.get("version"); vVer && vVer->isNum())
    version = (int)vVer->asNum(1.0);

  const Value *vEnts = root.get("entities");
  if (!vEnts || !vEnts->isArray())
    return false;

  world.clear();

  std::unordered_map<uint64_t, EntityID> map;
  map.reserve(vEnts->asArray().size());

  for (const Value &ve : vEnts->asArray()) {
    if (!ve.isObject())
      return false;

    const Value *vu = ve.get("uuid");
    const Value *vn = ve.get("name");
    if (!vu || !vn || !vn->isString())
      return false;

    const uint64_t uuid = readU64(vu);
    if (uuid == 0)
      return false;
    const std::string name = vn->asString();

    EntityID e = world.createEntityWithUUID(EntityUUID{uuid}, name);
    if (e == InvalidEntity)
      return false;
    map[uuid] = e;
  }

  for (const Value &ve : vEnts->asArray()) {
    const uint64_t uuid = readU64(ve.get("uuid"));
    EntityID e = map[uuid];
    if (e == InvalidEntity)
      return false;

    if (const Value *vp = ve.get("parent")) {
      const uint64_t pu = readU64(vp);
      if (pu != 0) {
        auto it = map.find(pu);
        if (it != map.end())
          world.setParent(e, it->second);
      }
    }

    if (const Value *vt = ve.get("transform")) {
      if (vt->isObject()) {
        float tx = 0, ty = 0, tz = 0, sx = 1, sy = 1, sz = 1;
        float qw = 1, qx = 0, qy = 0, qz = 0;
        const Value *jt = vt->get("t");
        const Value *jr = vt->get("r");
        const Value *js = vt->get("s");
        if (jt)
          readVec3(*jt, tx, ty, tz);
        if (jr)
          readQuatWXYZ(*jr, qw, qx, qy, qz);
        if (js)
          readVec3(*js, sx, sy, sz);

        auto &tr = world.transform(e);
        tr.translation = {tx, ty, tz};
        tr.rotation = {qw, qx, qy, qz};
        tr.scale = {sx, sy, sz};
        tr.dirty = true;
      }
    }

    if (const Value *vm = ve.get("mesh")) {
      if (vm->isObject()) {
        const Value *vsubs = vm->get("submeshes");
        if (vsubs && vsubs->isArray()) {
          auto &mc = world.ensureMesh(e);
          mc.submeshes.clear();

          for (const Value &vs : vsubs->asArray()) {
            if (!vs.isObject())
              continue;
            MeshSubmesh sm;

            if (const Value *sn = vs.get("name"); sn && sn->isString())
              sm.name = sn->asString();
            if (const Value *st = vs.get("type"); st && st->isNum())
              sm.type = (ProcMeshType)(int)st->asNum();
            if (const Value *smat = vs.get("material");
                smat && smat->isArray()) {
              const auto &mh = smat->asArray();
              if (mh.size() >= 2) {
                sm.material.slot = (uint32_t)mh[0].asNum();
                sm.material.gen = (uint32_t)mh[1].asNum();
              }
            }

            mc.submeshes.push_back(std::move(sm));
          }
        }
      }
    }

    if (version >= 2) {
      if (const Value *vc = ve.get("camera")) {
        if (vc->isObject()) {
          auto &cam = world.ensureCamera(e);
          if (const Value *vproj = vc->get("projection"); vproj && vproj->isNum())
            cam.projection = (CameraProjection)(int)vproj->asNum(0.0);
          if (const Value *vfov = vc->get("fovYDeg"); vfov && vfov->isNum())
            cam.fovYDeg = (float)vfov->asNum(60.0);
          if (const Value *vo = vc->get("orthoHeight"); vo && vo->isNum())
            cam.orthoHeight = (float)vo->asNum(10.0);
          if (const Value *vn = vc->get("nearZ"); vn && vn->isNum())
            cam.nearZ = (float)vn->asNum(0.01);
          if (const Value *vf = vc->get("farZ"); vf && vf->isNum())
            cam.farZ = (float)vf->asNum(2000.0);
          if (const Value *ve = vc->get("exposure"); ve && ve->isNum())
            cam.exposure = (float)ve->asNum(0.0);
          cam.dirty = true;
        }
      }
    }

    if (const Value *vl = ve.get("light")) {
      if (vl->isObject()) {
        auto &L = world.ensureLight(e);
        if (const Value *vt = vl->get("type"); vt && vt->isNum())
          L.type = (LightType)(int)vt->asNum(0.0);
        if (const Value *vc = vl->get("color"))
          readVec3(*vc, L.color.x, L.color.y, L.color.z);
        if (const Value *vi = vl->get("intensity"); vi && vi->isNum())
          L.intensity = (float)vi->asNum(L.intensity);
        if (const Value *vr = vl->get("radius"); vr && vr->isNum())
          L.radius = (float)vr->asNum(L.radius);
        if (const Value *vi = vl->get("innerDeg"); vi && vi->isNum())
          L.innerAngle = degToRad((float)vi->asNum(radToDeg(L.innerAngle)));
        if (const Value *vo = vl->get("outerDeg"); vo && vo->isNum())
          L.outerAngle = degToRad((float)vo->asNum(radToDeg(L.outerAngle)));
        if (const Value *ve = vl->get("exposure"); ve && ve->isNum())
          L.exposure = (float)ve->asNum(L.exposure);
        if (const Value *ve = vl->get("enabled"); ve && ve->isBool())
          L.enabled = ve->asBool(true);
      }
    }

    if (world.hasCamera(e) && !world.hasMesh(e)) {
      auto &mc = world.ensureMesh(e);
      if (mc.submeshes.empty())
        mc.submeshes.push_back(MeshSubmesh{});
      mc.submeshes[0].name = "Camera Body";
      mc.submeshes[0].type = ProcMeshType::Cube;
    }

    if (world.hasLight(e) && !world.hasMesh(e)) {
      auto &mc = world.ensureMesh(e);
      if (mc.submeshes.empty())
        mc.submeshes.push_back(MeshSubmesh{});
      mc.submeshes[0].name = "Light";
      mc.submeshes[0].type = ProcMeshType::Sphere;
    }
  }

  if (version >= 2) {
    if (const Value *vac = root.get("activeCamera")) {
      const uint64_t uuid = readU64(vac);
      world.setActiveCameraUUID(EntityUUID{uuid});
    }
  }

  world.updateTransforms();
  world.clearEvents();
  return true;
}

} // namespace Nyx
