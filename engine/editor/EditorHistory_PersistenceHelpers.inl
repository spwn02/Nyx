
// ---- Persistence helpers ----
using namespace JsonLite;

static Value jVec3(const glm::vec3 &v) {
  Array a;
  a.emplace_back(v.x);
  a.emplace_back(v.y);
  a.emplace_back(v.z);
  return Value(std::move(a));
}
static Value jVec2(const glm::vec2 &v) {
  Array a;
  a.emplace_back(v.x);
  a.emplace_back(v.y);
  return Value(std::move(a));
}
static Value jVec4(const glm::vec4 &v) {
  Array a;
  a.emplace_back(v.x);
  a.emplace_back(v.y);
  a.emplace_back(v.z);
  a.emplace_back(v.w);
  return Value(std::move(a));
}
static Value jQuatWXYZ(const glm::quat &q) {
  Array a;
  a.emplace_back(q.w);
  a.emplace_back(q.x);
  a.emplace_back(q.y);
  a.emplace_back(q.z);
  return Value(std::move(a));
}
static bool readVec3(const Value &v, glm::vec3 &out) {
  if (!v.isArray())
    return false;
  const auto &a = v.asArray();
  if (a.size() < 3)
    return false;
  out.x = (float)a[0].asNum(out.x);
  out.y = (float)a[1].asNum(out.y);
  out.z = (float)a[2].asNum(out.z);
  return true;
}
static bool readVec2(const Value &v, glm::vec2 &out) {
  if (!v.isArray())
    return false;
  const auto &a = v.asArray();
  if (a.size() < 2)
    return false;
  out.x = (float)a[0].asNum(out.x);
  out.y = (float)a[1].asNum(out.y);
  return true;
}
static bool readVec4(const Value &v, glm::vec4 &out) {
  if (!v.isArray())
    return false;
  const auto &a = v.asArray();
  if (a.size() < 4)
    return false;
  out.x = (float)a[0].asNum(out.x);
  out.y = (float)a[1].asNum(out.y);
  out.z = (float)a[2].asNum(out.z);
  out.w = (float)a[3].asNum(out.w);
  return true;
}
static bool readQuat(const Value &v, glm::quat &out) {
  if (!v.isArray())
    return false;
  const auto &a = v.asArray();
  if (a.size() < 4)
    return false;
  out.w = (float)a[0].asNum(out.w);
  out.x = (float)a[1].asNum(out.x);
  out.y = (float)a[2].asNum(out.y);
  out.z = (float)a[3].asNum(out.z);
  return true;
}

static Value jTransform(const CTransform &t) {
  Object o;
  o["t"] = jVec3(t.translation);
  o["r"] = jQuatWXYZ(t.rotation);
  o["s"] = jVec3(t.scale);
  o["hidden"] = t.hidden;
  o["disabledAnim"] = t.disabledAnim;
  return Value(std::move(o));
}
static void readTransform(const Value &v, CTransform &t) {
  if (!v.isObject())
    return;
  if (const Value *jt = v.get("t"))
    readVec3(*jt, t.translation);
  if (const Value *jr = v.get("r"))
    readQuat(*jr, t.rotation);
  if (const Value *js = v.get("s"))
    readVec3(*js, t.scale);
  if (const Value *jh = v.get("hidden"); jh && jh->isBool())
    t.hidden = jh->asBool(t.hidden);
  if (const Value *jd = v.get("disabledAnim"); jd && jd->isBool())
    t.disabledAnim = jd->asBool(t.disabledAnim);
}

static Value jMesh(const CMesh &m) {
  Object o;
  Array subs;
  subs.reserve(m.submeshes.size());
  for (const auto &sm : m.submeshes) {
    Object js;
    js["name"] = sm.name;
    js["type"] = (double)(int)sm.type;
    Array mh;
    mh.emplace_back((double)sm.material.slot);
    mh.emplace_back((double)sm.material.gen);
    js["material"] = Value(std::move(mh));
    subs.emplace_back(Value(std::move(js)));
  }
  o["submeshes"] = Value(std::move(subs));
  return Value(std::move(o));
}
static void readMesh(const Value &v, CMesh &m) {
  if (!v.isObject())
    return;
  if (const Value *vsubs = v.get("submeshes"); vsubs && vsubs->isArray()) {
    const auto &a = vsubs->asArray();
    m.submeshes.clear();
    m.submeshes.reserve(a.size());
    for (const Value &vs : a) {
      if (!vs.isObject())
        continue;
      MeshSubmesh sm{};
      if (const Value *vn = vs.get("name"); vn && vn->isString())
        sm.name = vn->asString();
      if (const Value *vt = vs.get("type"); vt && vt->isNum())
        sm.type = (ProcMeshType)(int)vt->asNum();
      if (const Value *mh = vs.get("material"); mh && mh->isArray()) {
        const auto &ma = mh->asArray();
        if (ma.size() >= 2) {
          sm.material.slot = (uint32_t)ma[0].asNum();
          sm.material.gen = (uint32_t)ma[1].asNum();
        }
      }
      m.submeshes.push_back(std::move(sm));
    }
  }
}

