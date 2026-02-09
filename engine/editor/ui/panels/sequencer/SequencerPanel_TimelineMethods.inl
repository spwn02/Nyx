void SequencerPanel::draw() {
  const auto drawStart = std::chrono::steady_clock::now();
  ImGui::Begin("Sequencer");

  if (m_autoUpdateLastFrame && m_clip) {
    recomputeLastFrameFromKeys();
  }

  drawTransportBar();
  ImGui::Separator();
  const float timelineH = std::min(ImGui::GetContentRegionAvail().y,
                                   m_timelineHeight);
  ImGui::BeginChild("##SequencerTimelineScroll",
                    ImVec2(0.0f, timelineH), false,
                    ImGuiWindowFlags_HorizontalScrollbar |
                        ImGuiWindowFlags_AlwaysVerticalScrollbar);
  if (m_showGraphPanel) {
    rebuildLayoutCacheIfNeeded();

    const float totalW = ImGui::GetContentRegionAvail().x;
    const float splitterW = 6.0f;
    const float minLeftW = 180.0f;
    const float minMainW = 220.0f;
    float leftW = m_labelGutter > 1.0f ? m_labelGutter : 240.0f;
    leftW = clampf(leftW, minLeftW,
                   std::max(minLeftW, totalW - minMainW - splitterW));
    m_labelGutter = leftW;

    ImGui::BeginChild("##GraphLayerBar", ImVec2(leftW, 0.0f), true,
                      ImGuiWindowFlags_AlwaysVerticalScrollbar |
                          ImGuiWindowFlags_HorizontalScrollbar);
    drawLayerBarPane();
    ImGui::EndChild();

    ImGui::SameLine(0.0f, 0.0f);
    const float splitH = ImGui::GetContentRegionAvail().y;
    ImGui::InvisibleButton("##GraphSidebarSplitter", ImVec2(splitterW, splitH));
    const bool splitHovered = ImGui::IsItemHovered();
    const bool splitActive = ImGui::IsItemActive();
    if (splitHovered || splitActive)
      ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (splitActive) {
      m_labelGutter += ImGui::GetIO().MouseDelta.x;
      m_labelGutter =
          clampf(m_labelGutter, minLeftW,
                 std::max(minLeftW, totalW - minMainW - splitterW));
    }
    {
      ImDrawList *dl = ImGui::GetWindowDrawList();
      const ImVec2 s0 = ImGui::GetItemRectMin();
      const ImVec2 s1 = ImGui::GetItemRectMax();
      dl->AddRectFilled(s0, s1, splitActive ? IM_COL32(120, 120, 120, 110)
                                            : (splitHovered ? IM_COL32(95, 95, 95, 80)
                                                            : IM_COL32(70, 70, 70, 55)));
    }

    ImGui::SameLine(0.0f, 0.0f);
    ImGui::BeginChild("##GraphMain", ImVec2(0.0f, 0.0f), true,
                      ImGuiWindowFlags_NoScrollbar);
    const ImVec2 graphAvail = ImGui::GetContentRegionAvail();
    const ImVec2 gp0 = ImGui::GetCursorScreenPos();
    const float rulerH = std::min(m_rulerHeight, std::max(0.0f, graphAvail.y));
    const ImRect rulerRect(gp0, ImVec2(gp0.x + graphAvail.x, gp0.y + rulerH));

    const int32_t lastFrame = m_clip ? std::max<int32_t>(0, m_clip->lastFrame) : 0;
    const float timelineW = std::max(1.0f, rulerRect.GetWidth());
    m_minPixelsPerFrame =
        std::max(1.0f, timelineW / std::max(1, (int)lastFrame + 1));
    if (m_pixelsPerFrame < m_minPixelsPerFrame)
      m_pixelsPerFrame = m_minPixelsPerFrame;
    const int32_t framesVisible =
        std::max<int32_t>(1, (int32_t)(timelineW / m_pixelsPerFrame));
    const int32_t maxFirstFrame = std::max(0, lastFrame - framesVisible);
    m_viewFirstFrame = clampi(m_viewFirstFrame, 0, maxFirstFrame);
    const int32_t firstFrame = m_viewFirstFrame;
    const int32_t lastVisible =
        std::min(lastFrame, firstFrame + std::max<int32_t>(0, framesVisible - 1));

    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(rulerRect.Min, rulerRect.Max, IM_COL32(18, 18, 18, 255));
    dl->AddRect(rulerRect.Min, rulerRect.Max, IM_COL32(60, 60, 60, 255));

    const int32_t fpsFrames =
        std::max<int32_t>(1, (int32_t)std::round(m_anim->fps()));
    const float minLabelPx = 70.0f;
    float stepFrames = 1.0f;
    const float fpsF = (float)fpsFrames;
    const float stepCandidates[] = {1,   2,   5,   10,  0.25f * fpsF,
                                    0.5f * fpsF, 1.0f * fpsF, 2.0f * fpsF,
                                    5.0f * fpsF, 10.0f * fpsF, 30.0f * fpsF,
                                    60.0f * fpsF, 120.0f * fpsF,
                                    300.0f * fpsF};
    for (float s : stepCandidates) {
      if (s < 1.0f)
        continue;
      if (s * m_pixelsPerFrame >= minLabelPx) {
        stepFrames = s;
        break;
      }
      stepFrames = s;
    }
    const int32_t stepI = std::max(1, (int32_t)std::round(stepFrames));
    const int32_t firstStep = (firstFrame / stepI) * stepI;
    for (int32_t f = firstStep; f <= lastVisible; f += stepI) {
      const float x = gp0.x + float(f - firstFrame) * m_pixelsPerFrame;
      dl->AddLine(ImVec2(x, rulerRect.Min.y), ImVec2(x, rulerRect.Max.y),
                  IM_COL32(35, 35, 35, 255), 1.0f);
      double seconds = (double)f / (double)fpsFrames;
      char buf[64];
      if (stepFrames >= fpsFrames * 60.0f) {
        const int total = (int)seconds;
        const int mm = total / 60;
        const int ss = total % 60;
        std::snprintf(buf, sizeof(buf), "%d:%02d", mm, ss);
      } else if (stepFrames >= fpsFrames) {
        std::snprintf(buf, sizeof(buf), "%.0f s", seconds);
      } else {
        std::snprintf(buf, sizeof(buf), "%.2f s", seconds);
      }
      dl->AddText(ImVec2(x + 2.0f, rulerRect.Min.y + 2.0f),
                  IM_COL32(140, 140, 140, 255), buf);
    }
    if (m_anim) {
      const float frameX =
          gp0.x + float(m_anim->frame() - firstFrame) * m_pixelsPerFrame;
      dl->AddLine(ImVec2(frameX, rulerRect.Min.y), ImVec2(frameX, rulerRect.Max.y),
                  IM_COL32(255, 80, 80, 255), 2.0f);
    }

    ImGui::SetCursorScreenPos(rulerRect.Min);
    ImGui::InvisibleButton(
        "##GraphSharedRuler", ImVec2(rulerRect.GetWidth(), rulerRect.GetHeight()),
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    const bool rulerHovered = ImGui::IsItemHovered();
    const ImVec2 mp = ImGui::GetMousePos();
    if (rulerHovered && m_anim) {
      ImGuiIO &io = ImGui::GetIO();
      if (io.KeyAlt && io.MouseWheel != 0.0f) {
        const float zoom = (io.MouseWheel > 0.0f) ? 1.1f : 0.9f;
        m_pixelsPerFrame *= zoom;
        if (m_pixelsPerFrame < m_minPixelsPerFrame)
          m_pixelsPerFrame = m_minPixelsPerFrame;
        const int32_t maxFirstAfterZoom = std::max(0, lastFrame - framesVisible);
        m_viewFirstFrame = clampi(m_viewFirstFrame, 0, maxFirstAfterZoom);
      } else {
        float scroll = 0.0f;
        if (io.MouseWheelH != 0.0f)
          scroll = io.MouseWheelH;
        else if (io.KeyShift && io.MouseWheel != 0.0f)
          scroll = io.MouseWheel;
        if (scroll != 0.0f) {
          const int32_t step = std::max<int32_t>(1, framesVisible / 10);
          m_viewFirstFrame -= (int32_t)std::round(scroll * (float)step);
          m_viewFirstFrame = clampi(m_viewFirstFrame, 0, maxFirstFrame);
        }
      }

      if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
          (ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
           ImGui::IsItemActive())) {
        const int32_t f = clampFrame(firstFrame + (int32_t)std::round(
                                                    (mp.x - gp0.x) /
                                                    std::max(1.0f, m_pixelsPerFrame)));
        m_anim->setFrame(f);
      }

      if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
        m_panningTimeline = true;
        m_panStartMouseX = mp.x;
        m_panStartFirstFrame = m_viewFirstFrame;
      }
      if (m_panningTimeline && ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
        const float dx = mp.x - m_panStartMouseX;
        const int32_t df =
            (int32_t)std::round(-dx / std::max(1.0f, m_pixelsPerFrame));
        m_viewFirstFrame = clampi(m_panStartFirstFrame + df, 0, maxFirstFrame);
      } else if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
        m_panningTimeline = false;
      }
    }

    ImGui::SetCursorScreenPos(ImVec2(gp0.x, gp0.y + rulerH));
    if (m_clip) {
      const bool valid =
          m_graphTrackIndex >= 0 && m_graphTrackIndex < (int)m_clip->tracks.size();
      if (!valid) {
        m_graphTrackIndex = -1;
        for (int ti = 0; ti < (int)m_clip->tracks.size(); ++ti) {
          if (!m_clip->tracks[(size_t)ti].curve.keys.empty()) {
            m_graphTrackIndex = ti;
            break;
          }
        }
        if (m_graphTrackIndex < 0 && !m_clip->tracks.empty())
          m_graphTrackIndex = 0;
      }
    } else {
      m_graphTrackIndex = -1;
    }
    m_curveEditor.setClip(m_clip);
    m_curveEditor.setFrameWindow(m_viewFirstFrame, m_pixelsPerFrame);
    m_curveEditor.setCurrentFrame(m_anim ? m_anim->frame() : 0);
    m_curveEditor.setActiveTrack(m_graphTrackIndex);
    m_curveEditor.onImGui();
    m_graphTrackIndex = m_curveEditor.activeTrack();
    ImGui::EndChild();
  } else {
    drawTimeline();
  }
  ImGui::EndChild();

  ImGui::End();

  // Sequencer inspector removed: inspector panel is source of truth
  const auto drawEnd = std::chrono::steady_clock::now();
  m_lastDrawMs =
      std::chrono::duration<float, std::milli>(drawEnd - drawStart).count();
}

