#pragma once

#include "editor/ViewportState.h"
#include "editor/tools/LockCameraToView.h"
#include "editor/ui/GizmoState.h"
#include "scene/EntityID.h"
#include "editor/ui/CameraFrameOverlay.h"
#include "editor/ui/CameraGizmosOverlay.h"
#include "editor/ui/LightGizmosOverlay.h"

namespace Nyx {

class EngineContext;
class EditorLayer;

class ViewportPanel {
public:
  void setViewportTexture(uint32_t tex) { m_viewportTex = tex; }
  uint32_t viewportTexture() const { return m_viewportTex; }

  ViewportState &viewport() { return m_viewport; }
  const ViewportState &viewport() const { return m_viewport; }

  void draw(EngineContext &engine, EditorLayer &editor);

  bool gizmoWantsMouse() const { return m_gizmoUsing || m_gizmoOver; }

  GizmoState &gizmoState() { return m_gizmo; }
  const GizmoState &gizmoState() const { return m_gizmo; }

  LockCameraToView &lockCameraToView() { return m_lockCam; }
  const LockCameraToView &lockCameraToView() const { return m_lockCam; }

  bool viewThroughCamera() { return m_viewThroughCamera; }
  void setViewThroughCamera(bool enabled) { m_viewThroughCamera = enabled; }

private:
  ViewportState m_viewport;
  uint32_t m_viewportTex = 0;

  bool m_savedEditorCam = false;
  bool m_viewThroughCamera = false;
  LockCameraToView m_lockCam{};
  EditorCameraState m_savedEditorCamState{};

  bool m_gizmoOver = false;
  bool m_gizmoUsing = false;
  GizmoState m_gizmo{};
  
  CameraFrameOverlay m_frameOverlay;
  CameraGizmosOverlay m_cameraGizmos;
  LightGizmosOverlay m_lightOverlay;

};

} // namespace Nyx
