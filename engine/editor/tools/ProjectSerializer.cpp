#include "ProjectSerializer.h"

#include "scene/JsonLite.h"

#include <fstream>
#include <sstream>

namespace Nyx {

using namespace JsonLite;

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
  x = v.get("ViewMode");
  if (x && x->isNum())
    o.viewMode = (ViewMode)(int)x->asNum(0.0);
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