static Value jCamera(const CCamera &c) {
  Object o;
  o["projection"] = (double)(int)c.projection;
  o["fovYDeg"] = c.fovYDeg;
  o["orthoHeight"] = c.orthoHeight;
  o["nearZ"] = c.nearZ;
  o["farZ"] = c.farZ;
  o["exposure"] = c.exposure;
  o["aperture"] = c.aperture;
  o["focusDistance"] = c.focusDistance;
  o["sensorWidth"] = c.sensorWidth;
  o["sensorHeight"] = c.sensorHeight;
  o["dirty"] = c.dirty;
  return Value(std::move(o));
}
static void readCamera(const Value &v, CCamera &c) {
  if (!v.isObject())
    return;
  if (const Value *vp = v.get("projection"); vp && vp->isNum())
    c.projection = (CameraProjection)(int)vp->asNum();
  if (const Value *vf = v.get("fovYDeg"); vf && vf->isNum())
    c.fovYDeg = (float)vf->asNum(c.fovYDeg);
  if (const Value *vo = v.get("orthoHeight"); vo && vo->isNum())
    c.orthoHeight = (float)vo->asNum(c.orthoHeight);
  if (const Value *vn = v.get("nearZ"); vn && vn->isNum())
    c.nearZ = (float)vn->asNum(c.nearZ);
  if (const Value *vf = v.get("farZ"); vf && vf->isNum())
    c.farZ = (float)vf->asNum(c.farZ);
  if (const Value *ve = v.get("exposure"); ve && ve->isNum())
    c.exposure = (float)ve->asNum(c.exposure);
  if (const Value *va = v.get("aperture"); va && va->isNum())
    c.aperture = (float)va->asNum(c.aperture);
  if (const Value *vfd = v.get("focusDistance"); vfd && vfd->isNum())
    c.focusDistance = (float)vfd->asNum(c.focusDistance);
  if (const Value *vsw = v.get("sensorWidth"); vsw && vsw->isNum())
    c.sensorWidth = (float)vsw->asNum(c.sensorWidth);
  if (const Value *vsh = v.get("sensorHeight"); vsh && vsh->isNum())
    c.sensorHeight = (float)vsh->asNum(c.sensorHeight);
  if (const Value *vd = v.get("dirty"); vd && vd->isBool())
    c.dirty = vd->asBool(c.dirty);
}

static Value jCameraMatrices(const CCameraMatrices &m) {
  Object o;
  Array v;
  v.reserve(16);
  const float *p = &m.view[0][0];
  for (int i = 0; i < 16; ++i)
    v.emplace_back(p[i]);
  o["view"] = Value(v);
  v.clear();
  p = &m.proj[0][0];
  for (int i = 0; i < 16; ++i)
    v.emplace_back(p[i]);
  o["proj"] = Value(v);
  v.clear();
  p = &m.viewProj[0][0];
  for (int i = 0; i < 16; ++i)
    v.emplace_back(p[i]);
  o["viewProj"] = Value(v);
  o["dirty"] = m.dirty;
  o["lastW"] = (double)m.lastW;
  o["lastH"] = (double)m.lastH;
  return Value(std::move(o));
}
static void readCameraMatrices(const Value &v, CCameraMatrices &m) {
  if (!v.isObject())
    return;
  auto readMat = [](const Value *arr, glm::mat4 &out) {
    if (!arr || !arr->isArray())
      return;
    const auto &a = arr->asArray();
    if (a.size() < 16)
      return;
    float *p = &out[0][0];
    for (int i = 0; i < 16; ++i)
      p[i] = (float)a[i].asNum(p[i]);
  };
  readMat(v.get("view"), m.view);
  readMat(v.get("proj"), m.proj);
  readMat(v.get("viewProj"), m.viewProj);
  if (const Value *vd = v.get("dirty"); vd && vd->isBool())
    m.dirty = vd->asBool(m.dirty);
  if (const Value *vw = v.get("lastW"); vw && vw->isNum())
    m.lastW = (uint32_t)vw->asNum(m.lastW);
  if (const Value *vh = v.get("lastH"); vh && vh->isNum())
    m.lastH = (uint32_t)vh->asNum(m.lastH);
}

