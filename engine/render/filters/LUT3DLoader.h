#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Nyx {

struct LUT3DData final {
  uint32_t size = 0;
  std::vector<float> rgb; // size^3 * 3
};

// Minimal .cube 3D LUT loader (DOMAIN_MIN/MAX are ignored; assumes 0..1).
bool loadCubeLUT3D(const std::string &path, LUT3DData &out, std::string &err);

} // namespace Nyx
