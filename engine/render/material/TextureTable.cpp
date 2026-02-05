#include "render/material/TextureTable.h"
#include "core/Log.h"
#include "render/gl/GLResources.h"

#include <algorithm>
#include <fstream>
#include <sstream>

#include <glad/glad.h>
#include <stb_image.h>

namespace Nyx {

namespace {
constexpr uint32_t kCacheMagic = 0x4E595854; // 'NYXT'

struct CacheHeader {
  uint32_t magic = kCacheMagic;
  uint32_t w = 0;
  uint32_t h = 0;
  uint32_t size = 0;
};

static std::string cacheKey(const std::string &path, bool srgb) {
  std::error_code ec;
  auto ftime = std::filesystem::last_write_time(path, ec);
  std::uint64_t ts = ec ? 0u : static_cast<std::uint64_t>(ftime.time_since_epoch().count());
  std::ostringstream ss;
  ss << path << "|" << ts << "|" << (srgb ? 1 : 0);
  return ss.str();
}

static std::string hashHex(const std::string &s) {
  const std::uint64_t h = std::hash<std::string>{}(s);
  std::ostringstream ss;
  ss << std::hex << h;
  return ss.str();
}
} // namespace

void TextureTable::init(GLResources &gl) {
  m_gl = &gl;
  m_entries.clear();
  m_textures.clear();
  m_index.clear();

  m_placeholderLinear = createPlaceholder(false);
  m_placeholderSRGB = createPlaceholder(true);

  m_cacheDir = std::filesystem::current_path() / ".nyx" / "texcache";
  std::error_code ec;
  std::filesystem::create_directories(m_cacheDir, ec);

  startWorker();
}

void TextureTable::shutdown() {
  stopWorker();
  clearQueues();

  for (const Entry &entry : m_entries) {
    if (entry.glTex != 0 && entry.glTex != m_placeholderLinear &&
        entry.glTex != m_placeholderSRGB) {
      glDeleteTextures(1, &entry.glTex);
    }
  }
  if (m_placeholderLinear) {
    glDeleteTextures(1, &m_placeholderLinear);
    m_placeholderLinear = 0;
  }
  if (m_placeholderSRGB) {
    glDeleteTextures(1, &m_placeholderSRGB);
    m_placeholderSRGB = 0;
  }

  m_entries.clear();
  m_textures.clear();
  m_index.clear();
  m_gl = nullptr;
}

int TextureTable::find(const std::string &path, bool srgb) const {
  Key k{path, srgb};
  auto it = m_index.find(k);
  if (it == m_index.end())
    return -1;
  return static_cast<int>(it->second);
}

uint32_t TextureTable::getOrCreate2D(const std::string &path, bool srgb) {
  if (!m_gl || path.empty())
    return Invalid;

  if (int idx = find(path, srgb); idx >= 0)
    return static_cast<uint32_t>(idx);

  Entry e{};
  e.path = path;
  e.srgb = srgb;
  e.glTex = srgb ? m_placeholderSRGB : m_placeholderLinear;
  e.loading = true;
  e.failed = false;

  const uint32_t idx = static_cast<uint32_t>(m_entries.size());
  m_entries.push_back(std::move(e));
  m_textures.push_back(m_entries.back().glTex);
  m_index[Key{path, srgb}] = idx;

  enqueue(idx, path, srgb);

  return idx;
}

void TextureTable::bindAll(uint32_t firstUnit, uint32_t maxCount) const {
  size_t count = m_textures.size();
  if (maxCount > 0 && count > maxCount)
    count = maxCount;
  for (size_t i = 0; i < count; ++i) {
    glBindTextureUnit(firstUnit + static_cast<uint32_t>(i), m_textures[i]);
  }
}

bool TextureTable::reloadByIndex(uint32_t texIndex) {
  if (texIndex == Invalid || texIndex >= m_entries.size() || !m_gl)
    return false;

  Entry &e = m_entries[texIndex];
  if (e.loading)
    return false;

  if (e.glTex != 0 && e.glTex != m_placeholderLinear &&
      e.glTex != m_placeholderSRGB) {
    glDeleteTextures(1, &e.glTex);
  }
  e.glTex = e.srgb ? m_placeholderSRGB : m_placeholderLinear;
  e.loading = true;
  e.failed = false;
  if (texIndex < m_textures.size())
    m_textures[texIndex] = e.glTex;

  enqueue(texIndex, e.path, e.srgb);
  return true;
}

void TextureTable::processUploads(uint32_t maxPerFrame) {
  uint32_t budget = maxPerFrame;
  while (budget > 0) {
    Loaded t{};
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      if (m_ready.empty())
        return;
      t = std::move(m_ready.front());
      m_ready.pop();
    }

    if (t.index == Invalid || t.index >= m_entries.size())
      continue;

    Entry &e = m_entries[t.index];
    if (e.path != t.path || e.srgb != t.srgb)
      continue;

    if (!t.ok) {
      e.failed = true;
      e.loading = false;
      continue;
    }

    uint32_t newTex = uploadTexture(t);
    if (newTex == 0) {
      e.failed = true;
      e.loading = false;
      continue;
    }

    if (e.glTex != 0 && e.glTex != m_placeholderLinear &&
        e.glTex != m_placeholderSRGB) {
      glDeleteTextures(1, &e.glTex);
    }
    e.glTex = newTex;
    e.loading = false;
    if (t.index < m_textures.size())
      m_textures[t.index] = newTex;

    budget -= 1;
  }
}

