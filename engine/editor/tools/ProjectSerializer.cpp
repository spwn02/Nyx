#include "ProjectSerializer.h"

#include "scene/JsonLite.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdint>

namespace Nyx {

using namespace JsonLite;

static std::string u64ToString(uint64_t v) { return std::to_string(v); }

static bool parseU64Field(const Value *v, uint64_t &out) {
  if (!v)
    return false;
  if (v->isString()) {
    const std::string s = v->asString();
    if (s.empty())
      return false;
    try {
      out = (uint64_t)std::stoull(s);
      return true;
    } catch (...) {
      return false;
    }
  }
  if (v->isNum()) {
    out = (uint64_t)v->asNum((double)out);
    return true;
  }
  return false;
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
  std::ofstream f(path, std::ios::out | std::ios::binary | std::ios::trunc);
  if (!f.is_open())
    return false;
  f.write(text.data(), (std::streamsize)text.size());
  return true;
}

static Object jPanels(const PanelState &p) {
  Object o;
  o["Hierarchy"] = p.showHierarchy;
  o["Inspector"] = p.showInspector;
  o["Viewport"] = p.showViewport;
  o["Assets"] = p.showAssets;
  o["Stats"] = p.showStats;
  o["Console"] = p.showConsole;
  o["Graph"] = p.showGraph;
  return o;
}

static void readPanels(PanelState &p, const Value &v) {
  if (!v.isObject())
    return;
  const auto *x = v.get("Hierarchy");
  if (x && x->isBool())
    p.showHierarchy = x->asBool();
  x = v.get("Inspector");
  if (x && x->isBool())
    p.showInspector = x->asBool();
  x = v.get("Viewport");
  if (x && x->isBool())
    p.showViewport = x->asBool();
  x = v.get("Assets");
  if (x && x->isBool())
    p.showAssets = x->asBool();
  x = v.get("Stats");
  if (x && x->isBool())
    p.showStats = x->asBool();
  x = v.get("Console");
  if (x && x->isBool())
    p.showConsole = x->asBool();
  x = v.get("Graph");
  if (x && x->isBool())
    p.showGraph = x->asBool();
}

static Object jViewport(const EditorViewportPrefs &v) {
  Object o;
  o["ShowGrid"] = v.showGrid;
  o["ShowGizmos"] = v.showGizmos;
  o["ShowSelectionOutline"] = v.showSelectionOutline;
  o["MSAA"] = (double)v.msaa;
  o["Exposure"] = (double)v.exposure;
  o["OutlineThicknessPx"] = (double)v.outlineThicknessPx;
  o["ViewMode"] = (double)(int)v.viewMode;
  return o;
}

static void readViewport(EditorViewportPrefs &o, const Value &v) {
  if (!v.isObject())
    return;
  const Value *x = nullptr;

  x = v.get("ShowGrid");
  if (x && x->isBool())
    o.showGrid = x->asBool();
  x = v.get("ShowGizmos");
  if (x && x->isBool())
    o.showGizmos = x->asBool();
  x = v.get("ShowSelectionOutline");
  if (x && x->isBool())
    o.showSelectionOutline = x->asBool();

  x = v.get("MSAA");
  if (x && x->isNum())
    o.msaa = (uint32_t)x->asNum(1.0);
  x = v.get("Exposure");
  if (x && x->isNum())
    o.exposure = (float)x->asNum(0.0);
  x = v.get("OutlineThicknessPx");
  if (x && x->isNum())
    o.outlineThicknessPx = (float)x->asNum(o.outlineThicknessPx);
  x = v.get("ViewMode");
  if (x && x->isNum())
    o.viewMode = (ViewMode)(int)x->asNum(0.0);
}

static Object jAnimTangent(const AnimTangent &t) {
  Object o;
  o["dx"] = (double)t.dx;
  o["dy"] = (double)t.dy;
  return o;
}

static void readAnimTangent(AnimTangent &t, const Value &v) {
  if (!v.isObject())
    return;
  if (const Value *x = v.get("dx"); x && x->isNum())
    t.dx = (float)x->asNum((double)t.dx);
  if (const Value *x = v.get("dy"); x && x->isNum())
    t.dy = (float)x->asNum((double)t.dy);
}

static Object jAnimKey(const AnimKey &k) {
  Object o;
  o["frame"] = (double)k.frame;
  o["value"] = (double)k.value;
  o["in"] = Value(jAnimTangent(k.in));
  o["out"] = Value(jAnimTangent(k.out));
  o["easeOut"] = (double)(int)k.easeOut;
  return o;
}

static bool readAnimKey(AnimKey &k, const Value &v) {
  if (!v.isObject())
    return false;
  if (const Value *x = v.get("frame"); x && x->isNum())
    k.frame = (AnimFrame)x->asNum((double)k.frame);
  if (const Value *x = v.get("value"); x && x->isNum())
    k.value = (float)x->asNum((double)k.value);
  if (const Value *x = v.get("in"))
    readAnimTangent(k.in, *x);
  if (const Value *x = v.get("out"))
    readAnimTangent(k.out, *x);
  if (const Value *x = v.get("easeOut"); x && x->isNum())
    k.easeOut = (SegmentEase)(int)x->asNum((double)(int)k.easeOut);
  return true;
}

static Object jAnimCurve(const AnimCurve &c) {
  Object o;
  o["interp"] = (double)(int)c.interp;
  Array keys;
  keys.reserve(c.keys.size());
  for (const auto &k : c.keys)
    keys.emplace_back(Value(jAnimKey(k)));
  o["keys"] = Value(std::move(keys));
  return o;
}

static bool readAnimCurve(AnimCurve &c, const Value &v) {
  if (!v.isObject())
    return false;
  if (const Value *x = v.get("interp"); x && x->isNum())
    c.interp = (InterpMode)(int)x->asNum((double)(int)c.interp);
  if (const Value *x = v.get("keys"); x && x->isArray()) {
    c.keys.clear();
    c.keys.reserve(x->asArray().size());
    for (const Value &it : x->asArray()) {
      AnimKey k{};
      if (readAnimKey(k, it))
        c.keys.push_back(k);
    }
    std::sort(c.keys.begin(), c.keys.end(),
              [](const AnimKey &a, const AnimKey &b) {
                return a.frame < b.frame;
              });
  }
  return true;
}

static Object jAnimationClip(const PersistedAnimationClip &clip) {
  Object o;
  o["valid"] = clip.valid;
  o["name"] = clip.name;
  o["lastFrame"] = (double)clip.lastFrame;
  o["loop"] = clip.loop;
  o["nextBlockId"] = (double)clip.nextBlockId;

  Array tracks;
  tracks.reserve(clip.tracks.size());
  for (const auto &t : clip.tracks) {
    Object to;
    to["entityUUID"] = u64ToString(t.entity.value);
    to["blockId"] = (double)t.blockId;
    to["channel"] = (double)(int)t.channel;
    to["curve"] = Value(jAnimCurve(t.curve));
    tracks.emplace_back(Value(std::move(to)));
  }
  o["tracks"] = Value(std::move(tracks));

  Array ranges;
  ranges.reserve(clip.ranges.size());
  for (const auto &r : clip.ranges) {
    Object ro;
    ro["entityUUID"] = u64ToString(r.entity.value);
    ro["blockId"] = (double)r.blockId;
    ro["start"] = (double)r.start;
    ro["end"] = (double)r.end;
    ranges.emplace_back(Value(std::move(ro)));
  }
  o["ranges"] = Value(std::move(ranges));
  return o;
}

static Object jSequencer(const SequencerPersistState &s) {
  Object o;
  o["valid"] = s.valid;
  o["pixelsPerFrame"] = (double)s.pixelsPerFrame;
  o["labelGutter"] = (double)s.labelGutter;
  o["viewFirstFrame"] = (double)s.viewFirstFrame;
  o["autoUpdateLastFrame"] = s.autoUpdateLastFrame;
  o["sortMode"] = (double)s.sortMode;
  o["showGraphPanel"] = s.showGraphPanel;
  o["search"] = s.search;

  Array ex;
  ex.reserve(s.expand.size());
  for (const auto &t : s.expand) {
    Object to;
    to["entityUUID"] = u64ToString(t.entity.value);
    to["rowType"] = (double)t.rowType;
    to["prop"] = (double)t.prop;
    to["value"] = t.value;
    ex.emplace_back(Value(std::move(to)));
  }
  o["expand"] = Value(std::move(ex));

  Array sw;
  sw.reserve(s.stopwatch.size());
  for (const auto &t : s.stopwatch) {
    Object to;
    to["entityUUID"] = u64ToString(t.entity.value);
    to["rowType"] = (double)t.rowType;
    to["prop"] = (double)t.prop;
    to["value"] = t.value;
    sw.emplace_back(Value(std::move(to)));
  }
  o["stopwatch"] = Value(std::move(sw));

  Array sl;
  sl.reserve(s.selectedLayers.size());
  for (const auto &u : s.selectedLayers)
    sl.emplace_back(Value(u64ToString(u.value)));
  o["selectedLayers"] = Value(std::move(sl));
  return o;
}

static void readSequencer(SequencerPersistState &s, const Value &v) {
  if (!v.isObject())
    return;
  s.valid = true;
  if (const Value *x = v.get("valid"); x && x->isBool())
    s.valid = x->asBool(s.valid);
  if (const Value *x = v.get("pixelsPerFrame"); x && x->isNum())
    s.pixelsPerFrame = (float)x->asNum((double)s.pixelsPerFrame);
  if (const Value *x = v.get("labelGutter"); x && x->isNum())
    s.labelGutter = (float)x->asNum((double)s.labelGutter);
  if (const Value *x = v.get("viewFirstFrame"); x && x->isNum())
    s.viewFirstFrame = (int32_t)x->asNum((double)s.viewFirstFrame);
  if (const Value *x = v.get("autoUpdateLastFrame"); x && x->isBool())
    s.autoUpdateLastFrame = x->asBool(s.autoUpdateLastFrame);
  if (const Value *x = v.get("sortMode"); x && x->isNum())
    s.sortMode = (int)x->asNum((double)s.sortMode);
  if (const Value *x = v.get("showGraphPanel"); x && x->isBool())
    s.showGraphPanel = x->asBool(s.showGraphPanel);
  if (const Value *x = v.get("search"); x && x->isString())
    s.search = x->asString();

  s.expand.clear();
  if (const Value *x = v.get("expand"); x && x->isArray()) {
    s.expand.reserve(x->asArray().size());
    for (const Value &it : x->asArray()) {
      if (!it.isObject())
        continue;
      SequencerPersistToggle t{};
      if (const Value *vu = it.get("entityUUID"))
        parseU64Field(vu, t.entity.value);
      if (const Value *vr = it.get("rowType"); vr && vr->isNum())
        t.rowType = (uint8_t)vr->asNum((double)t.rowType);
      if (const Value *vp = it.get("prop"); vp && vp->isNum())
        t.prop = (uint8_t)vp->asNum((double)t.prop);
      if (const Value *vv = it.get("value"); vv && vv->isBool())
        t.value = vv->asBool(t.value);
      if (t.entity)
        s.expand.push_back(t);
    }
  }

  s.stopwatch.clear();
  if (const Value *x = v.get("stopwatch"); x && x->isArray()) {
    s.stopwatch.reserve(x->asArray().size());
    for (const Value &it : x->asArray()) {
      if (!it.isObject())
        continue;
      SequencerPersistToggle t{};
      if (const Value *vu = it.get("entityUUID"))
        parseU64Field(vu, t.entity.value);
      if (const Value *vr = it.get("rowType"); vr && vr->isNum())
        t.rowType = (uint8_t)vr->asNum((double)t.rowType);
      if (const Value *vp = it.get("prop"); vp && vp->isNum())
        t.prop = (uint8_t)vp->asNum((double)t.prop);
      if (const Value *vv = it.get("value"); vv && vv->isBool())
        t.value = vv->asBool(t.value);
      if (t.entity)
        s.stopwatch.push_back(t);
    }
  }

  s.selectedLayers.clear();
  if (const Value *x = v.get("selectedLayers"); x && x->isArray()) {
    s.selectedLayers.reserve(x->asArray().size());
    for (const Value &it : x->asArray()) {
      uint64_t u = 0;
      if (parseU64Field(&it, u) && u != 0)
        s.selectedLayers.push_back(EntityUUID{u});
    }
  }
}

static void readAnimationClip(PersistedAnimationClip &clip, const Value &v) {
  if (!v.isObject())
    return;
  clip.valid = true;
  if (const Value *x = v.get("valid"); x && x->isBool())
    clip.valid = x->asBool(clip.valid);
  if (const Value *x = v.get("name"); x && x->isString())
    clip.name = x->asString();
  if (const Value *x = v.get("lastFrame"); x && x->isNum())
    clip.lastFrame = std::max<AnimFrame>(0, (AnimFrame)x->asNum(clip.lastFrame));
  if (const Value *x = v.get("loop"); x && x->isBool())
    clip.loop = x->asBool(clip.loop);
  if (const Value *x = v.get("nextBlockId"); x && x->isNum())
    clip.nextBlockId = (uint32_t)x->asNum((double)clip.nextBlockId);
  if (clip.nextBlockId == 0)
    clip.nextBlockId = 1;

  clip.tracks.clear();
  if (const Value *x = v.get("tracks"); x && x->isArray()) {
    clip.tracks.reserve(x->asArray().size());
    for (const Value &it : x->asArray()) {
      if (!it.isObject())
        continue;
      PersistedAnimTrack t{};
      if (const Value *vu = it.get("entityUUID"))
        parseU64Field(vu, t.entity.value);
      if (const Value *vb = it.get("blockId"); vb && vb->isNum())
        t.blockId = (uint32_t)vb->asNum((double)t.blockId);
      if (const Value *vc = it.get("channel"); vc && vc->isNum())
        t.channel = (AnimChannel)(int)vc->asNum();
      if (const Value *vcurve = it.get("curve"))
        readAnimCurve(t.curve, *vcurve);
      if (t.entity)
        clip.tracks.push_back(std::move(t));
    }
  }

  clip.ranges.clear();
  if (const Value *x = v.get("ranges"); x && x->isArray()) {
    clip.ranges.reserve(x->asArray().size());
    for (const Value &it : x->asArray()) {
      if (!it.isObject())
        continue;
      PersistedAnimRange r{};
      if (const Value *vu = it.get("entityUUID"))
        parseU64Field(vu, r.entity.value);
      if (const Value *vb = it.get("blockId"); vb && vb->isNum())
        r.blockId = (uint32_t)vb->asNum((double)r.blockId);
      if (const Value *vs = it.get("start"); vs && vs->isNum())
        r.start = (AnimFrame)vs->asNum((double)r.start);
      if (const Value *ve = it.get("end"); ve && ve->isNum())
        r.end = (AnimFrame)ve->asNum((double)r.end);
      if (r.end < r.start)
        std::swap(r.start, r.end);
      if (r.entity)
        clip.ranges.push_back(std::move(r));
    }
  }
}

bool ProjectSerializer::saveToFile(const EditorState &st,
                                  const std::string &path) {
  Object root;
  root["type"] = "NyxProject";
  root["version"] = 1;

  root["LastScene"] = st.lastScenePath;
  root["UUIDSeed"] = (double)st.uuidSeed;
  root["AutoSave"] = st.autoSave;

  root["ActiveCamera"] = (double)st.activeCamera.value;

  root["GizmoOp"] = (double)(int)st.gizmoOp;
  root["GizmoMode"] = (double)(int)st.gizmoMode;

  root["Panels"] = Value(jPanels(st.panels));
  root["Viewport"] = Value(jViewport(st.viewport));

  root["DockFallbackApplied"] = st.dockFallbackApplied;
  root["ProjectFPS"] = (double)st.projectFps;
  root["AnimationFrame"] = (double)st.animationFrame;
  root["AnimationPlaying"] = st.animationPlaying;
  root["AnimationLoop"] = st.animationLoop;
  root["AnimationLastFrame"] = (double)st.animationLastFrame;
  root["AnimationClip"] = Value(jAnimationClip(st.animationClip));
  root["Sequencer"] = Value(jSequencer(st.sequencer));

  {
    Array a;
    a.reserve(st.recentScenes.size());
    for (const auto &s : st.recentScenes)
      a.emplace_back(Value(s));
    root["RecentScenes"] = Value(std::move(a));
  }

  const std::string out = stringify(Value(std::move(root)), true, 2);
  return writeAllText(path, out);
}

bool ProjectSerializer::loadFromFile(EditorState &st,
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

  const Value *t = root.get("type");
  if (!t || !t->isString() || t->asString() != "NyxProject")
    return false;

  if (const Value *v = root.get("LastScene"); v && v->isString())
    st.lastScenePath = v->asString();

  if (const Value *v = root.get("UUIDSeed"); v && v->isNum())
    st.uuidSeed = (uint64_t)v->asNum((double)st.uuidSeed);

  if (const Value *v = root.get("AutoSave"); v && v->isBool())
    st.autoSave = v->asBool(false);

  if (const Value *v = root.get("ActiveCamera"); v && v->isNum())
    st.activeCamera.value = (uint64_t)v->asNum(0.0);

  if (const Value *v = root.get("GizmoOp"); v && v->isNum())
    st.gizmoOp = (GizmoOp)(int)v->asNum(0.0);

  if (const Value *v = root.get("GizmoMode"); v && v->isNum())
    st.gizmoMode = (GizmoMode)(int)v->asNum(0.0);

  if (const Value *v = root.get("Panels"))
    readPanels(st.panels, *v);

  if (const Value *v = root.get("Viewport"))
    readViewport(st.viewport, *v);

  if (const Value *v = root.get("DockFallbackApplied"); v && v->isBool())
    st.dockFallbackApplied = v->asBool(false);

  if (const Value *v = root.get("ProjectFPS"); v && v->isNum())
    st.projectFps = (float)v->asNum(st.projectFps);
  if (const Value *v = root.get("AnimationFrame"); v && v->isNum())
    st.animationFrame = (int32_t)v->asNum((double)st.animationFrame);
  if (const Value *v = root.get("AnimationPlaying"); v && v->isBool())
    st.animationPlaying = v->asBool(st.animationPlaying);
  if (const Value *v = root.get("AnimationLoop"); v && v->isBool())
    st.animationLoop = v->asBool(st.animationLoop);
  if (const Value *v = root.get("AnimationLastFrame"); v && v->isNum())
    st.animationLastFrame = (int32_t)v->asNum((double)st.animationLastFrame);
  if (const Value *v = root.get("AnimationClip"))
    readAnimationClip(st.animationClip, *v);
  if (const Value *v = root.get("Sequencer"))
    readSequencer(st.sequencer, *v);

  if (const Value *v = root.get("RecentScenes"); v && v->isArray()) {
    st.recentScenes.clear();
    for (const Value &it : v->asArray()) {
      if (it.isString())
        st.recentScenes.push_back(it.asString());
    }
  }

  st.lastProjectPath = path;
  if (!st.lastScenePath.empty())
    st.pushRecentScene(st.lastScenePath);

  return true;
}

} // namespace Nyx
