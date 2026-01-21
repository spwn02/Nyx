#pragma once

#include <cstdint>

namespace Nyx {

class PassTonemap final {
public:
  void init();
  void shutdown();

  void dispatch(uint32_t hdrTex, uint32_t ldrTex, uint32_t width,
                uint32_t height, float exposure, bool applyGamma);

private:
  uint32_t m_prog = 0;
};

} // namespace Nyx