void SequencerPanel::handleStepRepeat(const InputSystem &input, float dt) {
  if (!timelineHot() || !m_anim || !m_clip)
    return;

  const bool leftDown = input.isDown(Key::ArrowLeft);
  const bool rightDown = input.isDown(Key::ArrowRight);
  int dir = 0;
  if (leftDown && !rightDown)
    dir = -1;
  else if (rightDown && !leftDown)
    dir = 1;

  if (dir == 0) {
    m_repeatDir = 0;
    m_repeatTimer = 0.0f;
    return;
  }

  const bool ctrl = input.isDown(Key::LeftCtrl) || input.isDown(Key::RightCtrl);
  const int stepSize = ctrl ? 10 : 1;

  const bool justPressed =
      (dir < 0 && input.isPressed(Key::ArrowLeft)) ||
      (dir > 0 && input.isPressed(Key::ArrowRight));

  if (justPressed || dir != m_repeatDir) {
    step(dir * stepSize);
    m_repeatDir = dir;
    m_repeatTimer = m_repeatDelay;
    return;
  }

  m_repeatTimer -= std::max(0.0f, dt);
  while (m_repeatTimer <= 0.0f) {
    step(dir * stepSize);
    m_repeatTimer += m_repeatRate;
  }
}

} // namespace Nyx
