#pragma once

namespace Nyx {

class EngineContext;

class Layer {
public:
  virtual ~Layer() = default;

  virtual void onAttach() {}
  virtual void onDetach() {}

  virtual void onUpdate(float /*dt*/) {}
  virtual void onImGui(EngineContext &engine) {}
};

} // namespace Nyx
