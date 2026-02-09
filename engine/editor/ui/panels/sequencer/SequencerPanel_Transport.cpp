#include "editor/ui/panels/SequencerPanel.h"

#include "animation/AnimationSystem.h"
#include "core/Paths.h"
#include "scene/World.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <imgui.h>

namespace Nyx {

namespace {
static int32_t clampi(int32_t v, int32_t a, int32_t b) {
  if (v < a)
    return a;
  if (v > b)
    return b;
  return v;
}

static bool drawAtlasIconButton(const IconAtlas &atlas, const char *name,
                                const ImVec2 &size, ImU32 tint) {
  const AtlasRegion *r = atlas.find(name);
  if (!r)
    return ImGui::SmallButton("?");
  ImGui::InvisibleButton(name, size);
  ImDrawList *dl = ImGui::GetWindowDrawList();
  ImVec2 p0 = ImGui::GetItemRectMin();
  ImVec2 p1 = ImGui::GetItemRectMax();
  dl->AddImage(atlas.imguiTexId(), p0, p1, r->uv0, r->uv1, tint);
  return ImGui::IsItemClicked();
}
} // namespace

void SequencerPanel::togglePlay() {
  if (!m_anim)
    return;
  m_anim->toggle();
}

void SequencerPanel::stop() {
  if (!m_anim)
    return;
  m_anim->pause();
  m_anim->setFrame(0);
}

void SequencerPanel::step(int32_t delta) {
  if (!m_anim || !m_clip)
    return;
  const int32_t cur = m_anim->frame();
  const int32_t last = std::max<int32_t>(0, m_clip->lastFrame);
  m_anim->setFrame(clampi(cur + delta, 0, last));
}

void SequencerPanel::recomputeLastFrameFromKeys() {
  if (!m_clip)
    return;

  int32_t maxF = 0;
  for (const auto &t : m_clip->tracks) {
    for (const auto &k : t.curve.keys) {
      maxF = std::max(maxF, (int32_t)k.frame);
    }
  }
  for (const auto &r : m_clip->entityRanges) {
    maxF = std::max(maxF, (int32_t)r.end);
  }

  m_clip->lastFrame = std::max<int32_t>(0, maxF);
}

void SequencerPanel::buildNlaFromClip() {
  if (!m_anim || !m_clip)
    return;

  m_anim->clearNla();
  if (!m_world)
    return;

  for (const auto &range : m_clip->entityRanges) {
    if (range.entity == InvalidEntity || !m_world->isAlive(range.entity))
      continue;

    AnimAction a{};
    if (m_world->isAlive(range.entity)) {
      a.name = m_world->name(range.entity).name + " [B" +
               std::to_string(range.blockId) + "]";
    } else {
      a.name = "Action B" + std::to_string(range.blockId);
    }

    a.start = range.start;
    a.end = range.end;
    for (const auto &t : m_clip->tracks) {
      if (t.entity != range.entity || t.blockId != range.blockId)
        continue;
      AnimActionTrack at{};
      at.channel = t.channel;
      at.curve = t.curve;
      a.tracks.push_back(std::move(at));
      if (!t.curve.keys.empty()) {
        a.start = std::min(a.start, t.curve.keys.front().frame);
        a.end = std::max(a.end, t.curve.keys.back().frame);
      }
    }

    if (a.tracks.empty())
      continue;

    const ActionID id = m_anim->createAction(std::move(a));
    NlaStrip s{};
    s.action = id;
    s.target = range.entity;
    s.start = range.start;
    s.end = range.end;
    const AnimAction *aa = m_anim->action(id);
    if (aa) {
      s.inFrame = aa->start;
      s.outFrame = aa->end;
    } else {
      s.inFrame = range.start;
      s.outFrame = range.end;
    }
    s.timeScale = 1.0f;
    s.reverse = false;
    s.blend = NlaBlendMode::Replace;
    s.influence = 1.0f;
    s.layer = 0;
    s.muted = false;
    m_anim->addStrip(s);
  }

  m_anim->setFrame(m_anim->frame());
}

void SequencerPanel::drawTransportBar() {
  if (!m_anim || !m_clip) {
    ImGui::TextUnformatted("Sequencer: (no animation clip bound)");
    return;
  }

  if (!m_iconInit) {
    m_iconInit = true;
    const std::filesystem::path iconDir = Paths::engineRes() / "icons";
    const std::filesystem::path jsonPath =
        Paths::engineRes() / "icon_atlas.json";
    const std::filesystem::path pngPath = Paths::engineRes() / "icon_atlas.png";
    if (std::filesystem::exists(jsonPath) && std::filesystem::exists(pngPath)) {
      m_iconReady = m_iconAtlas.loadFromJson(jsonPath.string());
      if (m_iconReady) {
        if (!m_iconAtlas.find("clock") || !m_iconAtlas.find("hide") ||
            !m_iconAtlas.find("show")) {
          m_iconReady = m_iconAtlas.buildFromFolder(
              iconDir.string(), jsonPath.string(), pngPath.string(), 64, 0);
        }
      }
    } else {
      m_iconReady = m_iconAtlas.buildFromFolder(
          iconDir.string(), jsonPath.string(), pngPath.string(), 64, 0);
    }
  }

  const int32_t fpsFrames =
      std::max<int32_t>(1, (int32_t)std::round(m_anim->fps()));
  const int32_t frame = m_anim->frame();
  const int32_t secTotal = frame / fpsFrames;
  const int32_t frameInSec = frame % fpsFrames;
  const int32_t hours = secTotal / 3600;
  const int32_t mins = (secTotal / 60) % 60;
  const int32_t secs = secTotal % 60;

  char timeBuf[64];
  std::snprintf(timeBuf, sizeof(timeBuf), "%d:%02d:%02d:%02d", hours, mins,
                secs, frameInSec);
  ImGui::TextUnformatted(timeBuf);

  ImGui::SameLine();
  ImGui::Text("Frame: %d", frame);
  ImGui::SameLine();
  ImGui::Text("FPS: %.2f", m_anim->fps());

  ImGui::SameLine();
  ImGui::Checkbox("Auto Last", &m_autoUpdateLastFrame);

  ImGui::SameLine();
  int lastFrameInput = std::max(0, m_clip->lastFrame);
  ImGui::BeginDisabled(m_autoUpdateLastFrame);
  ImGui::SetNextItemWidth(120.0f);
  if (ImGui::InputInt("Last Frame", &lastFrameInput)) {
    m_clip->lastFrame = std::max(0, lastFrameInput);
    if (m_anim->frame() > m_clip->lastFrame)
      m_anim->setFrame(m_clip->lastFrame);
  }
  ImGui::EndDisabled();

  ImGui::SameLine();
  ImGui::SetNextItemWidth(180.0f);
  if (ImGui::InputTextWithHint("##SeqSearch", "Search layers", m_searchBuf,
                               sizeof(m_searchBuf))) {
    markLayoutDirty();
  }

  ImGui::SameLine();
  ImGui::SetNextItemWidth(140.0f);
  const char *sortItems[] = {"Scene", "Name A-Z", "Name Z-A", "Parent", "Type"};
  int sortIndex = (int)m_sortMode;
  if (ImGui::Combo("##SeqSort", &sortIndex, sortItems,
                   (int)(sizeof(sortItems) / sizeof(sortItems[0])))) {
    m_sortMode = (SeqSortMode)sortIndex;
    markLayoutDirty();
  }

  ImGui::SameLine();
  if (ImGui::Button("Graph")) {
    m_showGraphPanel = !m_showGraphPanel;
  }

  ImGui::SameLine();
  ImGui::SetNextItemWidth(140.0f);
  ImGui::SliderFloat("Zoom", &m_pixelsPerFrame, m_minPixelsPerFrame, 40.0f,
                     "%.1f px/f");
  ImGui::SameLine();
  ImGui::TextDisabled("CPU %.2f ms", m_lastDrawMs);

  drawNlaControls();
}

void SequencerPanel::drawNlaControls() {
  if (!m_anim)
    return;
  if (!ImGui::CollapsingHeader("NLA", ImGuiTreeNodeFlags_DefaultOpen))
    return;

  const auto &stripsConst = m_anim->strips();
  const auto &actionsConst = m_anim->actions();
  ImGui::Text("Actions: %d  Strips: %d", (int)actionsConst.size(),
              (int)stripsConst.size());
  ImGui::SameLine();
  ImGui::TextDisabled(stripsConst.empty() ? "(Clip mode)" : "(NLA mode)");

  if (ImGui::Button("Build NLA From Clip")) {
    buildNlaFromClip();
  }
  ImGui::SameLine();
  if (ImGui::Button("Clear NLA")) {
    m_anim->clearNla();
    m_anim->setFrame(m_anim->frame());
  }

  if (!actionsConst.empty()) {
    ImGui::SeparatorText("Keying");
    int actionIdx = (m_nlaKeyAction > 0) ? (int)m_nlaKeyAction - 1 : 0;
    actionIdx = std::clamp(actionIdx, 0, (int)actionsConst.size() - 1);
    ImGui::SetNextItemWidth(220.0f);
    const char *preview = actionsConst[(size_t)actionIdx].name.c_str();
    if (ImGui::BeginCombo("Target Action", preview)) {
      for (int i = 0; i < (int)actionsConst.size(); ++i) {
        const bool sel = i == actionIdx;
        const char *label = actionsConst[(size_t)i].name.c_str();
        if (ImGui::Selectable(label, sel)) {
          actionIdx = i;
          m_nlaKeyAction = (ActionID)(i + 1);
        }
        if (sel)
          ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }
    if (m_nlaKeyAction == 0)
      m_nlaKeyAction = (ActionID)(actionIdx + 1);

    ImGui::Checkbox("Auto Key (NLA)", &m_nlaKeying.autoKey);
    ImGui::SameLine();
    ImGui::Checkbox("T", &m_nlaKeying.keyTranslate);
    ImGui::SameLine();
    ImGui::Checkbox("R", &m_nlaKeying.keyRotate);
    ImGui::SameLine();
    ImGui::Checkbox("S", &m_nlaKeying.keyScale);
    ImGui::SameLine();
    int mode = (m_nlaKeying.mode == KeyingMode::Add) ? 1 : 0;
    ImGui::SetNextItemWidth(110.0f);
    if (ImGui::Combo("Mode", &mode, "Replace\0Add\0")) {
      m_nlaKeying.mode = (mode == 1) ? KeyingMode::Add : KeyingMode::Replace;
    }
  }

  auto &strips = m_anim->strips();
  if (strips.empty())
    return;

  ImGui::SeparatorText("Strips");
  for (int i = 0; i < (int)strips.size(); ++i) {
    ImGui::PushID(i);
    NlaStrip &s = strips[(size_t)i];
    const AnimAction *a = m_anim->action(s.action);
    const char *aname = (a && !a->name.empty()) ? a->name.c_str() : "Action";
    const char *tname = "Entity";
    if (m_world && m_world->isAlive(s.target))
      tname = m_world->name(s.target).name.c_str();

    ImGui::Text("%s -> %s", aname, tname);
    ImGui::SameLine();
    if (ImGui::SmallButton("Delete")) {
      m_anim->removeStrip((uint32_t)i);
      ImGui::PopID();
      m_anim->setFrame(m_anim->frame());
      break;
    }

    int start = s.start;
    int end = s.end;
    int inFrame = s.inFrame;
    int outFrame = s.outFrame;
    int layer = s.layer;
    float influence = s.influence;
    float timeScale = s.timeScale;
    bool reverse = s.reverse;
    bool muted = s.muted;
    int blend = (int)s.blend;

    bool changed = false;
    ImGui::SetNextItemWidth(90.0f);
    changed |= ImGui::InputInt("Start", &start);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    changed |= ImGui::InputInt("End", &end);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    changed |= ImGui::InputInt("Layer", &layer);

    ImGui::SetNextItemWidth(90.0f);
    changed |= ImGui::InputInt("In", &inFrame);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    changed |= ImGui::InputInt("Out", &outFrame);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    changed |= ImGui::DragFloat("Influence", &influence, 0.01f, 0.0f, 1.0f);

    ImGui::SetNextItemWidth(120.0f);
    changed |= ImGui::DragFloat("TimeScale", &timeScale, 0.01f, 0.01f, 32.0f);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Reverse", &reverse);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Mute", &muted);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110.0f);
    const char *blendItems[] = {"Replace", "Add"};
    changed |= ImGui::Combo("Blend", &blend, blendItems, 2);

    if (changed) {
      s.start = std::max(0, start);
      s.end = std::max(s.start, end);
      s.inFrame = std::max(0, inFrame);
      s.outFrame = std::max(s.inFrame, outFrame);
      s.layer = layer;
      s.influence = std::clamp(influence, 0.0f, 1.0f);
      s.timeScale = std::max(0.01f, timeScale);
      s.reverse = reverse;
      s.muted = muted;
      s.blend = (blend == 1) ? NlaBlendMode::Add : NlaBlendMode::Replace;
      m_anim->setFrame(m_anim->frame());
    }

    ImGui::Separator();
    ImGui::PopID();
  }
}

} // namespace Nyx
