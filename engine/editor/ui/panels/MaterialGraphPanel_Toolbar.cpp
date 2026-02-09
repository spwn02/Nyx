#include "MaterialGraphPanel.h"

#include "app/EngineContext.h"
#include "render/material/MaterialSystem.h"

#include <imgui.h>

#include <string>
#include <unordered_map>

namespace Nyx {

void MaterialGraphPanel::drawToolbar(EngineContext &engine) {
  auto &materials = engine.materials();
  if (!materials.isAlive(m_mat))
    return;

  if (ImGui::Button("Auto Layout")) {
    m_requestAutoLayout = true;
  }
  ImGui::SameLine();
  if (ImGui::Button("Zoom to Fit")) {
    m_requestNavigateToContent = true;
  }
  ImGui::SameLine();

  if (ImGui::Button("Compile & Upload")) {
    materials.markGraphDirty(m_mat);
    materials.uploadIfDirty();
  }
  ImGui::SameLine();
  if (ImGui::Button("Reset Defaults")) {
    materials.ensureGraphFromMaterial(m_mat, true);
    m_posInitialized.clear();
  }
  ImGui::SameLine();
  if (ImGui::Button("Copy Graph")) {
    m_clipboard = materials.graph(m_mat);
    m_hasClipboard = true;
  }
  ImGui::SameLine();
  if (ImGui::Button("Paste Graph") && m_hasClipboard) {
    MaterialGraph &g = materials.graph(m_mat);
    g.nodes.clear();
    g.links.clear();
    g.nextNodeId = 1;
    g.nextLinkId = 1;

    std::unordered_map<MatNodeID, MatNodeID> idMap;
    idMap.reserve(m_clipboard.nodes.size());
    for (const auto &src : m_clipboard.nodes) {
      MatNode n = src;
      n.id = g.nextNodeId++;
      idMap[src.id] = n.id;
      g.nodes.push_back(std::move(n));
    }
    for (const auto &src : m_clipboard.links) {
      MatLink l = src;
      l.id = g.nextLinkId++;
      l.from.node = idMap[l.from.node];
      l.to.node = idMap[l.to.node];
      g.links.push_back(std::move(l));
    }

    materials.markGraphDirty(m_mat);
    m_posInitialized.clear();
  }

  const std::string &err = materials.graphError(m_mat);
  if (!err.empty()) {
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "Graph Error: %s",
                       err.c_str());
  }
  ImGui::SameLine();
  ImGui::TextDisabled("CPU %.2f ms", m_lastDrawMs);
}

} // namespace Nyx