static Value jLight(const CLight &l) {
  Object o;
  o["type"] = (double)(int)l.type;
  o["color"] = jVec3(l.color);
  o["intensity"] = l.intensity;
  o["radius"] = l.radius;
  o["innerAngle"] = l.innerAngle;
  o["outerAngle"] = l.outerAngle;
  o["exposure"] = l.exposure;
  o["enabled"] = l.enabled;
  o["castShadow"] = l.castShadow;
  o["shadowRes"] = (double)l.shadowRes;
  o["cascadeRes"] = (double)l.cascadeRes;
  o["cascadeCount"] = (double)l.cascadeCount;
  o["normalBias"] = l.normalBias;
  o["slopeBias"] = l.slopeBias;
  o["pcfRadius"] = l.pcfRadius;
  o["pointFar"] = l.pointFar;
  return Value(std::move(o));
}
static void readLight(const Value &v, CLight &l) {
  if (!v.isObject())
    return;
  if (const Value *vt = v.get("type"); vt && vt->isNum())
    l.type = (LightType)(int)vt->asNum();
  if (const Value *vc = v.get("color"))
    readVec3(*vc, l.color);
  if (const Value *vi = v.get("intensity"); vi && vi->isNum())
    l.intensity = (float)vi->asNum(l.intensity);
  if (const Value *vr = v.get("radius"); vr && vr->isNum())
    l.radius = (float)vr->asNum(l.radius);
  if (const Value *vi = v.get("innerAngle"); vi && vi->isNum())
    l.innerAngle = (float)vi->asNum(l.innerAngle);
  if (const Value *vo = v.get("outerAngle"); vo && vo->isNum())
    l.outerAngle = (float)vo->asNum(l.outerAngle);
  if (const Value *ve = v.get("exposure"); ve && ve->isNum())
    l.exposure = (float)ve->asNum(l.exposure);
  if (const Value *ve = v.get("enabled"); ve && ve->isBool())
    l.enabled = ve->asBool(l.enabled);
  if (const Value *vs = v.get("castShadow"); vs && vs->isBool())
    l.castShadow = vs->asBool(l.castShadow);
  if (const Value *vsr = v.get("shadowRes"); vsr && vsr->isNum())
    l.shadowRes = (uint16_t)vsr->asNum(l.shadowRes);
  if (const Value *vcr = v.get("cascadeRes"); vcr && vcr->isNum())
    l.cascadeRes = (uint16_t)vcr->asNum(l.cascadeRes);
  if (const Value *vcc = v.get("cascadeCount"); vcc && vcc->isNum())
    l.cascadeCount = (uint8_t)vcc->asNum(l.cascadeCount);
  if (const Value *vnb = v.get("normalBias"); vnb && vnb->isNum())
    l.normalBias = (float)vnb->asNum(l.normalBias);
  if (const Value *vsb = v.get("slopeBias"); vsb && vsb->isNum())
    l.slopeBias = (float)vsb->asNum(l.slopeBias);
  if (const Value *vpf = v.get("pcfRadius"); vpf && vpf->isNum())
    l.pcfRadius = (float)vpf->asNum(l.pcfRadius);
  if (const Value *vpf = v.get("pointFar"); vpf && vpf->isNum())
    l.pointFar = (float)vpf->asNum(l.pointFar);
}

static Value jSky(const CSky &s) {
  Object o;
  o["hdriPath"] = s.hdriPath;
  o["intensity"] = s.intensity;
  o["exposure"] = s.exposure;
  o["rotationYawDeg"] = s.rotationYawDeg;
  o["ambient"] = s.ambient;
  o["enabled"] = s.enabled;
  o["drawBackground"] = s.drawBackground;
  return Value(std::move(o));
}
static void readSky(const Value &v, CSky &s) {
  if (!v.isObject())
    return;
  if (const Value *vp = v.get("hdriPath"); vp && vp->isString())
    s.hdriPath = vp->asString();
  if (const Value *vi = v.get("intensity"); vi && vi->isNum())
    s.intensity = (float)vi->asNum(s.intensity);
  if (const Value *ve = v.get("exposure"); ve && ve->isNum())
    s.exposure = (float)ve->asNum(s.exposure);
  if (const Value *vy = v.get("rotationYawDeg"); vy && vy->isNum())
    s.rotationYawDeg = (float)vy->asNum(s.rotationYawDeg);
  if (const Value *va = v.get("ambient"); va && va->isNum())
    s.ambient = (float)va->asNum(s.ambient);
  if (const Value *ve = v.get("enabled"); ve && ve->isBool())
    s.enabled = ve->asBool(s.enabled);
  if (const Value *vb = v.get("drawBackground"); vb && vb->isBool())
    s.drawBackground = vb->asBool(s.drawBackground);
}

