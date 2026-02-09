#include "EditorLayer.h"

#include "app/EngineContext.h"

namespace Nyx {

void EditorLayer::updateSceneSerialAndHistoryState(EngineContext &engine) {
  if (!m_sceneManager)
    return;

  const uint64_t serial = m_sceneManager->sceneChangeSerial();
  if (serial != m_seenSceneChangeSerial) {
    m_seenSceneChangeSerial = serial;
    m_history.clear();
    markSceneClean(engine);
    m_ignoreDirtyFramesAfterSceneLoad = 2;
    m_absorbMaterialHistoryAfterSceneLoad = true;
    m_lastObservedMaterialSerial = engine.materials().changeSerial();
    m_materialStableFramesAfterSceneLoad = 0;
  }

  if (!m_absorbMaterialHistoryAfterSceneLoad)
    return;

  const uint64_t matSerial = engine.materials().changeSerial();
  if (matSerial != m_lastObservedMaterialSerial) {
    m_lastObservedMaterialSerial = matSerial;
    m_materialStableFramesAfterSceneLoad = 0;
  } else {
    ++m_materialStableFramesAfterSceneLoad;
    if (m_materialStableFramesAfterSceneLoad >= 8)
      m_absorbMaterialHistoryAfterSceneLoad = false;
  }
}

void EditorLayer::updateSceneDirtyState(EngineContext &engine) {
  if (!m_sceneManager || !m_sceneManager->hasActive())
    return;

  const uint64_t rev = m_history.revision();
  if (m_ignoreDirtyFramesAfterSceneLoad > 0) {
    --m_ignoreDirtyFramesAfterSceneLoad;
    m_lastCleanHistoryRevision = rev;
    m_lastObservedHistoryRevision = rev;
    m_sceneManager->active().dirty = false;
    return;
  }

  if (rev != m_lastObservedHistoryRevision) {
    m_lastObservedHistoryRevision = rev;
    if (rev != m_lastCleanHistoryRevision)
      m_sceneManager->active().dirty = true;
  }

  if (!m_sceneManager->active().dirty) {
    m_lastCleanHistoryRevision = rev;
    m_lastObservedHistoryRevision = rev;
  }
}

} // namespace Nyx
