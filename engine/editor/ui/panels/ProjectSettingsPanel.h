#pragma once

namespace Nyx {

class EngineContext;
class EditorLayer;

class ProjectSettingsPanel final {
public:
  void draw(EditorLayer &editor, EngineContext &engine);
};

} // namespace Nyx

