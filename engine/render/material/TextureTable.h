#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace Nyx {

class GLResources;

// Owns GL textures for material slots and provides indices for GPU table.
class TextureTable final {
public:
  void init(GLResources &gl);
  void shutdown();

  // Returns texture index in table, or kInvalidTexIndex if load failed.
  uint32_t getOrCreate2D(const std::string &path, bool srgb);

  const std::vector<uint32_t> &glTextures() const { return m_textures; }

  void bindAll(uint32_t firstUnit = 0, uint32_t maxCount = 0) const;

  uint32_t glTexByIndex(uint32_t texIndex) const {
    if (texIndex == Invalid || texIndex >= m_entries.size())
      return 0;
    return m_entries[texIndex].glTex;
  }

  const std::string &pathByIndex(uint32_t texIndex) const {
    static const std::string kEmpty;
    if (texIndex == Invalid || (texIndex >= m_entries.size()))
      return kEmpty;
    return m_entries[texIndex].path;
  }

  bool srgbByIndex(uint32_t texIndex) const {
    if (texIndex == Invalid || (texIndex >= m_entries.size()))
      return false;
    return m_entries[texIndex].srgb;
  }

  bool reloadByIndex(uint32_t texIndex);

  // Process completed async loads on the main thread.
  void processUploads(uint32_t maxPerFrame = 8);

  static constexpr uint32_t Invalid = 0xFFFFFFFF;

private:
  GLResources *m_gl = nullptr;

  struct Entry final {
    std::string path;
    bool srgb = false;
    uint32_t glTex = 0;
    bool loading = false;
    bool failed = false;
  };

  std::vector<Entry> m_entries;
  std::vector<uint32_t> m_textures; // parallel glTex list for fast bind

  struct Key {
    std::string path;
    bool srgb = false;
    bool operator==(const Key &o) const {
      return srgb == o.srgb && path == o.path;
    }
  };

  struct KeyHash {
    std::size_t operator()(const Key &k) const noexcept {
      return std::hash<std::string>{}(k.path) ^ (k.srgb ? 0x9e3779b9 : 0);
    }
  };

  std::unordered_map<Key, uint32_t, KeyHash> m_index;

  struct Job {
    uint32_t index = Invalid;
    std::string path;
    bool srgb = false;
  };

  struct Loaded {
    uint32_t index = Invalid;
    std::string path;
    bool srgb = false;
    int w = 0;
    int h = 0;
    std::vector<uint8_t> rgba;
    bool ok = false;
  };

  std::thread m_worker;
  std::mutex m_mutex;
  std::condition_variable m_cv;
  std::queue<Job> m_jobs;
  std::queue<Loaded> m_ready;
  std::atomic<bool> m_stop{false};

  uint32_t m_placeholderLinear = 0;
  uint32_t m_placeholderSRGB = 0;

  std::filesystem::path m_cacheDir;

  void startWorker();
  void stopWorker();
  void workerLoop();
  void enqueue(uint32_t index, const std::string &path, bool srgb);
  void clearQueues();

  uint32_t createPlaceholder(bool srgb) const;
  uint32_t uploadTexture(const Loaded &t);

  bool loadFromCache(const std::string &path, bool srgb, Loaded &out) const;
  void writeCache(const Loaded &t) const;

  int find(const std::string &path, bool srgb) const;
};

} // namespace Nyx