static Value jMaterialData(const MaterialData &m) {
  Object o;
  o["name"] = m.name;
  o["baseColorFactor"] = jVec4(m.baseColorFactor);
  o["emissiveFactor"] = jVec3(m.emissiveFactor);
  o["metallic"] = m.metallic;
  o["roughness"] = m.roughness;
  o["ao"] = m.ao;
  o["uvScale"] = jVec2(m.uvScale);
  o["uvOffset"] = jVec2(m.uvOffset);
  Array tex;
  tex.reserve(m.texPath.size());
  for (const auto &p : m.texPath)
    tex.emplace_back(p);
  o["texPath"] = Value(std::move(tex));
  o["alphaMode"] = (double)(int)m.alphaMode;
  o["alphaCutoff"] = m.alphaCutoff;
  o["tangentSpaceNormal"] = m.tangentSpaceNormal;
  return Value(std::move(o));
}
static void readMaterialData(const Value &v, MaterialData &m) {
  if (!v.isObject())
    return;
  if (const Value *vn = v.get("name"); vn && vn->isString())
    m.name = vn->asString();
  if (const Value *vb = v.get("baseColorFactor"))
    readVec4(*vb, m.baseColorFactor);
  if (const Value *ve = v.get("emissiveFactor"))
    readVec3(*ve, m.emissiveFactor);
  if (const Value *vmc = v.get("metallic"); vmc && vmc->isNum())
    m.metallic = (float)vmc->asNum(m.metallic);
  if (const Value *vr = v.get("roughness"); vr && vr->isNum())
    m.roughness = (float)vr->asNum(m.roughness);
  if (const Value *vao = v.get("ao"); vao && vao->isNum())
    m.ao = (float)vao->asNum(m.ao);
  if (const Value *vus = v.get("uvScale"))
    readVec2(*vus, m.uvScale);
  if (const Value *vuo = v.get("uvOffset"))
    readVec2(*vuo, m.uvOffset);
  if (const Value *vt = v.get("texPath"); vt && vt->isArray()) {
    const auto &ta = vt->asArray();
    const size_t n = std::min(ta.size(), m.texPath.size());
    for (size_t i = 0; i < n; ++i) {
      if (ta[i].isString())
        m.texPath[i] = ta[i].asString();
    }
  }
  if (const Value *vam = v.get("alphaMode"); vam && vam->isNum())
    m.alphaMode = (MatAlphaMode)(int)vam->asNum();
  if (const Value *vac = v.get("alphaCutoff"); vac && vac->isNum())
    m.alphaCutoff = (float)vac->asNum(m.alphaCutoff);
  if (const Value *vtn = v.get("tangentSpaceNormal"); vtn && vtn->isBool())
    m.tangentSpaceNormal = vtn->asBool(m.tangentSpaceNormal);
}

static Value jMaterialGraph(const MaterialGraph &g) {
  Object o;
  o["version"] = 3;
  o["alphaMode"] = (double)(int)g.alphaMode;
  o["alphaCutoff"] = g.alphaCutoff;
  o["nextNodeId"] = (double)g.nextNodeId;
  o["nextLinkId"] = (double)g.nextLinkId;
  Array nodes;
  nodes.reserve(g.nodes.size());
  for (const auto &n : g.nodes) {
    Object jn;
    jn["id"] = (double)n.id;
    jn["type"] = (double)(int)n.type;
    jn["label"] = n.label;
    jn["pos"] = jVec2(n.pos);
    jn["posSet"] = n.posSet;
    jn["f"] = jVec4(n.f);
    Array ju;
    ju.emplace_back((double)n.u.x);
    ju.emplace_back((double)n.u.y);
    ju.emplace_back((double)n.u.z);
    ju.emplace_back((double)n.u.w);
    jn["u"] = Value(std::move(ju));
    jn["path"] = n.path;
    nodes.emplace_back(Value(std::move(jn)));
  }
  o["nodes"] = Value(std::move(nodes));
  Array links;
  links.reserve(g.links.size());
  for (const auto &l : g.links) {
    Object jl;
    jl["id"] = (double)l.id;
    Array from;
    from.emplace_back((double)l.from.node);
    from.emplace_back((double)l.from.slot);
    jl["from"] = Value(std::move(from));
    Array to;
    to.emplace_back((double)l.to.node);
    to.emplace_back((double)l.to.slot);
    jl["to"] = Value(std::move(to));
    links.emplace_back(Value(std::move(jl)));
  }
  o["links"] = Value(std::move(links));
  return Value(std::move(o));
}
static void readMaterialGraph(const Value &v, MaterialGraph &g) {
  if (!v.isObject())
    return;
  if (const Value *va = v.get("alphaMode"); va && va->isNum())
    g.alphaMode = (MatAlphaMode)(int)va->asNum();
  if (const Value *vc = v.get("alphaCutoff"); vc && vc->isNum())
    g.alphaCutoff = (float)vc->asNum(g.alphaCutoff);
  if (const Value *vnid = v.get("nextNodeId"); vnid && vnid->isNum())
    g.nextNodeId = (uint32_t)vnid->asNum(g.nextNodeId);
  if (const Value *vlid = v.get("nextLinkId"); vlid && vlid->isNum())
    g.nextLinkId = (uint64_t)vlid->asNum(g.nextLinkId);
  if (const Value *vNodes = v.get("nodes"); vNodes && vNodes->isArray()) {
    g.nodes.clear();
    for (const Value &vn : vNodes->asArray()) {
      if (!vn.isObject())
        continue;
      MatNode n{};
      if (const Value *vid = vn.get("id"); vid && vid->isNum())
        n.id = (uint32_t)vid->asNum();
      if (const Value *vt = vn.get("type"); vt && vt->isNum())
        n.type = (MatNodeType)(int)vt->asNum();
      if (const Value *vl = vn.get("label"); vl && vl->isString())
        n.label = vl->asString();
      if (const Value *vp = vn.get("pos"))
        readVec2(*vp, n.pos);
      if (const Value *vps = vn.get("posSet"); vps && vps->isBool())
        n.posSet = vps->asBool(n.posSet);
      if (const Value *vf = vn.get("f"))
        readVec4(*vf, n.f);
      if (const Value *vu = vn.get("u"); vu && vu->isArray()) {
        const auto &a = vu->asArray();
        if (a.size() >= 4) {
          n.u.x = (uint32_t)a[0].asNum(n.u.x);
          n.u.y = (uint32_t)a[1].asNum(n.u.y);
          n.u.z = (uint32_t)a[2].asNum(n.u.z);
          n.u.w = (uint32_t)a[3].asNum(n.u.w);
        }
      }
      if (const Value *vp = vn.get("path"); vp && vp->isString())
        n.path = vp->asString();
      g.nodes.push_back(std::move(n));
    }
  }
  if (const Value *vLinks = v.get("links"); vLinks && vLinks->isArray()) {
    g.links.clear();
    for (const Value &vl : vLinks->asArray()) {
      if (!vl.isObject())
        continue;
      MatLink l{};
      if (const Value *vid = vl.get("id"); vid && vid->isNum())
        l.id = (uint64_t)vid->asNum();
      if (const Value *vf = vl.get("from"); vf && vf->isArray()) {
        const auto &a = vf->asArray();
        if (a.size() >= 2) {
          l.from.node = (uint32_t)a[0].asNum();
          l.from.slot = (uint32_t)a[1].asNum();
        }
      }
      if (const Value *vt = vl.get("to"); vt && vt->isArray()) {
        const auto &a = vt->asArray();
        if (a.size() >= 2) {
          l.to.node = (uint32_t)a[0].asNum();
          l.to.slot = (uint32_t)a[1].asNum();
        }
      }
      g.links.push_back(std::move(l));
    }
  }
}

