#pragma once

#include "scene/EntityUUID.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Nyx {

struct SequencerPersistToggle final {
  EntityUUID entity{};
  uint8_t rowType = 0;
  uint8_t prop = 0;
  bool value = false;
};

struct SequencerPersistState final {
  bool valid = false;
  float pixelsPerFrame = 12.0f;
  float labelGutter = 200.0f;
  int32_t viewFirstFrame = 0;
  bool autoUpdateLastFrame = true;
  int sortMode = 0;
  bool showGraphPanel = false;
  std::string search;
  std::vector<SequencerPersistToggle> expand;
  std::vector<SequencerPersistToggle> stopwatch;
  std::vector<EntityUUID> selectedLayers;
};

} // namespace Nyx
