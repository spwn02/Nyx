#include "AssetBrowserPanel.h"

#include "core/Paths.h"
#include "editor/AssetBrowserContextMenu.h"
#include "editor/ui/UiPayloads.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace fs = std::filesystem;

namespace Nyx {

namespace {

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

static void drawFolderIcon(ImDrawList *dl, ImVec2 p, float size, ImU32 fill,
                           ImU32 border) {
  const float w = size;
  const float h = size * 0.75f;
  const ImVec2 tabMin(p.x + 1.0f, p.y + 0.0f);
  const ImVec2 tabMax(p.x + w * 0.55f, p.y + h * 0.4f);
  const ImVec2 bodyMin(p.x + 0.0f, p.y + h * 0.25f);
  const ImVec2 bodyMax(p.x + w, p.y + h + h * 0.25f);
  dl->AddRectFilled(tabMin, tabMax, fill, 1.0f);
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

void AssetBrowserPanel::ensureIconAtlas() {
  if (m_iconInit)
    return;
  m_iconInit = true;

  const std::filesystem::path iconDir = Paths::engineRes() / "icons";
  const std::filesystem::path jsonPath = Paths::engineRes() / "icon_atlas.json";
  const std::filesystem::path pngPath = Paths::engineRes() / "icon_atlas.png";

  if (std::filesystem::exists(jsonPath) && std::filesystem::exists(pngPath)) {
    m_iconReady = m_iconAtlas.loadFromJson(jsonPath.string());
    if (m_iconReady && !m_iconAtlas.find("folder")) {
      m_iconReady = m_iconAtlas.buildFromFolder(iconDir.string(), jsonPath.string(),
                                                pngPath.string(), 64, 0);
    }
  } else {
    m_iconReady = m_iconAtlas.buildFromFolder(iconDir.string(), jsonPath.string(),
                                              pngPath.string(), 64, 0);
  }
}

void AssetBrowserPanel::drawHeader() {
  ImGui::TextUnformatted("Root:");
  ImGui::SameLine();
  ImGui::TextUnformatted(m_root.c_str());

  ImGui::Separator();
  if (m_currentFolder.empty()) {
    ImGui::TextUnformatted("Root");
  } else {
    if (ImGui::SmallButton("Root"))
      m_currentFolder.clear();

    fs::path p(m_currentFolder);
    fs::path accum;
    for (auto &part : p) {
      accum /= part;
      ImGui::SameLine();
      ImGui::TextUnformatted("/");
      ImGui::SameLine();
      const std::string label = part.string();
      if (ImGui::SmallButton(label.c_str()))
        m_currentFolder = accum.generic_string();
    }
  }

  ImGui::SameLine();
  if (ImGui::Button("Refresh"))
    refresh();
  ImGui::SameLine();
  if (ImGui::Button("Rescan")) {
    if (m_registry) {
      m_registry->rescan();
    }
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
  ImGui::SameLine();
  ImGui::Checkbox("Show Unknown", &m_showUnknown);
}

void AssetBrowserPanel::collectVisibleEntries(
    const std::string &filterLower, std::vector<std::string> &folders,
    std::vector<size_t> &indices) const {
  auto itemMatches = [&](const Item &it) -> bool {
    if (!m_showUnknown && it.type == AssetType::Unknown)
      return false;
    if (filterLower.empty())
      return true;
    const std::string rel = toLower(it.relPath);
    const std::string name = toLower(it.name);
    return icontains(rel, filterLower) || icontains(name, filterLower);
  };

  folders.clear();
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

  indices.clear();
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
}

void AssetBrowserPanel::drawFolderEntries(const std::vector<std::string> &folders,
                                          float thumb) {
  const AtlasRegion *folderIcon = m_iconReady ? m_iconAtlas.find("folder") : nullptr;
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
                     IM_COL32(220, 200, 120, 255), IM_COL32(80, 60, 30, 255));
    }
    if (ImGui::IsItemHovered() &&
        ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
      m_currentFolder = folderPath;
    }
    ImGui::TextWrapped("%s", name.c_str());
    ImGui::NextColumn();
    ImGui::PopID();
  }
}

void AssetBrowserPanel::drawAssetEntries(const std::vector<size_t> &indices,
                                         float thumb) {
  for (size_t idx = 0; idx < indices.size(); ++idx) {
    const size_t i = indices[idx];
    auto &it = m_items[i];
    ImGui::PushID(static_cast<int>(i));
    ensureThumbnail(it, i);

    ImTextureID id = static_cast<ImTextureID>(static_cast<uintptr_t>(it.glThumb));
    if (it.glThumb != 0) {
      ImGui::Image(id, ImVec2(thumb, thumb));
    } else {
      ImGui::Button("##missing_thumb", ImVec2(thumb, thumb));
    }

    const std::string low = toLower(it.absPath);
    const char *badge = fileTypeBadge(low);
    if (badge && badge[0]) {
      ImDrawList *dl = ImGui::GetWindowDrawList();
      ImVec2 pmin = ImGui::GetItemRectMin();
      ImVec2 pmax = ImGui::GetItemRectMax();
      ImVec2 pad(4.0f, 2.0f);
      ImVec2 textSize = ImGui::CalcTextSize(badge);
      ImVec2 bmin(pmax.x - textSize.x - pad.x * 2.0f - 2.0f, pmin.y + 2.0f);
      ImVec2 bmax(pmax.x - 2.0f, pmin.y + textSize.y + pad.y * 2.0f + 2.0f);
      ImU32 col = fileTypeColor(low);
      dl->AddRectFilled(bmin, bmax, IM_COL32(0, 0, 0, 180), 3.0f);
      dl->AddRect(bmin, bmax, col, 3.0f);
      dl->AddText(ImVec2(bmin.x + pad.x, bmin.y + pad.y), col, badge);
    }

    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
      ImGui::SetDragDropPayload(UiPayload::AssetId, &it.id, sizeof(it.id));
      ImGui::SetDragDropPayload(UiPayload::AssetPath, it.relPath.c_str(),
                                it.relPath.size() + 1);
      ImGui::SetDragDropPayload(UiPayload::AssetRelPath, it.relPath.c_str(),
                                it.relPath.size() + 1);
      ImGui::SetDragDropPayload(UiPayload::TexturePath, it.absPath.c_str(),
                                it.absPath.size() + 1);
      ImGui::TextUnformatted(it.name.c_str());
      ImGui::EndDragDropSource();
    }

    ImGui::TextWrapped("%s", it.name.c_str());
    ImGui::NextColumn();
    ImGui::PopID();
  }
}

