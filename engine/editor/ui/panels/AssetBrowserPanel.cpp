#include "AssetBrowserPanel.h"

#include "render/material/TextureTable.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <queue>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

#include <glad/glad.h>
#include <stb_image.h>

namespace fs = std::filesystem;

namespace Nyx {

namespace {
constexpr uint32_t kThumbSize = 64;
constexpr uint32_t kCacheMagic = 0x4E595854; // 'NYXT'

struct CacheHeader {
  uint32_t magic = kCacheMagic;
  uint32_t w = 0;
  uint32_t h = 0;
  uint32_t size = 0;
};

static std::string cacheKey(const std::string &path) {
  std::error_code ec;
  auto ftime = std::filesystem::last_write_time(path, ec);
  std::uint64_t ts =
      ec ? 0u : static_cast<std::uint64_t>(ftime.time_since_epoch().count());
  std::ostringstream ss;
  ss << path << "|" << ts;
  return ss.str();
}

static std::string hashHex(const std::string &s) {
  const std::uint64_t h = std::hash<std::string>{}(s);
  std::ostringstream ss;
  ss << std::hex << h;
  return ss.str();
}

static void downscaleNearest(const uint8_t *src, int sw, int sh,
                             std::vector<uint8_t> &dst, int dw, int dh) {
  dst.resize(dw * dh * 4);
  for (int y = 0; y < dh; ++y) {
    int sy = (y * sh) / dh;
    for (int x = 0; x < dw; ++x) {
      int sx = (x * sw) / dw;
      const uint8_t *sp = src + (sy * sw + sx) * 4;
      uint8_t *dp = dst.data() + (y * dw + x) * 4;
      dp[0] = sp[0];
      dp[1] = sp[1];
      dp[2] = sp[2];
      dp[3] = sp[3];
    }
  }
}

} // namespace

void AssetBrowserPanel::init(TextureTable &texTable) {
  m_tex = &texTable;
  m_cacheDir = std::filesystem::current_path() / ".cache" / "thumbcache";
  std::error_code ec;
  std::filesystem::create_directories(m_cacheDir, ec);
  startWorker();
}

void AssetBrowserPanel::shutdown() {
  stopWorker();
  clearThumbnails();
  m_items.clear();
  m_registry = nullptr;
  m_tex = nullptr;
}

void AssetBrowserPanel::setRegistry(AssetRegistry *registry) {
  if (m_registry == registry)
    return;
  m_registry = registry;
  m_needsRefresh = true;
}

void AssetBrowserPanel::setRoot(std::string rootAbsPath) {
  m_root = std::move(rootAbsPath);
  m_needsRefresh = true;
}

void AssetBrowserPanel::setCurrentFolder(std::string folder) {
  m_currentFolder = std::move(folder);
  if (m_folderItems.find(m_currentFolder) == m_folderItems.end())
    m_currentFolder.clear();
}

void AssetBrowserPanel::setFilter(std::string filter) {
  m_filterStr = std::move(filter);
  std::snprintf(m_filter, sizeof(m_filter), "%s", m_filterStr.c_str());
}

std::string AssetBrowserPanel::toLower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return s;
}

bool AssetBrowserPanel::isAssetExt(const std::string &pathLower) {
  return pathLower.ends_with(".png") || pathLower.ends_with(".jpg") ||
         pathLower.ends_with(".jpeg") || pathLower.ends_with(".tga") ||
         pathLower.ends_with(".bmp") || pathLower.ends_with(".ktx") ||
         pathLower.ends_with(".ktx2") || pathLower.ends_with(".hdr") ||
         pathLower.ends_with(".exr") || pathLower.ends_with(".cube");
}

std::string AssetBrowserPanel::filenameOf(const std::string &absPath) {
  try {
    return fs::path(absPath).filename().string();
  } catch (...) {
    return absPath;
  }
}

std::string AssetBrowserPanel::parentFolder(const std::string &relDir) {
  const size_t pos = relDir.find_last_of("/\\");
  if (pos == std::string::npos)
    return {};
  return relDir.substr(0, pos);
}