static Value jMaterialSystemSnapshot(const MaterialSystemSnapshot &s) {
  Object o;
  Array slots;
  slots.reserve(s.slots.size());
  for (const auto &ms : s.slots) {
    Object js;
    js["gen"] = (double)ms.gen;
    js["alive"] = ms.alive;
    js["cpu"] = jMaterialData(ms.cpu);
    js["graph"] = jMaterialGraph(ms.graph);
    slots.emplace_back(Value(std::move(js)));
  }
  o["slots"] = Value(std::move(slots));
  Array free;
  free.reserve(s.free.size());
  for (uint32_t f : s.free)
    free.emplace_back((double)f);
  o["free"] = Value(std::move(free));
  o["changeSerial"] = (double)s.changeSerial;
  return Value(std::move(o));
}
static void readMaterialSystemSnapshot(const Value &v,
                                       MaterialSystemSnapshot &s) {
  if (!v.isObject())
    return;
  if (const Value *vs = v.get("slots"); vs && vs->isArray()) {
    s.slots.clear();
    for (const Value &it : vs->asArray()) {
      if (!it.isObject())
        continue;
      MaterialSystem::MaterialSnapshot ms{};
      if (const Value *vg = it.get("gen"); vg && vg->isNum())
        ms.gen = (uint32_t)vg->asNum(ms.gen);
      if (const Value *va = it.get("alive"); va && va->isBool())
        ms.alive = va->asBool(ms.alive);
      if (const Value *vc = it.get("cpu"))
        readMaterialData(*vc, ms.cpu);
      if (const Value *vg = it.get("graph"))
        readMaterialGraph(*vg, ms.graph);
      s.slots.push_back(std::move(ms));
    }
  }
  if (const Value *vf = v.get("free"); vf && vf->isArray()) {
    s.free.clear();
    for (const Value &it : vf->asArray())
      s.free.push_back((uint32_t)it.asNum());
  }
  if (const Value *vc = v.get("changeSerial"); vc && vc->isNum())
    s.changeSerial = (uint64_t)vc->asNum(s.changeSerial);
}

static Value jCategorySnapshot(const CategorySnapshot &s) {
  Object o;
  Array cats;
  cats.reserve(s.categories.size());
  for (const auto &c : s.categories) {
    Object jc;
    jc["name"] = c.name;
    jc["parent"] = (double)c.parent;
    Array ch;
    ch.reserve(c.children.size());
    for (uint32_t v : c.children)
      ch.emplace_back((double)v);
    jc["children"] = Value(std::move(ch));
    Array ents;
    ents.reserve(c.entities.size());
    for (const auto &e : c.entities)
      ents.emplace_back((double)e.index);
    jc["entities"] = Value(std::move(ents));
    cats.emplace_back(Value(std::move(jc)));
  }
  o["categories"] = Value(std::move(cats));
  Object map;
  for (const auto &kv : s.entityCategoriesByUUID) {
    Array arr;
    arr.reserve(kv.second.size());
    for (uint32_t v : kv.second)
      arr.emplace_back((double)v);
    map[std::to_string(kv.first)] = Value(std::move(arr));
  }
  o["entityCategories"] = Value(std::move(map));
  return Value(std::move(o));
}
static void readCategorySnapshot(const Value &v, CategorySnapshot &s) {
  if (!v.isObject())
    return;
  if (const Value *vc = v.get("categories"); vc && vc->isArray()) {
    s.categories.clear();
    for (const Value &it : vc->asArray()) {
      if (!it.isObject())
        continue;
      World::Category c{};
      if (const Value *vn = it.get("name"); vn && vn->isString())
        c.name = vn->asString();
      if (const Value *vp = it.get("parent"); vp && vp->isNum())
        c.parent = (int32_t)vp->asNum(c.parent);
      if (const Value *vch = it.get("children"); vch && vch->isArray()) {
        for (const Value &ch : vch->asArray())
          c.children.push_back((uint32_t)ch.asNum());
      }
      if (const Value *ve = it.get("entities"); ve && ve->isArray()) {
        for (const Value &ch : ve->asArray()) {
          EntityID e{};
          e.index = (uint32_t)ch.asNum();
          e.generation = 1;
          c.entities.push_back(e);
        }
      }
      s.categories.push_back(std::move(c));
    }
  }
  if (const Value *vm = v.get("entityCategories"); vm && vm->isObject()) {
    s.entityCategoriesByUUID.clear();
    for (const auto &kv : vm->asObject()) {
      const uint64_t uuid = std::stoull(kv.first);
      std::vector<uint32_t> cats;
      if (kv.second.isArray()) {
        for (const Value &it : kv.second.asArray())
          cats.push_back((uint32_t)it.asNum());
      }
      s.entityCategoriesByUUID.emplace(uuid, std::move(cats));
    }
  }
}

