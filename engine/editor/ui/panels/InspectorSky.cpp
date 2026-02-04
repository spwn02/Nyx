#include "InspectorSky.h"

#include "platform/FileDialogs.h"
#include "scene/World.h"
#include "scene/WorldEvents.h"
#include <imgui.h>

namespace Nyx {

void drawSkyPanel(World &world) {
  auto &sky = world.skySettings();

  ImGui::Begin("Sky");

  if (ImGui::Checkbox("##SkyEnabled", &sky.enabled)) {
    world.events().push(
        {.type = WorldEventType::SkyChanged, .a = InvalidEntity});
  }
  ImGui::SameLine();
  ImGui::Text("Enabled");

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

  ImGui::Separator();

  ImGui::TextUnformatted("HDRI Path");
  ImGui::PushItemWidth(-1.0f);
  ImGui::InputText("##SkyHdriPath", sky.hdriPath.data(),
                   sky.hdriPath.capacity() + 1, ImGuiInputTextFlags_ReadOnly);
  ImGui::PopItemWidth();

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

  if (!sky.hdriPath.empty()) {
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
      sky.hdriPath.clear();
      world.events().push(
          {.type = WorldEventType::SkyChanged, .a = InvalidEntity});
    }
  }

  ImGui::End();
}

} // namespace Nyx
