# Editor Refactor Map

## Goals
- Keep editor modules cohesive and reusable.
- Keep source files below 1000 LOC where practical.
- Centralize shared graph editor behavior.
- Reduce per-frame editor overhead in hot panels.

## Graph Editor
- Shared infrastructure: `engine/editor/graph/GraphEditorInfra.h`, `engine/editor/graph/GraphEditorInfra.cpp`
  - Popup state and Shift+A behavior
  - Search/filter helpers
  - Node-editor context lifecycle
  - Shared categorized palette rendering (`drawPalettePopup`)
- Adapter contract: `engine/editor/graph/GraphEditorInfra.h`
  - `IGraphAdapter`
  - `PaletteItem`
- Domain adapters:
  - `engine/editor/graph/MaterialGraphAdapter.h`, `engine/editor/graph/MaterialGraphAdapter.cpp`
  - `engine/editor/graph/PostGraphAdapter.h`, `engine/editor/graph/PostGraphAdapter.cpp`
- Panel wrappers:
  - `engine/editor/ui/panels/MaterialGraphPanel.cpp`
  - `engine/editor/ui/panels/PostGraphEditorPanel.cpp`

## Sequencer
- Orchestrator / common methods:
  - `engine/editor/ui/panels/SequencerPanel.cpp`
- State/persist:
  - `engine/editor/ui/panels/SequencerPanel_State.cpp`
- Feature modules:
  - `engine/editor/ui/panels/sequencer/SequencerPanel_KeyOps.cpp`
  - `engine/editor/ui/panels/sequencer/SequencerPanel_Rows.cpp`
  - `engine/editor/ui/panels/sequencer/SequencerPanel_Transport.cpp`
  - `engine/editor/ui/panels/sequencer/SequencerPanel_Timeline.cpp`
  - `engine/editor/ui/panels/sequencer/SequencerPanel_TimelineMethods.inl`
- Perf cleanups:
  - Removed duplicate row/track rebuild passes inside `updateHiddenEntities()`
  - Reused scratch frame vector to avoid repeated hot-path allocations
  - Added lightweight panel CPU timing display

## History / App / Hierarchy splits
- Editor history:
  - `engine/editor/EditorHistory.cpp`
  - `engine/editor/EditorHistory_Animation.cpp`
  - `engine/editor/EditorHistory_Apply.cpp`
  - `engine/editor/EditorHistory_Persistence.cpp`
  - `engine/editor/EditorHistory_PersistenceHelpers.inl`
- Application:
  - `engine/app/Application.cpp`
  - `engine/app/Application_Helpers.inl`
- Hierarchy panel:
  - `engine/editor/ui/panels/HierarchyPanel.cpp`
  - `engine/editor/ui/panels/HierarchyPanel_Helpers.inl`

## Current constraint status
- Engine `.cpp/.h/.inl` files are now under 1000 LOC.
- Legacy serializer paths previously removed from runtime/editor usage remain out of the active path.
