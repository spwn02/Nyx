#include "WorldSerializer.h"

#include "scene/EntityID.h"
#include "scene/JsonLite.h"
#include "scene/World.h"
#include "render/material/MaterialSystem.h"
#include "scene/material/MaterialData.h"

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
static Value jVec2(float x, float y) {
  Array a;
  a.emplace_back((double)x);
  a.emplace_back((double)y);
  return Value(std::move(a));
}
static Value jVec4(float x, float y, float z, float w) {
  Array a;
  a.emplace_back((double)x);
  a.emplace_back((double)y);
  a.emplace_back((double)z);
  a.emplace_back((double)w);
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
static bool readVec2(const Value &v, float &x, float &y) {
  if (!v.isArray())
    return false;
  const auto &a = v.asArray();
  if (a.size() != 2)
    return false;
  x = (float)a[0].asNum();
  y = (float)a[1].asNum();
  return true;
}
static bool readVec4(const Value &v, float &x, float &y, float &z, float &w) {
  if (!v.isArray())
    return false;
  const auto &a = v.asArray();
  if (a.size() != 4)
    return false;
  x = (float)a[0].asNum();
  y = (float)a[1].asNum();
  z = (float)a[2].asNum();
  w = (float)a[3].asNum();
  return true;
}
static bool readUVec4(const Value &v, uint32_t &x, uint32_t &y, uint32_t &z,
                      uint32_t &w) {
  if (!v.isArray())
    return false;
  const auto &a = v.asArray();
  if (a.size() != 4)
    return false;
  x = (uint32_t)a[0].asNum();
  y = (uint32_t)a[1].asNum();
  z = (uint32_t)a[2].asNum();
  w = (uint32_t)a[3].asNum();
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

static bool saveToFileImpl(const World &world, EntityID editorCamera,
                           const MaterialSystem *materials,
                           const std::string &path) {
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
  root["version"] = materials ? 5 : 3;
  root["type"] = "NyxScene";
  root["activeCamera"] = u64ToString(world.activeCameraUUID().value);

  std::vector<MaterialHandle> materialHandles;
  std::unordered_map<uint64_t, uint32_t> materialIndex;
  if (materials) {
    materialHandles.reserve(64);
  }

  {
    const auto &sky = world.skySettings();
    Object js;
    js["enabled"] = sky.enabled;
    js["drawBackground"] = sky.drawBackground;
    js["intensity"] = (double)sky.intensity;
    js["exposure"] = (double)sky.exposure;
    js["rotationYawDeg"] = (double)sky.rotationYawDeg;
    js["ambient"] = (double)sky.ambient;
    js["hdriPath"] = sky.hdriPath;
    root["sky"] = Value(std::move(js));
  }

  Array jEntities;
  jEntities.reserve(ents.size());

  for (const auto &rec : ents) {
    const EntityID e = rec.e;

    if (e == editorCamera)
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
    jt["hidden"] = tr.hidden;
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
        js["materialRef"] = sm.materialAssetPath;

        Array mh;
        mh.emplace_back((double)sm.material.slot);
        mh.emplace_back((double)sm.material.gen);
        js["material"] = Value(std::move(mh));

        if (materials && materials->isAlive(sm.material)) {
          const uint64_t key =
              (uint64_t(sm.material.slot) << 32) | uint64_t(sm.material.gen);
          auto it = materialIndex.find(key);
          if (it == materialIndex.end()) {
            const uint32_t idx = (uint32_t)materialHandles.size();
            materialHandles.push_back(sm.material);
            materialIndex[key] = idx;
            js["materialIndex"] = (double)idx;
          } else {
            js["materialIndex"] = (double)it->second;
          }
        } else {
          js["materialIndex"] = (double)-1;
        }

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

    if (world.hasSky(e))
      continue;

    jEntities.emplace_back(Value(std::move(je)));
  }

  root["entities"] = Value(std::move(jEntities));

  // Categories (editor-only grouping)
  if (!world.categories().empty()) {
    Array jCats;
    jCats.reserve(world.categories().size());
    for (const auto &c : world.categories()) {
      Object jc;
      jc["name"] = c.name;
      jc["parent"] = (double)c.parent;
      Array jEnts;
      for (EntityID e : c.entities) {
        if (!world.isAlive(e))
          continue;
        const EntityUUID id = world.uuid(e);
        if (!id)
          continue;
        jEnts.emplace_back(u64ToString(id.value));
      }
      jc["entities"] = Value(std::move(jEnts));
      jCats.emplace_back(Value(std::move(jc)));
    }
    root["categories"] = Value(std::move(jCats));
  }

  if (materials) {
    Array mats;
    mats.reserve(materialHandles.size());
    for (const auto &mh : materialHandles) {
      if (!materials->isAlive(mh)) {
        mats.emplace_back(Value(nullptr));
        continue;
      }
      const MaterialData &m = materials->cpu(mh);
      const MaterialGraph &g = materials->graph(mh);
      Object jm;
      jm["name"] = m.name;
      jm["baseColorFactor"] =
          jVec4(m.baseColorFactor.x, m.baseColorFactor.y, m.baseColorFactor.z,
                m.baseColorFactor.w);
      jm["emissiveFactor"] =
          jVec3(m.emissiveFactor.x, m.emissiveFactor.y, m.emissiveFactor.z);
      jm["metallic"] = (double)m.metallic;
      jm["roughness"] = (double)m.roughness;
      jm["ao"] = (double)m.ao;
      jm["uvScale"] = jVec2(m.uvScale.x, m.uvScale.y);
      jm["uvOffset"] = jVec2(m.uvOffset.x, m.uvOffset.y);
      Array texPaths;
      texPaths.reserve(m.texPath.size());
      for (const auto &p : m.texPath)
        texPaths.emplace_back(p);
      jm["texPath"] = Value(std::move(texPaths));

      Object jg;
      jg["version"] = 3;
      jg["alphaMode"] = (double)(int)g.alphaMode;
      jg["alphaCutoff"] = (double)g.alphaCutoff;
      jg["nextNodeId"] = (double)g.nextNodeId;
      jg["nextLinkId"] = (double)g.nextLinkId;

      Array jnodes;
      jnodes.reserve(g.nodes.size());
      for (const auto &n : g.nodes) {
        Object jn;
        jn["id"] = (double)n.id;
        jn["type"] = (double)(int)n.type;
        jn["label"] = n.label;
        jn["pos"] = jVec2(n.pos.x, n.pos.y);
        jn["posSet"] = n.posSet;
        jn["f"] = jVec4(n.f.x, n.f.y, n.f.z, n.f.w);
        Array ju;
        ju.emplace_back((double)n.u.x);
        ju.emplace_back((double)n.u.y);
        ju.emplace_back((double)n.u.z);
        ju.emplace_back((double)n.u.w);
        jn["u"] = Value(std::move(ju));
        std::string path = n.path;
        if (path.empty() && (n.type == MatNodeType::Texture2D ||
                             n.type == MatNodeType::TextureMRA ||
                             n.type == MatNodeType::NormalMap)) {
          if (materials) {
            const auto &tt = materials->textures();
            if (n.u.x != TextureTable::Invalid)
              path = tt.pathByIndex(n.u.x);
          }
        }
        jn["path"] = path;
        jnodes.emplace_back(Value(std::move(jn)));
      }
      jg["nodes"] = Value(std::move(jnodes));

      Array jlinks;
      jlinks.reserve(g.links.size());
      for (const auto &l : g.links) {
        Object jl;
        jl["id"] = (double)l.id;
        Array jfrom;
        jfrom.emplace_back((double)l.from.node);
        jfrom.emplace_back((double)l.from.slot);
        jl["from"] = Value(std::move(jfrom));
        Array jto;
        jto.emplace_back((double)l.to.node);
        jto.emplace_back((double)l.to.slot);
        jl["to"] = Value(std::move(jto));
        jlinks.emplace_back(Value(std::move(jl)));
      }
      jg["links"] = Value(std::move(jlinks));
      jm["graph"] = Value(std::move(jg));

      mats.emplace_back(Value(std::move(jm)));
    }
    root["materials"] = Value(std::move(mats));
  }

  const std::string out =
      stringify(Value(std::move(root)), /*pretty=*/true, /*indent=*/2);
  return writeAllText(path, out);
}

bool WorldSerializer::saveToFile(const World &world, EntityID editorCamera,
                                 const std::string &path) {
  return saveToFileImpl(world, editorCamera, nullptr, path);
}

bool WorldSerializer::saveToFile(const World &world, EntityID editorCamera,
                                 const MaterialSystem &materials,
                                 const std::string &path) {
  return saveToFileImpl(world, editorCamera, &materials, path);
}

static bool loadFromFileImpl(World &world, MaterialSystem *materials,
                             const std::string &path) {
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

  std::vector<MaterialHandle> loadedMaterials;
  if (materials) {
    materials->reset();
    if (const Value *vMats = root.get("materials"); vMats && vMats->isArray()) {
      const auto &arr = vMats->asArray();
      loadedMaterials.reserve(arr.size());
      for (const Value &vm : arr) {
        if (!vm.isObject()) {
          loadedMaterials.push_back(InvalidMaterial);
          continue;
        }
        MaterialData md{};
        if (const Value *vn = vm.get("name"); vn && vn->isString())
          md.name = vn->asString();
        if (const Value *vb = vm.get("baseColorFactor"); vb && vb->isArray()) {
          readVec4(*vb, md.baseColorFactor.x, md.baseColorFactor.y,
                   md.baseColorFactor.z, md.baseColorFactor.w);
        }
        if (const Value *ve = vm.get("emissiveFactor"); ve && ve->isArray()) {
          readVec3(*ve, md.emissiveFactor.x, md.emissiveFactor.y,
                   md.emissiveFactor.z);
        }
        if (const Value *vmc = vm.get("metallic"); vmc && vmc->isNum())
          md.metallic = (float)vmc->asNum(md.metallic);
        if (const Value *vr = vm.get("roughness"); vr && vr->isNum())
          md.roughness = (float)vr->asNum(md.roughness);
        if (const Value *vao = vm.get("ao"); vao && vao->isNum())
          md.ao = (float)vao->asNum(md.ao);
        if (const Value *vus = vm.get("uvScale"); vus && vus->isArray())
          readVec2(*vus, md.uvScale.x, md.uvScale.y);
        if (const Value *vuo = vm.get("uvOffset"); vuo && vuo->isArray())
          readVec2(*vuo, md.uvOffset.x, md.uvOffset.y);
        if (const Value *vt = vm.get("texPath"); vt && vt->isArray()) {
          const auto &ta = vt->asArray();
          if (ta.size() == 5 && md.texPath.size() == 6) {
            md.texPath[0] = ta[0].asString(); // base
            md.texPath[1] = ta[1].asString(); // emissive
            md.texPath[2] = ta[2].asString(); // normal
            const std::string mr = ta[3].asString();
            md.texPath[3] = mr;               // metallic
            md.texPath[4] = mr;               // roughness
            md.texPath[5] = ta[4].asString(); // ao
          } else {
            const size_t n = std::min(ta.size(), md.texPath.size());
            for (size_t i = 0; i < n; ++i) {
              if (ta[i].isString())
                md.texPath[i] = ta[i].asString();
            }
          }
        }
        MaterialHandle h = materials->create(md);
        if (const Value *vg = vm.get("graph"); vg && vg->isObject()) {
          int graphVer = 0;
          if (const Value *vv = vg->get("version"); vv && vv->isNum())
            graphVer = (int)vv->asNum(graphVer);

          if (graphVer >= 3) {
            const Value *vNodes = vg->get("nodes");
            const Value *vLinks = vg->get("links");
            if (vNodes && vNodes->isArray()) {
              MaterialGraph g{};
              if (const Value *va = vg->get("alphaMode"); va && va->isNum())
                g.alphaMode = (MatAlphaMode)(int)va->asNum();
              if (const Value *vc = vg->get("alphaCutoff"); vc && vc->isNum())
                g.alphaCutoff = (float)vc->asNum(g.alphaCutoff);
              if (const Value *vnid = vg->get("nextNodeId"); vnid && vnid->isNum())
                g.nextNodeId = (uint32_t)vnid->asNum(g.nextNodeId);
              if (const Value *vlid = vg->get("nextLinkId"); vlid && vlid->isNum())
                g.nextLinkId = (uint64_t)vlid->asNum(g.nextLinkId);

              for (const Value &vn : vNodes->asArray()) {
                if (!vn.isObject())
                  continue;
                MatNode n{};
                if (const Value *vid = vn.get("id"); vid && vid->isNum())
                  n.id = (MatNodeID)(uint32_t)vid->asNum();
                if (const Value *vt = vn.get("type"); vt && vt->isNum())
                  n.type = (MatNodeType)(uint32_t)vt->asNum();
                if (const Value *vl = vn.get("label"); vl && vl->isString())
                  n.label = vl->asString();
                if (const Value *vp = vn.get("pos"); vp && vp->isArray())
                  readVec2(*vp, n.pos.x, n.pos.y);
                if (const Value *vps = vn.get("posSet"); vps && vps->isBool())
                  n.posSet = vps->asBool(false);
                if (const Value *vf = vn.get("f"); vf && vf->isArray())
                  readVec4(*vf, n.f.x, n.f.y, n.f.z, n.f.w);
                if (const Value *vu = vn.get("u"); vu && vu->isArray())
                  readUVec4(*vu, n.u.x, n.u.y, n.u.z, n.u.w);
                if (const Value *vp = vn.get("path"); vp && vp->isString())
                  n.path = vp->asString();
                if (!n.path.empty() &&
                    (n.type == MatNodeType::Texture2D ||
                     n.type == MatNodeType::TextureMRA ||
                     n.type == MatNodeType::NormalMap)) {
                  const bool srgb = (n.type == MatNodeType::Texture2D) ? (n.u.y != 0) : false;
                  uint32_t idx = materials->textures().getOrCreate2D(n.path, srgb);
                  if (idx != TextureTable::Invalid)
                    n.u.x = idx;
                }
                g.nodes.push_back(std::move(n));
              }

              if (vLinks && vLinks->isArray()) {
                for (const Value &vl : vLinks->asArray()) {
                  if (!vl.isObject())
                    continue;
                  MatLink l{};
                  if (const Value *vid = vl.get("id"); vid && vid->isNum())
                    l.id = (uint64_t)vid->asNum();
                  if (const Value *vf = vl.get("from"); vf && vf->isArray()) {
                    const auto &a = vf->asArray();
                    if (a.size() >= 2) {
                      l.from.node = (MatNodeID)(uint32_t)a[0].asNum();
                      l.from.slot = (uint32_t)a[1].asNum();
                    }
                  }
                  if (const Value *vt = vl.get("to"); vt && vt->isArray()) {
                    const auto &a = vt->asArray();
                    if (a.size() >= 2) {
                      l.to.node = (MatNodeID)(uint32_t)a[0].asNum();
                      l.to.slot = (uint32_t)a[1].asNum();
                    }
                  }
                  g.links.push_back(std::move(l));
                }
              }

              if (g.findSurfaceOutput() == 0) {
                materials->ensureGraphFromMaterial(h, true);
              } else {
                materials->graph(h) = std::move(g);
                materials->markGraphDirty(h);
                materials->syncMaterialFromGraph(h);
              }
            } else {
              materials->ensureGraphFromMaterial(h);
            }
          } else {
            // Discard old graph data entirely.
            materials->ensureGraphFromMaterial(h, true);
          }
        } else {
          materials->ensureGraphFromMaterial(h);
        }

        loadedMaterials.push_back(h);
      }
    }
    for (auto h : loadedMaterials) {
      if (materials->isAlive(h)) {
        if (materials->graph(h).nodes.empty())
          materials->ensureGraphFromMaterial(h);
      }
    }
  }

  if (const Value *vSky = root.get("sky"); vSky && vSky->isObject()) {
    auto &sky = world.skySettings();
    if (const Value *ve = vSky->get("enabled"); ve && ve->isBool())
      sky.enabled = ve->asBool(true);
    if (const Value *vb = vSky->get("drawBackground"); vb && vb->isBool())
      sky.drawBackground = vb->asBool(true);
    if (const Value *vi = vSky->get("intensity"); vi && vi->isNum())
      sky.intensity = (float)vi->asNum(sky.intensity);
    if (const Value *ve = vSky->get("exposure"); ve && ve->isNum())
      sky.exposure = (float)ve->asNum(sky.exposure);
    if (const Value *vy = vSky->get("rotationYawDeg"); vy && vy->isNum())
      sky.rotationYawDeg = (float)vy->asNum(sky.rotationYawDeg);
    if (const Value *va = vSky->get("ambient"); va && va->isNum())
      sky.ambient = (float)va->asNum(sky.ambient);
    if (const Value *vp = vSky->get("hdriPath"); vp && vp->isString())
      sky.hdriPath = vp->asString();
  }

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
        const Value *jh = vt->get("hidden");
        if (jt)
          readVec3(*jt, tx, ty, tz);
        if (jr)
          readQuatWXYZ(*jr, qw, qx, qy, qz);
        if (js)
          readVec3(*js, sx, sy, sz);
        bool hidden = false;
        if (jh && jh->isBool())
          hidden = jh->asBool(false);

        auto &tr = world.transform(e);
        tr.translation = {tx, ty, tz};
        tr.rotation = {qw, qx, qy, qz};
        tr.scale = {sx, sy, sz};
        tr.hidden = hidden;
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
            if (const Value *sref = vs.get("materialRef");
                sref && sref->isString()) {
              sm.materialAssetPath = sref->asString();
            }
            bool setMat = false;
            if (!loadedMaterials.empty()) {
              if (const Value *vmi = vs.get("materialIndex"); vmi && vmi->isNum()) {
                int idx = (int)vmi->asNum(-1.0);
                if (idx >= 0 && idx < (int)loadedMaterials.size()) {
                  sm.material = loadedMaterials[(size_t)idx];
                  setMat = true;
                }
              }
            }
            if (!setMat) {
              if (const Value *smat = vs.get("material");
                  smat && smat->isArray()) {
                const auto &mh = smat->asArray();
                if (mh.size() >= 2) {
                  sm.material.slot = (uint32_t)mh[0].asNum();
                  sm.material.gen = (uint32_t)mh[1].asNum();
                }
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
          if (const Value *vproj = vc->get("projection");
              vproj && vproj->isNum())
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
        // Shadow parameters
        if (const Value *vs = vl->get("castShadow"); vs && vs->isBool())
          L.castShadow = vs->asBool(false);
        if (const Value *vsr = vl->get("shadowRes"); vsr && vsr->isNum())
          L.shadowRes = (uint16_t)vsr->asNum(1024);
        if (const Value *vcr = vl->get("cascadeRes"); vcr && vcr->isNum())
          L.cascadeRes = (uint16_t)vcr->asNum(1024);
        if (const Value *vcc = vl->get("cascadeCount"); vcc && vcc->isNum())
          L.cascadeCount = (uint8_t)vcc->asNum(4);
        if (const Value *vnb = vl->get("normalBias"); vnb && vnb->isNum())
          L.normalBias = (float)vnb->asNum(0.0025f);
        if (const Value *vsb = vl->get("slopeBias"); vsb && vsb->isNum())
          L.slopeBias = (float)vsb->asNum(1.0f);
      }
    }

    if (world.hasCamera(e) && world.hasMesh(e)) {
      world.removeMesh(e);
    }

    if (world.hasLight(e) && !world.hasMesh(e)) {
      auto &mc = world.ensureMesh(e);
      if (mc.submeshes.empty())
        mc.submeshes.push_back(MeshSubmesh{});
      mc.submeshes[0].name = "Light";
      mc.submeshes[0].type = ProcMeshType::Sphere;
    }
  }

  if (const Value *vCats = root.get("categories")) {
    if (vCats->isArray()) {
      std::vector<int32_t> catParents;
      for (const Value &vc : vCats->asArray()) {
        if (!vc.isObject())
          continue;
        const Value *vn = vc.get("name");
        std::string name = vn && vn->isString() ? vn->asString() : "Category";
        const uint32_t idx = world.addCategory(name);
        const Value *vp = vc.get("parent");
        int32_t pidx = vp && vp->isNum() ? (int32_t)vp->asNum(-1) : -1;
        catParents.push_back(pidx);
        const Value *ve = vc.get("entities");
        if (ve && ve->isArray()) {
          for (const Value &uuidVal : ve->asArray()) {
            const uint64_t uuid = readU64(&uuidVal);
            auto it = map.find(uuid);
            if (it != map.end() && it->second != InvalidEntity)
              world.addEntityCategory(it->second, (int32_t)idx);
          }
        }
      }
      for (uint32_t i = 0; i < catParents.size(); ++i) {
        world.setCategoryParent(i, catParents[i]);
      }
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

bool WorldSerializer::loadFromFile(World &world, const std::string &path) {
  return loadFromFileImpl(world, nullptr, path);
}

bool WorldSerializer::loadFromFile(World &world, MaterialSystem &materials,
                                   const std::string &path) {
  return loadFromFileImpl(world, &materials, path);
}

} // namespace Nyx
