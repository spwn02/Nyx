#pragma once

#include <imgui.h>

#include <cstdint>
#include <string>
#include <vector>

namespace ax::NodeEditor {
struct EditorContext;
struct Config;
} // namespace ax::NodeEditor

namespace Nyx::GraphEditorInfra {

struct PopupState final {
  bool open = false;
  bool requestOpen = false;
  ImVec2 popupPos{0.0f, 0.0f};
};

struct PaletteItem final {
  uint32_t id = 0;
  const char *name = "";
  const char *category = "";
};

class IGraphAdapter {
public:
  virtual ~IGraphAdapter() = default;
  virtual const std::vector<PaletteItem> &paletteItems() const = 0;
  virtual const std::vector<const char *> &paletteCategories() const = 0;
  virtual bool addPaletteItem(uint32_t itemId, const ImVec2 &popupScreenPos) = 0;
};

bool passFilterCI(const char *filter, const char *text);
std::string filenameOnly(const std::string &path);
bool hasExtensionCI(const std::string &path, const char *extNoDot);

void triggerAddMenuAtMouse(bool panelHovered, PopupState &state, char *searchBuf,
                           size_t searchBufSize);
void preparePopupOpen(const char *popupName, PopupState &state);
bool drawPalettePopup(const char *popupName, const char *title,
                     const char *searchHint, PopupState &state, char *searchBuf,
                     size_t searchBufSize, IGraphAdapter &adapter);
bool graphWindowWantsPriority();

ax::NodeEditor::EditorContext *createNodeEditorContext(const char *settingsFile);
void destroyNodeEditorContext(ax::NodeEditor::EditorContext *&ctx);

} // namespace Nyx::GraphEditorInfra
