#pragma once

namespace Nyx {

class EngineContext;

class LUTManagerPanel final {
public:
  void draw(EngineContext &engine);

private:
  int m_selectedIndex = 0;
};

} // namespace Nyx
