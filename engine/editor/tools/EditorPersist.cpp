#include "EditorPersist.h"
#include <filesystem>
#include <fstream>
#include <sstream>

namespace Nyx {

static void put(std::ostringstream &o, const char *k, bool v) {
  o << k << "=" << (v ? "1" : "0") << "\n";
}
static void put(std::ostringstream &o, const char *k, int v) {
  o << k << "=" << v << "\n";
}
static void put(std::ostringstream &o, const char *k, float v) {
  o << k << "=" << v << "\n";
}
static void put(std::ostringstream &o, const char *k, const std::string &v) {
  o << k << "=" << v << "\n";
}

std::expected<void, std::string>
EditorPersist::save(const std::string &path, const EditorPersistState &s) {
  try {
    std::filesystem::path p = std::filesystem::absolute(path);
    std::filesystem::create_directories(p.parent_path());

    std::ostringstream o;

    // Camera
    put(o, "cam.pos.x", s.camera.position.x);
    put(o, "cam.pos.y", s.camera.position.y);
    put(o, "cam.pos.z", s.camera.position.z);
    put(o, "cam.yaw", s.camera.yawDeg);
    put(o, "cam.pitch", s.camera.pitchDeg);
    put(o, "cam.fov", s.camera.fovYDeg);
    put(o, "cam.near", s.camera.nearZ);
    put(o, "cam.far", s.camera.farZ);
    put(o, "cam.speed", s.camera.speed);
    put(o, "cam.boost", s.camera.boostMul);
    put(o, "cam.sens", s.camera.sensitivity);

    // Gizmo
    put(o, "gizmo.op", (int)s.gizmoOp);
    put(o, "gizmo.mode", (int)s.gizmoMode);
    put(o, "gizmo.useSnap", s.gizmoUseSnap);
    put(o, "gizmo.snapTranslate", s.gizmoSnapTranslate);
    put(o, "gizmo.snapRotateDeg", s.gizmoSnapRotateDeg);
    put(o, "gizmo.snapScale", s.gizmoSnapScale);

    // Panels
    put(o, "panel.viewport", s.panels.viewport);
    put(o, "panel.hierarchy", s.panels.hierarchy);
    put(o, "panel.inspector", s.panels.inspector);
    put(o, "panel.sky", s.panels.sky);
    put(o, "panel.assetBrowser", s.panels.assetBrowser);
    put(o, "panel.stats", s.panels.stats);
    put(o, "panel.renderSettings", s.panels.renderSettings);
    put(o, "panel.projectSettings", s.panels.projectSettings);
    put(o, "panel.lutManager", s.panels.lutManager);
    put(o, "panel.materialGraph", s.panels.materialGraph);
    put(o, "panel.postGraph", s.panels.postGraph);
    put(o, "panel.sequencer", s.panels.sequencer);
    put(o, "panel.history", s.panels.history);

    // Asset browser UI state
    put(o, "assetBrowser.folder", s.assetBrowserFolder);
    put(o, "assetBrowser.filter", s.assetBrowserFilter);

    put(o, "dock.layoutVersion", s.dockLayoutVersion);

    // PostGraph filters
    put(o, "postgraph.count", (int)s.postGraphFilters.size());
    for (size_t i = 0; i < s.postGraphFilters.size(); ++i) {
      const auto &n = s.postGraphFilters[i];
      const std::string base = "postgraph.node." + std::to_string(i);
      put(o, (base + ".type").c_str(), (int)n.typeId);
      put(o, (base + ".enabled").c_str(), n.enabled);
      put(o, (base + ".label").c_str(), n.label);
      put(o, (base + ".lutPath").c_str(), n.lutPath);
      put(o, (base + ".paramCount").c_str(), (int)n.params.size());
      for (size_t p = 0; p < n.params.size(); ++p) {
        put(o, (base + ".param." + std::to_string(p)).c_str(), n.params[p]);
      }
    }

    std::ofstream f(p, std::ios::binary);
    if (!f.is_open())
      return std::unexpected(std::string("Failed to open for write: ") +
                             p.string());
    const std::string txt = o.str();
    f.write(txt.data(), (std::streamsize)txt.size());
    return {};
  } catch (const std::exception &e) {
    return std::unexpected(std::string("save exception: ") + e.what());
  }
}

std::expected<void, std::string> EditorPersist::load(const std::string &path,
                                                     EditorPersistState &out) {
  try {
    std::filesystem::path p = std::filesystem::absolute(path);
    std::ifstream f(p, std::ios::binary);
    if (!f.is_open()) {
      // Not an error; first run.
      return {};
    }

    std::stringstream buf;
    buf << f.rdbuf();

    auto kv = parseKV(buf.str());

    auto get = [&](const char *k) -> std::string {
      auto it = kv.find(k);
      if (it == kv.end())
        return {};
      return it->second;
    };

    out.camera.position.x = toFloat(get("cam.pos.x"), out.camera.position.x);
    out.camera.position.y = toFloat(get("cam.pos.y"), out.camera.position.y);
    out.camera.position.z = toFloat(get("cam.pos.z"), out.camera.position.z);
    out.camera.yawDeg = toFloat(get("cam.yaw"), out.camera.yawDeg);
    out.camera.pitchDeg = toFloat(get("cam.pitch"), out.camera.pitchDeg);
    out.camera.fovYDeg = toFloat(get("cam.fov"), out.camera.fovYDeg);
    out.camera.nearZ = toFloat(get("cam.near"), out.camera.nearZ);
    out.camera.farZ = toFloat(get("cam.far"), out.camera.farZ);
    out.camera.speed = toFloat(get("cam.speed"), out.camera.speed);
    out.camera.boostMul = toFloat(get("cam.boost"), out.camera.boostMul);
    out.camera.sensitivity = toFloat(get("cam.sens"), out.camera.sensitivity);

    out.gizmoOp = (GizmoOp)toInt(get("gizmo.op"), (int)out.gizmoOp);
    out.gizmoMode = (GizmoMode)toInt(get("gizmo.mode"), (int)out.gizmoMode);
    out.gizmoUseSnap = toBool(get("gizmo.useSnap"), out.gizmoUseSnap);
    out.gizmoSnapTranslate =
        toFloat(get("gizmo.snapTranslate"), out.gizmoSnapTranslate);
    out.gizmoSnapRotateDeg =
        toFloat(get("gizmo.snapRotateDeg"), out.gizmoSnapRotateDeg);
    out.gizmoSnapScale =
        toFloat(get("gizmo.snapScale"), out.gizmoSnapScale);

    out.panels.viewport = toBool(get("panel.viewport"), out.panels.viewport);
    out.panels.hierarchy = toBool(get("panel.hierarchy"), out.panels.hierarchy);
    out.panels.inspector = toBool(get("panel.inspector"), out.panels.inspector);
    out.panels.sky = toBool(get("panel.sky"), out.panels.sky);
    out.panels.assetBrowser =
        toBool(get("panel.assetBrowser"), out.panels.assetBrowser);
    out.panels.stats = toBool(get("panel.stats"), out.panels.stats);
    out.panels.renderSettings =
        toBool(get("panel.renderSettings"), out.panels.renderSettings);
    out.panels.projectSettings =
        toBool(get("panel.projectSettings"), out.panels.projectSettings);
    out.panels.lutManager =
        toBool(get("panel.lutManager"), out.panels.lutManager);
    out.panels.materialGraph =
        toBool(get("panel.materialGraph"), out.panels.materialGraph);
    out.panels.postGraph =
        toBool(get("panel.postGraph"), out.panels.postGraph);
    out.panels.sequencer =
        toBool(get("panel.sequencer"), out.panels.sequencer);
    out.panels.history =
        toBool(get("panel.history"), out.panels.history);

    out.assetBrowserFolder = get("assetBrowser.folder");
    out.assetBrowserFilter = get("assetBrowser.filter");

    out.dockLayoutVersion = toInt(get("dock.layoutVersion"), out.dockLayoutVersion);

    // PostGraph filters
    const int pgCount = toInt(get("postgraph.count"), 0);
    out.postGraphFilters.clear();
    if (pgCount > 0) {
      out.postGraphFilters.reserve((size_t)pgCount);
      for (int i = 0; i < pgCount; ++i) {
        const std::string base = "postgraph.node." + std::to_string(i);
        EditorPersistState::PostGraphPersistNode n{};
        n.typeId = (uint32_t)toInt(get((base + ".type").c_str()), 0);
        n.enabled = toBool(get((base + ".enabled").c_str()), true);
        n.label = get((base + ".label").c_str());
        n.lutPath = get((base + ".lutPath").c_str());
        const int pc = toInt(get((base + ".paramCount").c_str()), 0);
        if (pc > 0) {
          n.params.reserve((size_t)pc);
          for (int p = 0; p < pc; ++p) {
            n.params.push_back(
                toFloat(get((base + ".param." + std::to_string(p)).c_str()),
                        0.0f));
          }
        }
        out.postGraphFilters.push_back(std::move(n));
      }
    }

    return {};
  } catch (const std::exception &e) {
    return std::unexpected(std::string("load exception: ") + e.what());
  }
}

std::string EditorPersist::trim(std::string v) {
  auto isSpace = [](unsigned char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
  };
  while (!v.empty() && isSpace((unsigned char)v.front()))
    v.erase(v.begin());
  while (!v.empty() && isSpace((unsigned char)v.back()))
    v.pop_back();
  return v;
}

std::unordered_map<std::string, std::string>
EditorPersist::parseKV(const std::string &text) {
  std::unordered_map<std::string, std::string> m;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    line = trim(line);
    if (line.empty())
      continue;
    if (line[0] == '#')
      continue;
    const auto eq = line.find('=');
    if (eq == std::string::npos)
      continue;
    std::string k = trim(line.substr(0, eq));
    std::string v = trim(line.substr(eq + 1));
    if (!k.empty())
      m.emplace(std::move(k), std::move(v));
  }
  return m;
}

bool EditorPersist::toBool(const std::string &v, bool def) {
  if (v.empty())
    return def;
  if (v == "1" || v == "true" || v == "True" || v == "TRUE")
    return true;
  if (v == "0" || v == "false" || v == "False" || v == "FALSE")
    return false;
  return def;
}

int EditorPersist::toInt(const std::string &v, int def) {
  if (v.empty())
    return def;
  try {
    return std::stoi(v);
  } catch (...) {
    return def;
  }
}

float EditorPersist::toFloat(const std::string &v, float def) {
  if (v.empty())
    return def;
  try {
    return std::stof(v);
  } catch (...) {
    return def;
  }
}

} // namespace Nyx