void AssetBrowserPanel::draw(bool *pOpen) {
  if (!pOpen || !*pOpen)
    return;

  ensureIconAtlas();

  if (m_needsRefresh)
    refresh();

  startWorker();

  if (ImGui::Begin("Asset Browser", pOpen)) {
    drawHeader();

    std::string filterLower = toLower(m_filter);

    const float thumb = 64.0f;
    const float pad = 12.0f;
    const float cell = thumb + pad;
    const float w = ImGui::GetContentRegionAvail().x;
    int cols = static_cast<int>(w / cell);
    if (cols < 1)
      cols = 1;

    ImGui::Columns(cols, nullptr, false);

    processReadyThumbs();

    std::vector<std::string> folders;
    std::vector<size_t> indices;
    collectVisibleEntries(filterLower, folders, indices);

    drawFolderEntries(folders, thumb);
    drawAssetEntries(indices, thumb);

    ImGui::Columns(1);

    if (m_registry) {
      if (NyxProjectRuntime *proj = m_registry->projectRuntime()) {
        bool doRescan = false;
        std::string currentFolder = m_currentFolder;
        if (currentFolder.empty())
          currentFolder = m_registry->contentRootRel();
        drawAssetBrowserContextMenu(*proj, currentFolder, &doRescan);
        if (doRescan) {
          m_registry->rescan();
          refresh();
        }
      }
    }
  }

  ImGui::End();
}

} // namespace Nyx