static Value jEntitySnapshot(const EntitySnapshot &s) {
  Object o;
  o["uuid"] = (double)s.uuid.value;
  o["parent"] = s.parent ? Value((double)s.parent.value) : Value(nullptr);
  o["name"] = s.name.name;
  o["transform"] = jTransform(s.transform);
  o["hasMesh"] = s.hasMesh;
  if (s.hasMesh)
    o["mesh"] = jMesh(s.mesh);
  o["hasCamera"] = s.hasCamera;
  if (s.hasCamera) {
    o["camera"] = jCamera(s.camera);
    o["cameraMatrices"] = jCameraMatrices(s.cameraMatrices);
  }
  o["hasLight"] = s.hasLight;
  if (s.hasLight)
    o["light"] = jLight(s.light);
  o["hasSky"] = s.hasSky;
  if (s.hasSky)
    o["sky"] = jSky(s.sky);
  Array cats;
  cats.reserve(s.categories.size());
  for (uint32_t c : s.categories)
    cats.emplace_back((double)c);
  o["categories"] = Value(std::move(cats));
  return Value(std::move(o));
}
static void readEntitySnapshot(const Value &v, EntitySnapshot &s) {
  if (!v.isObject())
    return;
  if (const Value *vu = v.get("uuid"); vu && vu->isNum())
    s.uuid = EntityUUID{(uint64_t)vu->asNum()};
  if (const Value *vp = v.get("parent"); vp && vp->isNum())
    s.parent = EntityUUID{(uint64_t)vp->asNum()};
  if (const Value *vn = v.get("name"); vn && vn->isString())
    s.name.name = vn->asString();
  if (const Value *vt = v.get("transform"))
    readTransform(*vt, s.transform);
  if (const Value *vhm = v.get("hasMesh"); vhm && vhm->isBool())
    s.hasMesh = vhm->asBool(s.hasMesh);
  if (s.hasMesh && v.get("mesh"))
    readMesh(*v.get("mesh"), s.mesh);
  if (const Value *vhc = v.get("hasCamera"); vhc && vhc->isBool())
    s.hasCamera = vhc->asBool(s.hasCamera);
  if (s.hasCamera && v.get("camera"))
    readCamera(*v.get("camera"), s.camera);
  if (s.hasCamera && v.get("cameraMatrices"))
    readCameraMatrices(*v.get("cameraMatrices"), s.cameraMatrices);
  if (const Value *vhl = v.get("hasLight"); vhl && vhl->isBool())
    s.hasLight = vhl->asBool(s.hasLight);
  if (s.hasLight && v.get("light"))
    readLight(*v.get("light"), s.light);
  if (const Value *vhs = v.get("hasSky"); vhs && vhs->isBool())
    s.hasSky = vhs->asBool(s.hasSky);
  if (s.hasSky && v.get("sky"))
    readSky(*v.get("sky"), s.sky);
  if (const Value *vc = v.get("categories"); vc && vc->isArray()) {
    for (const Value &it : vc->asArray())
      s.categories.push_back((uint32_t)it.asNum());
  }
}