void TextureTable::startWorker() {
  if (m_worker.joinable())
    return;
  m_stop = false;
  m_worker = std::thread([this] { workerLoop(); });
}

void TextureTable::stopWorker() {
  if (!m_worker.joinable())
    return;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_stop = true;
  }
  m_cv.notify_all();
  m_worker.join();
}

void TextureTable::clearQueues() {
  std::lock_guard<std::mutex> lock(m_mutex);
  std::queue<Job> emptyJobs;
  std::swap(m_jobs, emptyJobs);
  std::queue<Loaded> emptyReady;
  std::swap(m_ready, emptyReady);
}

void TextureTable::enqueue(uint32_t index, const std::string &path, bool srgb) {
  Job j{};
  j.index = index;
  j.path = path;
  j.srgb = srgb;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_jobs.push(std::move(j));
  }
  m_cv.notify_one();
}

uint32_t TextureTable::createPlaceholder(bool srgb) const {
  uint32_t tex = 0;
  glCreateTextures(GL_TEXTURE_2D, 1, &tex);
  glTextureStorage2D(tex, 1, srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, 1, 1);
  const uint32_t white = 0xFFFFFFFFu;
  glTextureSubImage2D(tex, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, &white);
  glTextureParameteri(tex, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTextureParameteri(tex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTextureParameteri(tex, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTextureParameteri(tex, GL_TEXTURE_WRAP_T, GL_REPEAT);
  return tex;
}

uint32_t TextureTable::uploadTexture(const Loaded &t) {
  if (t.w <= 0 || t.h <= 0 || t.rgba.empty())
    return 0;
  uint32_t glTex = 0;
  glCreateTextures(GL_TEXTURE_2D, 1, &glTex);
  glTextureStorage2D(glTex, 1, t.srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, t.w, t.h);
  glTextureSubImage2D(glTex, 0, 0, 0, t.w, t.h, GL_RGBA, GL_UNSIGNED_BYTE,
                      t.rgba.data());
  glGenerateTextureMipmap(glTex);
  glTextureParameteri(glTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTextureParameteri(glTex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTextureParameteri(glTex, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTextureParameteri(glTex, GL_TEXTURE_WRAP_T, GL_REPEAT);
  return glTex;
}

bool TextureTable::loadFromCache(const std::string &path, bool srgb,
                                 Loaded &out) const {
  if (m_cacheDir.empty())
    return false;
  const std::string key = hashHex(cacheKey(path, srgb));
  const auto cachePath = m_cacheDir / (key + ".bin");

  std::ifstream f(cachePath, std::ios::binary);
  if (!f.is_open())
    return false;

  CacheHeader h{};
  f.read(reinterpret_cast<char *>(&h), sizeof(h));
  if (!f || h.magic != kCacheMagic || h.w == 0 || h.h == 0 || h.size == 0)
    return false;
  if (h.size != h.w * h.h * 4)
    return false;

  out.rgba.resize(h.size);
  f.read(reinterpret_cast<char *>(out.rgba.data()), h.size);
  if (!f)
    return false;

  out.w = static_cast<int>(h.w);
  out.h = static_cast<int>(h.h);
  out.ok = true;
  return true;
}

void TextureTable::writeCache(const Loaded &t) const {
  if (m_cacheDir.empty() || !t.ok || t.w <= 0 || t.h <= 0)
    return;

  const std::string key = hashHex(cacheKey(t.path, t.srgb));
  const auto cachePath = m_cacheDir / (key + ".bin");

  std::ofstream f(cachePath, std::ios::binary | std::ios::trunc);
  if (!f.is_open())
    return;

  CacheHeader h{};
  h.w = static_cast<uint32_t>(t.w);
  h.h = static_cast<uint32_t>(t.h);
  h.size = static_cast<uint32_t>(t.rgba.size());

  f.write(reinterpret_cast<const char *>(&h), sizeof(h));
  if (h.size)
    f.write(reinterpret_cast<const char *>(t.rgba.data()), h.size);
}

void TextureTable::workerLoop() {
  for (;;) {
    Job job{};
    {
      std::unique_lock<std::mutex> lock(m_mutex);
      m_cv.wait(lock, [&] { return m_stop || !m_jobs.empty(); });
      if (m_stop)
        return;
      job = std::move(m_jobs.front());
      m_jobs.pop();
    }

    Loaded t{};
    t.index = job.index;
    t.path = job.path;
    t.srgb = job.srgb;

    if (!loadFromCache(job.path, job.srgb, t)) {
      int w = 0, h = 0, c = 0;
      stbi_uc *data = stbi_load(job.path.c_str(), &w, &h, &c, STBI_rgb_alpha);
      if (!data) {
        t.ok = false;
      } else {
        t.w = w;
        t.h = h;
        t.rgba.assign(data, data + (w * h * 4));
        stbi_image_free(data);
        t.ok = true;
        writeCache(t);
      }
    }

    {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_ready.push(std::move(t));
    }
  }
}

} // namespace Nyx