void AssetBrowserPanel::scanFolderRecursive(const std::string &rootAbs) {
  m_items.clear();
  m_folders.clear();
  m_folderItems.clear();
  m_folderChildren.clear();

  if (rootAbs.empty())
    return;

  fs::path root(rootAbs);
  if (!fs::exists(root))
    return;

  const uint64_t gen = m_jobGen.load();

  for (auto const &it : fs::recursive_directory_iterator(root)) {
    if (!it.is_regular_file())
      continue;

    const std::string abs = it.path().string();
    const std::string low = toLower(abs);
    if (!isAssetExt(low))
      continue;

    Item item{};
    item.id = hashString64(abs);
    item.absPath = abs;
    item.name = filenameOf(abs);
    item.isTexture = isAssetExt(low) && !low.ends_with(".cube");
    try {
      fs::path rel = fs::relative(it.path(), root);
      item.relPath = rel.generic_string();
      item.relDir = rel.parent_path().generic_string();
    } catch (...) {
      item.relPath = item.name;
      item.relDir.clear();
    }
    item.gen = gen;
    m_items.push_back(std::move(item));
  }

  std::sort(m_items.begin(), m_items.end(),
            [](const Item &a, const Item &b) { return a.relPath < b.relPath; });

  std::unordered_map<std::string, bool> folderSet;
  folderSet.emplace("", true);
  for (size_t i = 0; i < m_items.size(); ++i) {
    const std::string &dir = m_items[i].relDir;
    m_folderItems[dir].push_back(i);
    std::string cur = dir;
    while (true) {
      if (folderSet.emplace(cur, true).second) {
        const std::string parent = parentFolder(cur);
        m_folderChildren[parent].push_back(cur);
      }
      if (cur.empty())
        break;
      cur = parentFolder(cur);
    }
  }

  for (auto &kv : m_folderChildren) {
    std::sort(kv.second.begin(), kv.second.end());
    kv.second.erase(std::unique(kv.second.begin(), kv.second.end()),
                    kv.second.end());
  }
  for (auto &kv : m_folderItems) {
    std::sort(kv.second.begin(), kv.second.end());
  }

  if (m_currentFolder.empty())
    m_currentFolder = "";
}

void AssetBrowserPanel::buildFromRegistry() {
  m_items.clear();
  m_folders.clear();
  m_folderItems.clear();
  m_folderChildren.clear();

  if (!m_registry)
    return;

  m_root = m_registry->projectRootAbs();
  const uint64_t gen = m_jobGen.load();

  for (const auto &asset : m_registry->all()) {
    Item item{};
    item.id = asset.id;
    item.type = asset.type;
    item.relPath = asset.relPath;
    item.relDir = asset.folder;
    item.name = asset.name;
    item.absPath = m_registry->makeAbs(asset.relPath);
    item.isTexture = (asset.type == AssetType::Texture2D);
    item.gen = gen;
    m_items.push_back(std::move(item));
  }

  std::sort(m_items.begin(), m_items.end(),
            [](const Item &a, const Item &b) { return a.relPath < b.relPath; });

  std::unordered_map<std::string, bool> folderSet;
  folderSet.emplace("", true);
  for (size_t i = 0; i < m_items.size(); ++i) {
    const std::string &dir = m_items[i].relDir;
    m_folderItems[dir].push_back(i);
    std::string cur = dir;
    while (true) {
      if (folderSet.emplace(cur, true).second) {
        const std::string parent = parentFolder(cur);
        m_folderChildren[parent].push_back(cur);
      }
      if (cur.empty())
        break;
      cur = parentFolder(cur);
    }
  }

  for (auto &kv : m_folderChildren) {
    std::sort(kv.second.begin(), kv.second.end());
    kv.second.erase(std::unique(kv.second.begin(), kv.second.end()),
                    kv.second.end());
  }
  for (auto &kv : m_folderItems) {
    std::sort(kv.second.begin(), kv.second.end());
  }

  if (m_currentFolder.empty())
    m_currentFolder = m_registry->contentRootRel();
}

void AssetBrowserPanel::refresh() {
  clearThumbnails();
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::queue<ThumbJob> empty;
    std::swap(m_jobs, empty);
    std::queue<LoadedThumb> emptyReady;
    std::swap(m_ready, emptyReady);
  }
  m_jobGen.fetch_add(1);
  if (m_registry) {
    buildFromRegistry();
  } else {
    scanFolderRecursive(m_root);
  }
  if (m_folderItems.find(m_currentFolder) == m_folderItems.end())
    m_currentFolder = m_registry ? m_registry->contentRootRel() : "";
  m_needsRefresh = false;
}

void AssetBrowserPanel::startWorker() {
  if (!m_workers.empty())
    return;
  m_workerStop = false;
  const size_t hw = std::max(1u, std::thread::hardware_concurrency());
  m_workerCount = std::min<size_t>(4, hw);
  m_workers.reserve(m_workerCount);
  for (size_t i = 0; i < m_workerCount; ++i) {
    m_workers.emplace_back([this] { workerLoop(); });
  }
}

void AssetBrowserPanel::stopWorker() {
  if (m_workers.empty())
    return;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_workerStop = true;
  }
  m_cv.notify_all();
  for (auto &t : m_workers) {
    if (t.joinable())
      t.join();
  }
  m_workers.clear();
}

