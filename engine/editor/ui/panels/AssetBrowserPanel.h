#pragma once

#include "assets/AssetId.h"
#include "assets/AssetRegistry.h"
#include "assets/AssetType.h"
#include "editor/tools/IconAtlas.h"
#include <cstdint>
#include <string>
#include <vector>
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>

namespace Nyx {

class TextureTable;

class AssetBrowserPanel final {
public:
  void init(TextureTable &texTable);
  void shutdown();
  void setRegistry(AssetRegistry *registry);
  const AssetRegistry *registry() const { return m_registry; }

  void setRoot(std::string rootAbsPath);
  const std::string &root() const { return m_root; }
  void setCurrentFolder(std::string folder);
  const std::string &currentFolder() const { return m_currentFolder; }
  void setFilter(std::string filter);
  const std::string &filter() const { return m_filterStr; }

  void draw(bool *pOpen);

  // Refresh file listing (manual call or when root changes)
  void refresh();

private:
  struct Item final {
    AssetId id = 0;
    AssetType type = AssetType::Unknown;
    std::string absPath;
    std::string relPath;
    std::string relDir;
    std::string name;
    bool isTexture = false;
    uint32_t previewTexIndexLinear = 0xFFFFFFFF; // for thumbnail (linear)
    uint32_t glThumb = 0;
    bool thumbFailed = false;
    bool thumbRequested = false;
    uint64_t gen = 0;
  };

  TextureTable *m_tex = nullptr;
  AssetRegistry *m_registry = nullptr;
  std::string m_root;
  std::vector<Item> m_items;
  std::string m_currentFolder;
  std::string m_lastFolder;
  std::string m_filterStr;
  char m_filter[128]{};
  bool m_showAll = false;
  std::vector<std::string> m_folders;
  std::unordered_map<std::string, std::vector<size_t>> m_folderItems;
  std::unordered_map<std::string, std::vector<std::string>> m_folderChildren;

  bool m_needsRefresh = true;
  bool m_showUnknown = false;

  void scanFolderRecursive(const std::string &rootAbs);
  void buildFromRegistry();
  static bool isAssetExt(const std::string &pathLower);
  static std::string filenameOf(const std::string &absPath);
  static std::string toLower(std::string s);
  static std::string parentFolder(const std::string &relDir);

  struct ThumbJob {
    size_t index = 0;
    std::string path;
    uint64_t gen = 0;
  };

  struct LoadedThumb {
    size_t index = 0;
    std::string path;
    uint64_t gen = 0;
    int w = 0;
    int h = 0;
    std::vector<uint8_t> rgba;
  };

  std::thread m_worker;
  std::vector<std::thread> m_workers;
  size_t m_workerCount = 0;
  std::mutex m_mutex;
  std::condition_variable m_cv;
  std::queue<ThumbJob> m_jobs;
  std::queue<LoadedThumb> m_ready;
  std::atomic<bool> m_workerStop{false};
  std::atomic<uint64_t> m_jobGen{1};

  std::filesystem::path m_cacheDir;

  IconAtlas m_iconAtlas{};
  bool m_iconInit = false;
  bool m_iconReady = false;

  void startWorker();
  void stopWorker();
  void workerLoop();

  void enqueueThumb(size_t index, const std::string &path);
  void processReadyThumbs();
  uint32_t createThumbTexture(const LoadedThumb &t) const;

  void ensureThumbnail(Item &it, size_t index);
  void clearThumbnails();
  void ensureIconAtlas();
  void drawHeader();
  void collectVisibleEntries(const std::string &filterLower,
                             std::vector<std::string> &folders,
                             std::vector<size_t> &indices) const;
  void drawFolderEntries(const std::vector<std::string> &folders,
                         float thumb);
  void drawAssetEntries(const std::vector<size_t> &indices, float thumb);
};

} // namespace Nyx
