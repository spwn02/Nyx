#include "PostGraphEditorPanel.h"

#include "app/EngineContext.h"
#include "editor/graph/GraphEditorInfra.h"
#include "editor/ui/UiPayloads.h"
#include "platform/FileDialogs.h"

#include <imgui.h>

#include <cstring>
#include <string>

namespace Nyx {

void PostGraphEditorPanel::drawNodeContents(PostGraph &graph,
                                            const FilterRegistry &registry,
                                            EngineContext &engine, PGNode &n) {
  ImGui::PushID(static_cast<int>(n.id));
  if (n.kind == PGNodeKind::Input) {
    ImGui::TextUnformatted("Scene HDR in");
    ImGui::PopID();
    return;
  }
  if (n.kind == PGNodeKind::Output) {
    ImGui::TextUnformatted("Final LDR out");
    ImGui::PopID();
    return;
  }

  const FilterTypeInfo *t = registry.find(static_cast<FilterTypeId>(n.typeID));

  bool en = n.enabled;
  if (ImGui::Checkbox("##enabled", &en)) {
    n.enabled = en;
    markChanged();
  }
  ImGui::SameLine();
  ImGui::TextUnformatted(t ? t->name : "Filter");

  ImGui::Spacing();
  if (ImGui::Button("Reset")) {
    if (t) {
      n.params.clear();
      n.params.reserve(t->paramCount);
      for (uint32_t i = 0; i < t->paramCount; ++i)
        n.params.push_back(t->params[i].defaultValue);
      markChanged();
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Copy")) {
    m_clipTypeID = n.typeID;
    m_clipParams = n.params;
  }
  ImGui::SameLine();
  const bool canPaste = (m_clipTypeID == n.typeID) && (!m_clipParams.empty());
  if (!canPaste)
    ImGui::BeginDisabled();
  if (ImGui::Button("Paste")) {
    n.params = m_clipParams;
    markChanged();
  }
  if (!canPaste)
    ImGui::EndDisabled();

  ImGui::Spacing();
  if (t && std::string(t->name) == "LUT") {
    if (n.params.size() < 2)
      n.params.resize(2, 0.0f);
    float intensity = n.params[0];
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::SliderFloat("Intensity", &intensity, 0.0f, 1.0f)) {
      n.params[0] = intensity;
      markChanged();
    }

    const auto &lutPaths = engine.postLUTPaths();
    int currentIndex = 0;
    if (!n.lutPath.empty()) {
      for (size_t i = 0; i < lutPaths.size(); ++i) {
        if (lutPaths[i] == n.lutPath) {
          currentIndex = (int)i;
          break;
        }
      }
    }

    ImGui::TextUnformatted("LUT");
    ImGui::SameLine();
    const std::string fileOnly = GraphEditorInfra::filenameOnly(n.lutPath);
    const char *preview = (n.lutPath.empty() ? "Identity" : fileOnly.c_str());
    if (ImGui::BeginCombo("##lut_combo", preview)) {
      for (size_t i = 0; i < lutPaths.size(); ++i) {
        const bool selected = (int)i == currentIndex;
        const std::string lutLabel = GraphEditorInfra::filenameOnly(lutPaths[i]);
        const char *label = lutLabel.c_str();
        if (ImGui::Selectable(label, selected)) {
          n.lutPath = lutPaths[i];
          markChanged();
        }
        if (selected)
          ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }

    ImGui::SameLine();
    const char *btn = n.lutPath.empty() ? "Select..." : "Change...";
    if (ImGui::Button(btn)) {
      const char *filters = "cube";
      if (auto path = FileDialogs::openFile("Select LUT", filters, nullptr)) {
        if (!path->empty()) {
          n.lutPath = *path;
          markChanged();
        }
      }
    }

    if (ImGui::BeginDragDropTarget()) {
      if (const ImGuiPayload *payload =
              ImGui::AcceptDragDropPayload(UiPayload::TexturePath)) {
        const char *p = (const char *)payload->Data;
        if (p) {
          std::string path(p);
          if (GraphEditorInfra::hasExtensionCI(path, "cube")) {
            n.lutPath = path;
            markChanged();
          }
        }
      }
      ImGui::EndDragDropTarget();
    }

    if (!n.lutPath.empty())
      ImGui::TextUnformatted(n.lutPath.c_str());
  } else if (t) {
    const size_t want = t->paramCount;
    if (n.params.size() != want)
      n.params.resize(want, 0.0f);

    for (size_t i = 0; i < want; ++i) {
      const auto &pd = t->params[i];
      float v = n.params[i];

      ImGui::PushID(static_cast<int>(i));
      ImGui::SetNextItemWidth(160.0f);
      bool edited = false;

      if (n.typeID == 29u && std::strcmp(pd.name, "Wrap Mode") == 0) {
        int mode = (v < 0.5f) ? 0 : (v < 1.5f ? 1 : 2);
        ImGui::TextUnformatted(pd.name);
        if (ImGui::RadioButton("Clamp", mode == 0)) {
          mode = 0;
          n.params[i] = static_cast<float>(mode);
          edited = true;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Repeat", mode == 1)) {
          mode = 1;
          n.params[i] = static_cast<float>(mode);
          edited = true;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Mirror", mode == 2)) {
          mode = 2;
          n.params[i] = static_cast<float>(mode);
          edited = true;
        }
        if (edited) {
          m_lastEditCommit = ImGui::GetTime();
          markChanged();
          edited = false;
        }
      } else {
        switch (pd.ui) {
        case FilterParamUI::Slider:
          if (ImGui::SliderFloat(pd.name, &v, pd.minValue, pd.maxValue)) {
            n.params[i] = v;
            edited = true;
          }
          break;
        case FilterParamUI::Drag:
          if (ImGui::DragFloat(pd.name, &v, pd.step, pd.minValue, pd.maxValue)) {
            n.params[i] = v;
            edited = true;
          }
          break;
        case FilterParamUI::Checkbox: {
          bool b = v > 0.5f;
          if (ImGui::Checkbox(pd.name, &b)) {
            n.params[i] = b ? 1.0f : 0.0f;
            markChanged();
          }
        } break;
        case FilterParamUI::Color3: {
          if (i + 2 < want) {
            float col[3] = {n.params[i + 0], n.params[i + 1], n.params[i + 2]};
            if (ImGui::ColorEdit3(pd.name, col)) {
              n.params[i + 0] = col[0];
              n.params[i + 1] = col[1];
              n.params[i + 2] = col[2];
              edited = true;
            }
            i += 2;
          } else if (ImGui::SliderFloat(pd.name, &v, pd.minValue, pd.maxValue)) {
            n.params[i] = v;
            edited = true;
          }
        } break;
        case FilterParamUI::Color4: {
          if (i + 3 < want) {
            float col[4] = {n.params[i + 0], n.params[i + 1], n.params[i + 2],
                            n.params[i + 3]};
            if (ImGui::ColorEdit4(pd.name, col)) {
              n.params[i + 0] = col[0];
              n.params[i + 1] = col[1];
              n.params[i + 2] = col[2];
              n.params[i + 3] = col[3];
              edited = true;
            }
            i += 3;
          } else if (ImGui::SliderFloat(pd.name, &v, pd.minValue, pd.maxValue)) {
            n.params[i] = v;
            edited = true;
          }
        } break;
        }
      }

      if (edited) {
        const double now = ImGui::GetTime();
        if (ImGui::IsItemDeactivatedAfterEdit()) {
          m_lastEditCommit = now;
          markChanged();
        } else if (ImGui::IsItemActive() && (now - m_lastEditCommit) > 0.08) {
          m_lastEditCommit = now;
          markChanged();
        }
      }
      ImGui::PopID();
    }
  } else {
    ImGui::TextUnformatted("(unknown filter type)");
  }

  ImGui::PopID();
}

} // namespace Nyx