static Value jSelection(const HistorySelectionSnapshot &s) {
  Object o;
  o["kind"] = (double)(int)s.kind;
  o["activeMaterial"] = Value((double)((uint64_t)s.activeMaterial.slot << 32 |
                                       (uint64_t)s.activeMaterial.gen));
  Array picks;
  for (const auto &p : s.picks) {
    Object jp;
    jp["uuid"] = (double)p.first.value;
    jp["sub"] = (double)p.second;
    picks.emplace_back(Value(std::move(jp)));
  }
  o["picks"] = Value(std::move(picks));
  if (s.activePick.first)
    o["activePick"] = Value((double)s.activePick.first.value);
  if (s.activeEntity)
    o["activeEntity"] = Value((double)s.activeEntity.value);
  return Value(std::move(o));
}
static void readSelection(const Value &v, HistorySelectionSnapshot &s) {
  if (!v.isObject())
    return;
  if (const Value *vk = v.get("kind"); vk && vk->isNum())
    s.kind = (SelectionKind)(int)vk->asNum();
  if (const Value *vam = v.get("activeMaterial"); vam && vam->isNum()) {
    const uint64_t packed = (uint64_t)vam->asNum();
    s.activeMaterial.slot = (uint32_t)(packed >> 32);
    s.activeMaterial.gen = (uint32_t)(packed & 0xffffffffu);
  }
  if (const Value *vp = v.get("picks"); vp && vp->isArray()) {
    for (const Value &it : vp->asArray()) {
      if (!it.isObject())
        continue;
      EntityUUID u{};
      uint32_t sub = 0;
      if (const Value *vu = it.get("uuid"); vu && vu->isNum())
        u = EntityUUID{(uint64_t)vu->asNum()};
      if (const Value *vs = it.get("sub"); vs && vs->isNum())
        sub = (uint32_t)vs->asNum();
      if (u)
        s.picks.emplace_back(u, sub);
    }
  }
  if (const Value *va = v.get("activePick"); va && va->isNum())
    s.activePick = {EntityUUID{(uint64_t)va->asNum()}, 0};
  if (const Value *ve = v.get("activeEntity"); ve && ve->isNum())
    s.activeEntity = EntityUUID{(uint64_t)ve->asNum()};
}

static Value jHistoryOp(const HistoryOp &op) {
  Object o;
  std::visit(
      [&](auto &v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, OpEntityCreate>) {
          o["type"] = "EntityCreate";
          o["snap"] = jEntitySnapshot(v.snap);
        } else if constexpr (std::is_same_v<T, OpEntityDestroy>) {
          o["type"] = "EntityDestroy";
          o["snap"] = jEntitySnapshot(v.snap);
        } else if constexpr (std::is_same_v<T, OpTransform>) {
          o["type"] = "Transform";
          o["uuid"] = (double)v.uuid.value;
          o["before"] = jTransform(v.before);
          o["after"] = jTransform(v.after);
        } else if constexpr (std::is_same_v<T, OpName>) {
          o["type"] = "Name";
          o["uuid"] = (double)v.uuid.value;
          o["before"] = v.before;
          o["after"] = v.after;
        } else if constexpr (std::is_same_v<T, OpParent>) {
          o["type"] = "Parent";
          o["uuid"] = (double)v.uuid.value;
          o["before"] = v.before ? Value((double)v.before.value) : Value(nullptr);
          o["after"] = v.after ? Value((double)v.after.value) : Value(nullptr);
        } else if constexpr (std::is_same_v<T, OpMesh>) {
          o["type"] = "Mesh";
          o["uuid"] = (double)v.uuid.value;
          o["beforeHas"] = v.beforeHasMesh;
          o["afterHas"] = v.afterHasMesh;
          if (v.beforeHasMesh)
            o["before"] = jMesh(v.before);
          if (v.afterHasMesh)
            o["after"] = jMesh(v.after);
        } else if constexpr (std::is_same_v<T, OpLight>) {
          o["type"] = "Light";
          o["uuid"] = (double)v.uuid.value;
          o["beforeHas"] = v.beforeHasLight;
          o["afterHas"] = v.afterHasLight;
          if (v.beforeHasLight)
            o["before"] = jLight(v.before);
          if (v.afterHasLight)
            o["after"] = jLight(v.after);
        } else if constexpr (std::is_same_v<T, OpCamera>) {
          o["type"] = "Camera";
          o["uuid"] = (double)v.uuid.value;
          o["beforeHas"] = v.beforeHasCamera;
          o["afterHas"] = v.afterHasCamera;
          if (v.beforeHasCamera) {
            o["before"] = jCamera(v.before);
            o["beforeMat"] = jCameraMatrices(v.beforeMat);
          }
          if (v.afterHasCamera) {
            o["after"] = jCamera(v.after);
            o["afterMat"] = jCameraMatrices(v.afterMat);
          }
        } else if constexpr (std::is_same_v<T, OpSky>) {
          o["type"] = "Sky";
          o["before"] = jSky(v.before);
          o["after"] = jSky(v.after);
        } else if constexpr (std::is_same_v<T, OpActiveCamera>) {
          o["type"] = "ActiveCamera";
          o["before"] = v.before ? Value((double)v.before.value) : Value(nullptr);
          o["after"] = v.after ? Value((double)v.after.value) : Value(nullptr);
        } else if constexpr (std::is_same_v<T, OpCategories>) {
          o["type"] = "Categories";
          o["before"] = jCategorySnapshot(v.before);
          o["after"] = jCategorySnapshot(v.after);
        } else if constexpr (std::is_same_v<T, OpMaterials>) {
          o["type"] = "Materials";
          o["before"] = jMaterialSystemSnapshot(v.before);
          o["after"] = jMaterialSystemSnapshot(v.after);
        }
      },
      op);
  return Value(std::move(o));
}

