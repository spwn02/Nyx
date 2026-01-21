#pragma once

#include "../scene/EntityID.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Nyx {

enum class SelectionKind : uint8_t {
  None = 0,
  Picks, // vector of pickIDs (submesh aware)
};

struct Selection {
  SelectionKind kind = SelectionKind::None;

  std::vector<uint32_t> picks;           // selected pick IDs (unique)
  uint32_t activePick = 0;               // last clicked pick (drives inspector)
  EntityID activeEntity = InvalidEntity; // cached convenience

  // Hierarchy cycling: "last clicked node -> next index"
  std::unordered_map<EntityID, uint32_t> cycleIndexByEntity;

  void clear() {
    kind = SelectionKind::None;
    picks.clear();
    activePick = 0;
    activeEntity = InvalidEntity;
    cycleIndexByEntity.clear();
  }

  bool isEmpty() const { return kind == SelectionKind::None || picks.empty(); }

  bool hasPick(uint32_t p) const {
    for (uint32_t v : picks)
      if (v == p)
        return true;
    return false;
  }

  void setSinglePick(uint32_t p) {
    kind = SelectionKind::Picks;
    picks.clear();
    picks.push_back(p);
    activePick = p;
    activeEntity = InvalidEntity;
  }

  void addPick(uint32_t p) {
    if (kind != SelectionKind::Picks) {
      kind = SelectionKind::Picks;
      picks.clear();
    }
    if (!hasPick(p))
      picks.push_back(p);
    activePick = p;
    activeEntity = InvalidEntity;
  }

  void togglePick(uint32_t p) {
    if (kind != SelectionKind::Picks) {
      setSinglePick(p);
      return;
    }
    for (size_t i = 0; i < picks.size(); ++i) {
      if (picks[i] == p) {
        picks.erase(picks.begin() + (ptrdiff_t)i);
        if (picks.empty())
          clear();
        else
          activePick = picks.back();
        activeEntity = InvalidEntity;
        return;
      }
    }
    picks.push_back(p);
    activePick = p;
    activeEntity = InvalidEntity;
  }
};

} // namespace Nyx
