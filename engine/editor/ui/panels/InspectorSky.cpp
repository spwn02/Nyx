#include "InspectorSky.h"

#include "app/EngineContext.h"
#include "platform/FileDialogs.h"
#include "scene/World.h"
#include "scene/WorldEvents.h"
#include "editor/ui/UiPayloads.h"
#include <imgui.h>

namespace {
static ImTextureID toImTex(uint32_t glTex) {
  return (ImTextureID)(intptr_t)glTex;
}
static bool acceptHdrDrop(std::string &outAbsPath) {
  if (!ImGui::BeginDragDropTarget())
    return false;
  const ImGuiPayload *p =
      ImGui::AcceptDragDropPayload(Nyx::UiPayload::TexturePath);
  if (p && p->Data && p->DataSize > 0) {
    const char *str = (const char *)p->Data;
    outAbsPath = std::string(str);
    ImGui::EndDragDropTarget();
    return true;
  }
  ImGui::EndDragDropTarget();
  return false;
}
static bool isHdrPath(const std::string &p) {
  const auto dot = p.find_last_of('.');
  if (dot == std::string::npos)
    return false;
  std::string ext = p.substr(dot + 1);
  for (char &c : ext)
    c = (char)std::tolower((unsigned char)c);
  return ext == "hdr" || ext == "exr";
}
} // namespace

namespace Nyx {

void drawSkyPanel(World &world, EngineContext &engine) {
  auto &sky = world.skySettings();

  ImGui::Begin("Sky");

  if (ImGui::Checkbox("##SkyEnabled", &sky.enabled)) {
    world.events().push(
        {.type = WorldEventType::SkyChanged, .a = InvalidEntity});
  }
  ImGui::SameLine();
  ImGui::Text("Enabled");

  ImGui::SameLine();
  if (ImGui::SmallButton("Reset")) {
    sky.enabled = true;
    sky.drawBackground = true;
    sky.intensity = 1.0f;
    sky.exposure = 0.0f;
    sky.rotationYawDeg = 0.0f;
    sky.ambient = 0.03f;
    world.events().push(
        {.type = WorldEventType::SkyChanged, .a = InvalidEntity});
  }

  if (ImGui::Checkbox("##SkyDrawBackground", &sky.drawBackground)) {
    world.events().push(
        {.type = WorldEventType::SkyChanged, .a = InvalidEntity});
  }
  ImGui::SameLine();
  ImGui::Text("Draw Background");

  if (ImGui::SliderFloat("Intensity", &sky.intensity, 0.0f, 10.0f, "%.3f")) {
    world.events().push(
        {.type = WorldEventType::SkyChanged, .a = InvalidEntity});
  }
  if (ImGui::SliderFloat("Exposure (stops)", &sky.exposure, -10.0f, 10.0f,
                         "%.2f")) {
    world.events().push(
        {.type = WorldEventType::SkyChanged, .a = InvalidEntity});
  }
  if (ImGui::SliderFloat("Rotation Y (deg)", &sky.rotationYawDeg, -180.0f,
                         180.0f, "%.1f")) {
    world.events().push(
        {.type = WorldEventType::SkyChanged, .a = InvalidEntity});
  }
  if (ImGui::SliderFloat("Ambient (no IBL)", &sky.ambient, 0.0f, 1.0f, "%.3f")) {
    world.events().push(
        {.type = WorldEventType::SkyChanged, .a = InvalidEntity});
  }

  ImGui::Separator();

  // Preview (equirect)
  const uint32_t hdrTex = engine.envIBL().hdrEquirect();
  if (hdrTex != 0) {
    const float w = ImGui::GetContentRegionAvail().x;
    const float h = w * 0.5f;
    ImGui::Image(toImTex(hdrTex), ImVec2(w, h));
  } else {
    ImGui::TextDisabled("No HDRI loaded");
  }

  ImGui::TextUnformatted("HDRI Path");
  ImGui::PushItemWidth(-1.0f);
  ImGui::InputText("##SkyHdriPath", sky.hdriPath.data(),
                   sky.hdriPath.capacity() + 1, ImGuiInputTextFlags_ReadOnly);
  ImGui::PopItemWidth();

  std::string dropPath;
  if (acceptHdrDrop(dropPath) && isHdrPath(dropPath)) {
    sky.hdriPath = dropPath;
    world.events().push({.type = WorldEventType::SkyChanged, .a = InvalidEntity});
  }

  ImGui::Spacing();

  if (ImGui::Button("Open HDRI...",
                    ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
    std::string path = "";
    if (auto f = FileDialogs::openFile("Select HDRI", "exr,hdr")) {
      path = *f;
    }

    if (!path.empty()) {
      sky.hdriPath = path;
      world.events().push(
          {.type = WorldEventType::SkyChanged, .a = InvalidEntity});
    }
  }

  ImGui::SameLine();
  if (ImGui::Button("Clear")) {
    sky.hdriPath.clear();
    world.events().push(
        {.type = WorldEventType::SkyChanged, .a = InvalidEntity});
  }

  ImGui::End();
}

} // namespace Nyx
