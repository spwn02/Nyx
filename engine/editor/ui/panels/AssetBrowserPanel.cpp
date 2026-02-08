#include "AssetBrowserPanel.h"

#include "core/Log.h"
#include "core/Paths.h"
#include "editor/ui/UiPayloads.h"
#include "render/material/TextureTable.h"
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <utility>
#include <fstream>
#include <sstream>
#include <functional>

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

static bool icontains(const std::string &hay, const std::string &needle) {
  if (needle.empty())
    return true;
  return hay.find(needle) != std::string::npos;
}

static const char *fileTypeBadge(const std::string &pathLower) {
  if (pathLower.ends_with(".cube"))
    return "LUT";
  if (pathLower.ends_with(".hdr") || pathLower.ends_with(".exr"))
    return "HDR";
  if (pathLower.ends_with(".ktx") || pathLower.ends_with(".ktx2"))
    return "KTX";
  if (pathLower.ends_with(".png") || pathLower.ends_with(".jpg") ||
      pathLower.ends_with(".jpeg") || pathLower.ends_with(".tga") ||
      pathLower.ends_with(".bmp"))
    return "IMG";
  return "";
}

static ImU32 fileTypeColor(const std::string &pathLower) {
  if (pathLower.ends_with(".cube"))
    return IM_COL32(70, 180, 255, 255);
  if (pathLower.ends_with(".hdr") || pathLower.ends_with(".exr"))
    return IM_COL32(255, 190, 60, 255);
  if (pathLower.ends_with(".ktx") || pathLower.ends_with(".ktx2"))
    return IM_COL32(120, 220, 120, 255);
  return IM_COL32(200, 200, 200, 255);
}

static void drawFolderIcon(ImDrawList *dl, ImVec2 p, float size,
                           ImU32 fill, ImU32 border) {
  const float w = size;
  const float h = size * 0.75f;
  const ImVec2 tabMin(p.x + 1.0f, p.y + 0.0f);
  const ImVec2 tabMax(p.x + w * 0.55f, p.y + h * 0.4f);
  const ImVec2 bodyMin(p.x + 0.0f, p.y + h * 0.25f);
  const ImVec2 bodyMax(p.x + w, p.y + h + h * 0.25f);
  // tab
  dl->AddRectFilled(tabMin, tabMax, fill, 1.0f);
  // body
  dl->AddRectFilled(bodyMin, bodyMax, fill, 1.0f);
  dl->AddRect(bodyMin, bodyMax, border, 1.0f);
}

static void DrawAtlasIconAt(const Nyx::IconAtlas &atlas,
                            const Nyx::AtlasRegion &r, ImVec2 p, ImVec2 size,
                            ImU32 tint = IM_COL32(220, 220, 220, 255)) {
  p.x = std::floor(p.x + 0.5f);
  p.y = std::floor(p.y + 0.5f);
  size.x = std::floor(size.x + 0.5f);
  size.y = std::floor(size.y + 0.5f);
  ImDrawList *dl = ImGui::GetWindowDrawList();
  dl->AddImage(atlas.imguiTexId(), p, ImVec2(p.x + size.x, p.y + size.y), r.uv0,
               r.uv1, tint);
}
} // namespace

void AssetBrowserPanel::init(TextureTable &texTable) {
  m_tex = &texTable;
  m_cacheDir = std::filesystem::current_path() / ".nyx" / "thumbcache";
  std::error_code ec;
  std::filesystem::create_directories(m_cacheDir, ec);
  startWorker();
}

