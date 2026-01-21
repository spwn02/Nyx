#pragma once

#include <algorithm>
#include <memory>
#include <vector>

#include "Layer.h"

namespace Nyx {

class LayerStack {
public:
  ~LayerStack();

  void pushLayer(std::unique_ptr<Layer> layer);
  void popLayer(Layer *layer);

  auto begin() { return m_layers.begin(); }
  auto end() { return m_layers.end(); }

private:
  std::vector<std::unique_ptr<Layer>> m_layers;
};

} // namespace Nyx
