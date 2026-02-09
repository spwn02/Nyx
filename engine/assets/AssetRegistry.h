#pragma once

#include "AssetRecord.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Nyx {

class NyxProjectRuntime;

// Scans project folders and builds an index of assets for UI and drag/drop.
// Phase 3.1.7: no cooking, no metadata DB, just scan filesystem.
class AssetRegistry final {
public:
  void init(NyxProjectRuntime &project);
  void shutdown();

  // Re-scan content folder. Called on project open, or via UI "Rescan".
  void rescan();

  // Query
  const std::vector<AssetRecord> &all() const { return m_assets; }
  const AssetRecord *findById(AssetId id) const;
  const AssetRecord *findByRelPath(const std::string &rel) const;

  // Paths
  const std::string &projectRootAbs() const { return m_rootAbs; }
  const std::string &contentRootRel() const { return m_contentRel; }
  NyxProjectRuntime *projectRuntime() const { return m_project; }
  std::string makeAbs(const std::string &rel) const;
  std::string makeRelFromAbs(const std::string &abs) const;

private:
  NyxProjectRuntime *m_project = nullptr;

  std::string m_rootAbs;
  std::string m_contentRel = "Content";
  std::string m_contentAbs;

  std::vector<AssetRecord> m_assets;
  std::unordered_map<AssetId, uint32_t> m_idToIndex;
  std::unordered_map<std::string, uint32_t> m_relToIndex;

  static AssetType classifyByExtension(const std::string &extLower);
  static std::string lower(std::string s);
  static std::string normalizeSlashes(std::string s);
  static std::string parentFolder(const std::string &relPath);
  static std::string fileName(const std::string &relPath);

  void addRecord(const std::string &relPath, AssetType type);
};

} // namespace Nyx