void AssetBrowserPanel::workerLoop() {
  for (;;) {
    ThumbJob job{};
    {
      std::unique_lock<std::mutex> lock(m_mutex);
      m_cv.wait(lock, [&] { return m_workerStop || !m_jobs.empty(); });
      if (m_workerStop)
        return;
      job = std::move(m_jobs.front());
      m_jobs.pop();
    }

    LoadedThumb t{};
    t.index = job.index;
    t.path = job.path;
    t.gen = job.gen;

    if (!m_cacheDir.empty()) {
      const std::string key = hashHex(cacheKey(job.path));
      const auto cachePath = m_cacheDir / (key + ".bin");
      std::ifstream f(cachePath, std::ios::binary);
      if (f.is_open()) {
        CacheHeader h{};
        f.read(reinterpret_cast<char *>(&h), sizeof(h));
        if (f && h.magic == kCacheMagic && h.w == kThumbSize &&
            h.h == kThumbSize && h.size == kThumbSize * kThumbSize * 4) {
          t.w = static_cast<int>(h.w);
          t.h = static_cast<int>(h.h);
          t.rgba.resize(h.size);
          f.read(reinterpret_cast<char *>(t.rgba.data()), h.size);
          if (f) {
            {
              std::lock_guard<std::mutex> lock(m_mutex);
              m_ready.push(std::move(t));
            }
            continue;
          }
        }
      }
    }

    int w = 0, h = 0, c = 0;
    stbi_uc *data = stbi_load(job.path.c_str(), &w, &h, &c, STBI_rgb_alpha);
    if (!data) {
      t.w = 0;
      t.h = 0;
      {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_ready.push(std::move(t));
      }
      continue;
    }

    t.w = kThumbSize;
    t.h = kThumbSize;
    downscaleNearest(data, w, h, t.rgba, kThumbSize, kThumbSize);
    stbi_image_free(data);

    if (!m_cacheDir.empty()) {
      const std::string key = hashHex(cacheKey(job.path));
      const auto cachePath = m_cacheDir / (key + ".bin");
      std::ofstream f(cachePath, std::ios::binary | std::ios::trunc);
      if (f.is_open()) {
        CacheHeader h{};
        h.w = kThumbSize;
        h.h = kThumbSize;
        h.size = kThumbSize * kThumbSize * 4;
        f.write(reinterpret_cast<const char *>(&h), sizeof(h));
        f.write(reinterpret_cast<const char *>(t.rgba.data()), h.size);
      }
    }

    {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_ready.push(std::move(t));
    }
  }
}

void AssetBrowserPanel::enqueueThumb(size_t index, const std::string &path) {
  ThumbJob job{};
  job.index = index;
  job.path = path;
  job.gen = m_jobGen.load();
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_jobs.push(std::move(job));
  }
  m_cv.notify_one();
}

uint32_t AssetBrowserPanel::createThumbTexture(const LoadedThumb &t) const {
  uint32_t tex = 0;
  if (t.w <= 0 || t.h <= 0 || t.rgba.empty())
    return 0;
  glCreateTextures(GL_TEXTURE_2D, 1, &tex);
  glTextureStorage2D(tex, 1, GL_RGBA8, t.w, t.h);
  glTextureSubImage2D(tex, 0, 0, 0, t.w, t.h, GL_RGBA, GL_UNSIGNED_BYTE,
                      t.rgba.data());
  glTextureParameteri(tex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTextureParameteri(tex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTextureParameteri(tex, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTextureParameteri(tex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  return tex;
}

void AssetBrowserPanel::processReadyThumbs() {
  for (;;) {
    LoadedThumb t{};
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      if (m_ready.empty())
        return;
      t = std::move(m_ready.front());
      m_ready.pop();
    }

    if (t.gen != m_jobGen.load())
      continue;

    if (t.index >= m_items.size())
      continue;
    Item &it = m_items[t.index];
    if (it.absPath != t.path || it.gen != t.gen)
      continue;

    if (t.w <= 0 || t.h <= 0) {
      it.thumbFailed = true;
      continue;
    }

    it.glThumb = createThumbTexture(t);
    if (it.glThumb == 0) {
      it.thumbFailed = true;
      continue;
    }
  }
}

void AssetBrowserPanel::ensureThumbnail(Item &it, size_t index) {
  if (!it.isTexture || it.glThumb != 0 || it.thumbFailed || it.thumbRequested)
    return;
  it.thumbRequested = true;
  enqueueThumb(index, it.absPath);
}

void AssetBrowserPanel::clearThumbnails() {
  for (auto &it : m_items) {
    if (it.glThumb != 0) {
      glDeleteTextures(1, &it.glThumb);
      it.glThumb = 0;
    }
    it.thumbFailed = false;
    it.thumbRequested = false;
  }
}

} // namespace Nyx
