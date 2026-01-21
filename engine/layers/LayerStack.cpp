#include "LayerStack.h"

namespace Nyx {

LayerStack::~LayerStack() {
  for (auto &l : m_layers) {
    l->onDetach();
  }
}

void LayerStack::pushLayer(std::unique_ptr<Layer> layer) {
  layer->onAttach();
  m_layers.emplace_back(std::move(layer));
}

void LayerStack::popLayer(Layer *layer) {
  auto it = std::find_if(
      m_layers.begin(), m_layers.end(),
      [&](const std::unique_ptr<Layer> &ptr) { return ptr.get() == layer; });

  if (it != m_layers.end()) {
    (*it)->onDetach();
    m_layers.erase(it);
  }
}

} // namespace Nyx