static bool readHistoryOp(const Value &v, HistoryOp &out) {
  if (!v.isObject())
    return false;
  const Value *vt = v.get("type");
  if (!vt || !vt->isString())
    return false;
  const std::string &t = vt->asString();
  if (t == "EntityCreate" || t == "EntityDestroy") {
    EntitySnapshot s{};
    if (const Value *vs = v.get("snap"))
      readEntitySnapshot(*vs, s);
    if (t == "EntityCreate")
      out = OpEntityCreate{s};
    else
      out = OpEntityDestroy{s};
    return true;
  }
  if (t == "Transform") {
    OpTransform op{};
    if (const Value *vu = v.get("uuid"); vu && vu->isNum())
      op.uuid = EntityUUID{(uint64_t)vu->asNum()};
    if (const Value *vb = v.get("before"))
      readTransform(*vb, op.before);
    if (const Value *va = v.get("after"))
      readTransform(*va, op.after);
    out = op;
    return true;
  }
  if (t == "Name") {
    OpName op{};
    if (const Value *vu = v.get("uuid"); vu && vu->isNum())
      op.uuid = EntityUUID{(uint64_t)vu->asNum()};
    if (const Value *vb = v.get("before"); vb && vb->isString())
      op.before = vb->asString();
    if (const Value *va = v.get("after"); va && va->isString())
      op.after = va->asString();
    out = op;
    return true;
  }
  if (t == "Parent") {
    OpParent op{};
    if (const Value *vu = v.get("uuid"); vu && vu->isNum())
      op.uuid = EntityUUID{(uint64_t)vu->asNum()};
    if (const Value *vb = v.get("before"); vb && vb->isNum())
      op.before = EntityUUID{(uint64_t)vb->asNum()};
    if (const Value *va = v.get("after"); va && va->isNum())
      op.after = EntityUUID{(uint64_t)va->asNum()};
    out = op;
    return true;
  }
  if (t == "Mesh") {
    OpMesh op{};
    if (const Value *vu = v.get("uuid"); vu && vu->isNum())
      op.uuid = EntityUUID{(uint64_t)vu->asNum()};
    if (const Value *vb = v.get("beforeHas"); vb && vb->isBool())
      op.beforeHasMesh = vb->asBool(op.beforeHasMesh);
    if (const Value *va = v.get("afterHas"); va && va->isBool())
      op.afterHasMesh = va->asBool(op.afterHasMesh);
    if (const Value *vb = v.get("before"))
      readMesh(*vb, op.before);
    if (const Value *va = v.get("after"))
      readMesh(*va, op.after);
    out = op;
    return true;
  }
  if (t == "Light") {
    OpLight op{};
    if (const Value *vu = v.get("uuid"); vu && vu->isNum())
      op.uuid = EntityUUID{(uint64_t)vu->asNum()};
    if (const Value *vb = v.get("beforeHas"); vb && vb->isBool())
      op.beforeHasLight = vb->asBool(op.beforeHasLight);
    if (const Value *va = v.get("afterHas"); va && va->isBool())
      op.afterHasLight = va->asBool(op.afterHasLight);
    if (const Value *vb = v.get("before"))
      readLight(*vb, op.before);
    if (const Value *va = v.get("after"))
      readLight(*va, op.after);
    out = op;
    return true;
  }
  if (t == "Camera") {
    OpCamera op{};
    if (const Value *vu = v.get("uuid"); vu && vu->isNum())
      op.uuid = EntityUUID{(uint64_t)vu->asNum()};
    if (const Value *vb = v.get("beforeHas"); vb && vb->isBool())
      op.beforeHasCamera = vb->asBool(op.beforeHasCamera);
    if (const Value *va = v.get("afterHas"); va && va->isBool())
      op.afterHasCamera = va->asBool(op.afterHasCamera);
    if (const Value *vb = v.get("before"))
      readCamera(*vb, op.before);
    if (const Value *va = v.get("after"))
      readCamera(*va, op.after);
    if (const Value *vb = v.get("beforeMat"))
      readCameraMatrices(*vb, op.beforeMat);
    if (const Value *va = v.get("afterMat"))
      readCameraMatrices(*va, op.afterMat);
    out = op;
    return true;
  }
  if (t == "Sky") {
    OpSky op{};
    if (const Value *vb = v.get("before"))
      readSky(*vb, op.before);
    if (const Value *va = v.get("after"))
      readSky(*va, op.after);
    out = op;
    return true;
  }
  if (t == "ActiveCamera") {
    OpActiveCamera op{};
    if (const Value *vb = v.get("before"); vb && vb->isNum())
      op.before = EntityUUID{(uint64_t)vb->asNum()};
    if (const Value *va = v.get("after"); va && va->isNum())
      op.after = EntityUUID{(uint64_t)va->asNum()};
    out = op;
    return true;
  }
  if (t == "Categories") {
    OpCategories op{};
    if (const Value *vb = v.get("before"))
      readCategorySnapshot(*vb, op.before);
    if (const Value *va = v.get("after"))
      readCategorySnapshot(*va, op.after);
    out = op;
    return true;
  }
  if (t == "Materials") {
    OpMaterials op{};
    if (const Value *vb = v.get("before"))
      readMaterialSystemSnapshot(*vb, op.before);
    if (const Value *va = v.get("after"))
      readMaterialSystemSnapshot(*va, op.after);
    out = op;
    return true;
  }
  return false;
}

