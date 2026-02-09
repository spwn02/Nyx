#include "AssetRegistry.h"
#include "project/NyxProjectRuntime.h"

#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

namespace Nyx {

static bool startsWith(const std::string &a, const std::string &b) {
  return a.size() >= b.size() && a.compare(0, b.size(), b) == 0;
}

void AssetRegistry::init(NyxProjectRuntime &project) {
  m_project = &project;
  m_rootAbs = project.rootAbs();
  m_contentAbs = project.makeAbsolute(m_contentRel);
  rescan();
}

void AssetRegistry::shutdown() {
  m_assets.clear();
  m_idToIndex.clear();
  m_relToIndex.clear();
  m_project = nullptr;
  m_rootAbs.clear();
  m_contentAbs.clear();
}

std::string AssetRegistry::normalizeSlashes(std::string s) {
  for (char &c : s) {
    if (c == '\\')
      c = '/';
  }
  // collapse "//"
  while (true) {
    const size_t p = s.find("//");
    if (p == std::string::npos)
      break;
    s.erase(p, 1);
  }
  return s;
}

std::string AssetRegistry::lower(std::string s) {
  for (char &c : s) {
    if (c >= 'A' && c <= 'Z')
      c = static_cast<char>(c - 'A' + 'a');
  }
  return s;
}

AssetType AssetRegistry::classifyByExtension(const std::string &extLower) {
  // extLower includes the dot: ".png"
  if (extLower == ".png" || extLower == ".jpg" || extLower == ".jpeg" ||
      extLower == ".tga" || extLower == ".bmp" || extLower == ".ktx" ||
      extLower == ".ktx2" || extLower == ".hdr" || extLower == ".exr") {
    return AssetType::Texture2D;
  }

  if (extLower == ".gltf" || extLower == ".glb" || extLower == ".obj" ||
      extLower == ".fbx") {
    return AssetType::Mesh;
  }

  if (extLower == ".nyxscene")
    return AssetType::Scene;
  if (extLower == ".nyxproj")
    return AssetType::Project;
  if (extLower == ".nasset")
    return AssetType::NyxAsset;

  return AssetType::Unknown;
}

std::string AssetRegistry::parentFolder(const std::string &relPath) {
  auto s = normalizeSlashes(relPath);
  auto pos = s.find_last_of('/');
  if (pos == std::string::npos)
    return "";
  return s.substr(0, pos);
}

std::string AssetRegistry::fileName(const std::string &relPath) {
  auto s = normalizeSlashes(relPath);
  auto pos = s.find_last_of('/');
  if (pos == std::string::npos)
    return s;
  return s.substr(pos + 1);
}

void AssetRegistry::addRecord(const std::string &relPath, AssetType type) {
  AssetRecord r{};
  r.type = type;
  r.relPath = normalizeSlashes(relPath);
  r.folder = parentFolder(r.relPath);
  r.name = fileName(r.relPath);

  // Make ID stable & project-portable: hash of rel path.
  r.id = hashString64(r.relPath);

  const uint32_t idx = static_cast<uint32_t>(m_assets.size());
  m_assets.push_back(r);
  m_idToIndex[r.id] = idx;
  m_relToIndex[r.relPath] = idx;
}

void AssetRegistry::rescan() {
  m_assets.clear();
  m_idToIndex.clear();
  m_relToIndex.clear();

  if (!m_project)
    return;

  // Ensure content dir exists (if not, it's still a valid empty project).
  if (!fs::exists(m_contentAbs)) {
    return;
  }

  fs::path root(m_contentAbs);

  for (auto it = fs::recursive_directory_iterator(root);
       it != fs::recursive_directory_iterator(); ++it) {
    const fs::directory_entry &e = *it;

    if (e.is_directory()) {
      // skip hidden dirs like ".git"
      const std::string name = e.path().filename().string();
      if (!name.empty() && name[0] == '.') {
        it.disable_recursion_pending();
      }
      continue;
    }

    if (!e.is_regular_file())
      continue;

    const fs::path p = e.path();

    // Relative to project root: Content/...
    std::string abs = normalizeSlashes(p.string());
    std::string rel = makeRelFromAbs(abs);
    rel = normalizeSlashes(rel);

    // We only index inside Content by default
    if (!startsWith(rel, normalizeSlashes(m_contentRel)))
      continue;

    std::string ext = lower(p.extension().string());
    AssetType type = classifyByExtension(ext);

    // Keep unknowns too (useful for browsing), but you can choose to hide in
    // UI.
    addRecord(rel, type);
  }

  // Stable order: folder, then name
  std::sort(m_assets.begin(), m_assets.end(),
            [](const AssetRecord &a, const AssetRecord &b) {
              if (a.folder != b.folder)
                return a.folder < b.folder;
              return a.name < b.name;
            });

  // Rebuild indices after sort
  m_idToIndex.clear();
  m_relToIndex.clear();
  for (uint32_t i = 0; i < static_cast<uint32_t>(m_assets.size()); ++i) {
    m_idToIndex[m_assets[i].id] = i;
    m_relToIndex[m_assets[i].relPath] = i;
  }
}

const AssetRecord *AssetRegistry::findById(AssetId id) const {
  auto it = m_idToIndex.find(id);
  if (it == m_idToIndex.end())
    return nullptr;
  return &m_assets[it->second];
}

const AssetRecord *AssetRegistry::findByRelPath(const std::string &rel) const {
  const auto key = normalizeSlashes(rel);
  auto it = m_relToIndex.find(key);
  if (it == m_relToIndex.end())
    return nullptr;
  return &m_assets[it->second];
}

std::string AssetRegistry::makeAbs(const std::string &rel) const {
  if (!m_project)
    return rel;
  return m_project->makeAbsolute(rel);
}

std::string AssetRegistry::makeRelFromAbs(const std::string &abs) const {
  if (!m_project)
    return abs;
  return m_project->makeRelative(abs);
}

} // namespace Nyx