void AssetBrowserPanel::shutdown() {
  stopWorker();
  clearThumbnails();
  m_items.clear();
  m_tex = nullptr;
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

  // Build folder tree + item map
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
  scanFolderRecursive(m_root);
  if (m_folderItems.find(m_currentFolder) == m_folderItems.end())
    m_currentFolder.clear();
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

    // Try cache first
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

void AssetBrowserPanel::draw(bool *pOpen) {
  if (!pOpen || !*pOpen)
    return;

  if (!m_iconInit) {
    m_iconInit = true;
    const std::filesystem::path iconDir = Paths::engineRes() / "icons";
    const std::filesystem::path jsonPath =
        Paths::engineRes() / "icon_atlas.json";
    const std::filesystem::path pngPath = Paths::engineRes() / "icon_atlas.png";
    if (std::filesystem::exists(jsonPath) && std::filesystem::exists(pngPath)) {
      m_iconReady = m_iconAtlas.loadFromJson(jsonPath.string());
      if (m_iconReady && !m_iconAtlas.find("folder")) {
        m_iconReady = m_iconAtlas.buildFromFolder(
            iconDir.string(), jsonPath.string(), pngPath.string(), 64, 0);
      }
    } else {
      m_iconReady = m_iconAtlas.buildFromFolder(
          iconDir.string(), jsonPath.string(), pngPath.string(), 64, 0);
    }
  }

  if (m_needsRefresh)
    refresh();

  startWorker();

  if (ImGui::Begin("Asset Browser", pOpen)) {
    ImGui::TextUnformatted("Root:");
    ImGui::SameLine();
    ImGui::TextUnformatted(m_root.c_str());

    // Breadcrumbs
    ImGui::Separator();
    if (m_currentFolder.empty()) {
      ImGui::TextUnformatted("Root");
    } else {
      if (ImGui::SmallButton("Root")) {
        m_currentFolder.clear();
      }
      fs::path p(m_currentFolder);
      fs::path accum;
      for (auto &part : p) {
        accum /= part;
        ImGui::SameLine();
        ImGui::TextUnformatted("/");
        ImGui::SameLine();
        const std::string label = part.string();
        if (ImGui::SmallButton(label.c_str())) {
          m_currentFolder = accum.generic_string();
        }
      }
    }

    ImGui::SameLine();
    if (ImGui::Button("Refresh")) {
      refresh();
    }

    ImGui::Separator();

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 110.0f);
    if (ImGui::InputTextWithHint("##asset_filter", "Search assets...", m_filter,
                                 sizeof(m_filter))) {
      m_filterStr = m_filter;
    }
    ImGui::SameLine();
    const bool prevShowAll = m_showAll;
    if (ImGui::Checkbox("Show All", &m_showAll)) {
      if (m_showAll && !prevShowAll) {
        m_lastFolder = m_currentFolder;
      } else if (!m_showAll && prevShowAll) {
        if (!m_lastFolder.empty())
          m_currentFolder = m_lastFolder;
      }
    }

    std::string filterLower = toLower(m_filter);

    // Simple grid (folders + files)
    const float thumb = 64.0f;
    const float pad = 12.0f;
    const float cell = thumb + pad;
    const float w = ImGui::GetContentRegionAvail().x;
    int cols = static_cast<int>(w / cell);
    if (cols < 1)
      cols = 1;

    ImGui::Columns(cols, nullptr, false);

    processReadyThumbs();

    auto itemMatches = [&](const Item &it) -> bool {
      if (filterLower.empty())
        return true;
      const std::string rel = toLower(it.relPath);
      const std::string name = toLower(it.name);
      return icontains(rel, filterLower) || icontains(name, filterLower);
    };

    std::vector<std::string> folders;
    if (m_showAll) {
      folders.reserve(m_folderItems.size());
      for (const auto &kv : m_folderItems) {
        const std::string &path = kv.first;
        if (path.empty())
          continue;
        const std::string rel = toLower(path);
        const std::string name = toLower(fs::path(path).filename().string());
        if (filterLower.empty() || icontains(rel, filterLower) ||
            icontains(name, filterLower)) {
          folders.push_back(path);
        }
      }
    } else {
      const auto itFolders = m_folderChildren.find(m_currentFolder);
      if (itFolders != m_folderChildren.end()) {
        for (const auto &child : itFolders->second) {
          const std::string name = fs::path(child).filename().string();
          const std::string rel = toLower(child);
          if (filterLower.empty() || icontains(rel, filterLower) ||
              icontains(toLower(name), filterLower)) {
            folders.push_back(child);
          }
        }
      }
    }
    std::sort(folders.begin(), folders.end());

    std::vector<size_t> indices;
    if (m_showAll) {
      indices.reserve(m_items.size());
      for (size_t i = 0; i < m_items.size(); ++i) {
        if (itemMatches(m_items[i]))
          indices.push_back(i);
      }
    } else {
      const auto itIdx = m_folderItems.find(m_currentFolder);
      if (itIdx != m_folderItems.end()) {
        const std::vector<size_t> &src = itIdx->second;
        indices.reserve(src.size());
        for (size_t i : src) {
          if (itemMatches(m_items[i]))
            indices.push_back(i);
        }
      }
    }

    // Folders first
    const AtlasRegion *folderIcon =
        m_iconReady ? m_iconAtlas.find("folder") : nullptr;
    for (size_t f = 0; f < folders.size(); ++f) {
      const std::string &folderPath = folders[f];
      const std::string name =
          m_showAll ? folderPath : fs::path(folderPath).filename().string();
      ImGui::PushID((int)f);
      ImGui::Button("##folder_btn", ImVec2(thumb, thumb));
      ImDrawList *dl = ImGui::GetWindowDrawList();
      ImVec2 pmin = ImGui::GetItemRectMin();
      if (folderIcon) {
        const float icon = 32.0f;
        const ImVec2 ip(pmin.x + (thumb - icon) * 0.5f,
                        pmin.y + (thumb - icon) * 0.5f);
        DrawAtlasIconAt(m_iconAtlas, *folderIcon, ip, ImVec2(icon, icon));
      } else {
        drawFolderIcon(dl, ImVec2(pmin.x + 8.0f, pmin.y + 10.0f), 32.0f,
                       IM_COL32(220, 200, 120, 255),
                       IM_COL32(80, 60, 30, 255));
      }
      if (ImGui::IsItemHovered() &&
          ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        m_currentFolder = folderPath;
      }
      ImGui::TextWrapped("%s", name.c_str());
      ImGui::NextColumn();
      ImGui::PopID();
    }

    if (!indices.empty()) {
      for (size_t idx = 0; idx < indices.size(); ++idx) {
        const size_t i = indices[idx];
        auto &it = m_items[i];
        ImGui::PushID(static_cast<int>(i));
        ensureThumbnail(it, i);

        const std::string low = toLower(it.absPath);
        // Thumbnail
        ImTextureID id =
            static_cast<ImTextureID>(static_cast<uintptr_t>(it.glThumb));
        if (it.glThumb != 0) {
          ImGui::Image(id, ImVec2(thumb, thumb));
        } else {
          // placeholder
          ImGui::Button("##missing_thumb", ImVec2(thumb, thumb));
        }

        // File type badge
        const char *badge = fileTypeBadge(low);
        if (badge && badge[0]) {
          ImDrawList *dl = ImGui::GetWindowDrawList();
          ImVec2 pmin = ImGui::GetItemRectMin();
          ImVec2 pmax = ImGui::GetItemRectMax();
          ImVec2 pad(4.0f, 2.0f);
          ImVec2 textSize = ImGui::CalcTextSize(badge);
          ImVec2 bmin(pmax.x - textSize.x - pad.x * 2.0f - 2.0f,
                      pmin.y + 2.0f);
          ImVec2 bmax(pmax.x - 2.0f, pmin.y + textSize.y + pad.y * 2.0f + 2.0f);
          ImU32 col = fileTypeColor(low);
          dl->AddRectFilled(bmin, bmax, IM_COL32(0, 0, 0, 180), 3.0f);
          dl->AddRect(bmin, bmax, col, 3.0f);
          dl->AddText(ImVec2(bmin.x + pad.x, bmin.y + pad.y), col, badge);
        }

        // Drag payload: path string
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
          const char *payloadType = UiPayload::TexturePath;
          // Include null terminator
          ImGui::SetDragDropPayload(payloadType, it.absPath.c_str(),
                                    it.absPath.size() + 1);
          ImGui::TextUnformatted(it.name.c_str());
          ImGui::EndDragDropSource();
        }

        // Label
        ImGui::TextWrapped("%s", it.name.c_str());

        ImGui::NextColumn();
        ImGui::PopID();
      }
    }

    ImGui::Columns(1);
  }

  ImGui::End();
}

} // namespace Nyx
