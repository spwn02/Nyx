#include "MaterialGraphPanel.h"

#include "app/EngineContext.h"
#include "editor/graph/GraphEditorInfra.h"
#include "editor/graph/MaterialGraphAdapter.h"
#include "render/material/MaterialGraph.h"
#include "render/material/MaterialSystem.h"

#include <chrono>

namespace Nyx {

void MaterialGraphPanel::ensureContext() {
  if (m_ctx)
    return;
  m_ctx = GraphEditorInfra::createNodeEditorContext(".cache/nyx_matgraph.json");
}

void MaterialGraphPanel::setMaterial(MaterialHandle h) {
  if (m_mat.slot == h.slot && m_mat.gen == h.gen)
    return;
  m_mat = h;
  m_selectedNode = 0;
  m_selectedLink = 0;
  m_posInitialized.clear();
}

void MaterialGraphPanel::ensureDefaultGraph(EngineContext &engine) {
  auto &materials = engine.materials();
  if (!materials.isAlive(m_mat))
    return;

  MaterialGraph &g = materials.graph(m_mat);
  if (!g.nodes.empty())
    return;

  materials.ensureGraphFromMaterial(m_mat, true);
  m_posInitialized.clear();
}

void MaterialGraphPanel::drawAddMenu(EngineContext &engine) {
  GraphEditorInfra::PopupState popup{m_openAddMenu, m_requestOpenAddMenu,
                                     m_popupPos};
  auto &materials = engine.materials();
  MaterialGraph &g = materials.graph(m_mat);
  MaterialGraphAdapter adapter(g, materials, m_mat, m_ctx, m_posInitialized);
  (void)GraphEditorInfra::drawPalettePopup(
      "AddMaterialNode", "Add Node", "Search nodes...", popup, m_search,
      sizeof(m_search), adapter);
  m_openAddMenu = popup.open;
  m_requestOpenAddMenu = popup.requestOpen;
  m_popupPos = popup.popupPos;
}

void MaterialGraphPanel::draw(EngineContext &engine) {
  const auto drawStart = std::chrono::steady_clock::now();
  ensureContext();

  ImGui::SetNextWindowSize(ImVec2(1200, 720), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Material Graph")) {
    ImGui::End();
    return;
  }

  m_hovered = GraphEditorInfra::graphWindowWantsPriority();
  if (m_hovered) {
    engine.requestUiBlockGlobalShortcuts();
  }

  if (!engine.materials().isAlive(m_mat)) {
    ImGui::TextUnformatted("No material selected.");
    ImGui::End();
    return;
  }

  ensureDefaultGraph(engine);
  drawToolbar(engine);

  ImGui::Separator();

  const float rightWidth = 320.0f;
  if (ImGui::BeginTable("MatGraphLayout", 2,
                        ImGuiTableFlags_Resizable |
                            ImGuiTableFlags_BordersInnerV)) {
    ImGui::TableSetupColumn("Graph", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Props", ImGuiTableColumnFlags_WidthFixed,
                            rightWidth);
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::BeginChild("MatGraphLeft", ImVec2(0, 0), false);
    drawGraph(engine);
    ImGui::EndChild();

    ImGui::TableNextColumn();
    ImGui::BeginChild("MatGraphRight", ImVec2(0, 0), true);
    drawNodeProps(engine);
    ImGui::EndChild();
    ImGui::EndTable();
  }

  const bool windowHovered = GraphEditorInfra::graphWindowWantsPriority();
  GraphEditorInfra::PopupState popup{m_openAddMenu, m_requestOpenAddMenu,
                                     m_popupPos};
  GraphEditorInfra::triggerAddMenuAtMouse(windowHovered, popup, m_search,
                                          sizeof(m_search));
  m_openAddMenu = popup.open;
  m_requestOpenAddMenu = popup.requestOpen;
  m_popupPos = popup.popupPos;

  if (m_openAddMenu || m_requestOpenAddMenu)
    drawAddMenu(engine);

  ImGui::End();
  const auto drawEnd = std::chrono::steady_clock::now();
  m_lastDrawMs =
      std::chrono::duration<float, std::milli>(drawEnd - drawStart).count();
}

} // namespace Nyx
