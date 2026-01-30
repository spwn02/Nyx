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
  std::unordered_map<uint32_t, EntityID> pickEntity;

  // Hierarchy cycling: "last clicked node -> next index"
  std::unordered_map<EntityID, uint32_t, EntityHash> cycleIndexByEntity;

  void clear() {
    kind = SelectionKind::None;
    picks.clear();
    activePick = 0;
    activeEntity = InvalidEntity;
    pickEntity.clear();
    cycleIndexByEntity.clear();
  }

  bool isEmpty() const { return kind == SelectionKind::None || picks.empty(); }

  bool hasPick(uint32_t p) const {
    for (uint32_t v : picks)
      if (v == p)
        return true;
    return false;
  }

  EntityID entityForPick(uint32_t p) const {
    auto it = pickEntity.find(p);
    return (it == pickEntity.end()) ? InvalidEntity : it->second;
  }

  void setSinglePick(uint32_t p, EntityID e) {
    kind = SelectionKind::Picks;
    picks.clear();
    picks.push_back(p);
    activePick = p;
    activeEntity = e;
    pickEntity.clear();
    pickEntity.emplace(p, e);
  }

  void addPick(uint32_t p, EntityID e) {
    if (kind != SelectionKind::Picks) {
      kind = SelectionKind::Picks;
      picks.clear();
      pickEntity.clear();
    }
    if (!hasPick(p))
      picks.push_back(p);
    activePick = p;
    activeEntity = e;
    pickEntity.emplace(p, e);
  }

  void togglePick(uint32_t p, EntityID e) {
    if (kind != SelectionKind::Picks) {
      setSinglePick(p, e);
      return;
    }
    for (size_t i = 0; i < picks.size(); ++i) {
      if (picks[i] == p) {
        picks.erase(picks.begin() + (ptrdiff_t)i);
        pickEntity.erase(p);
        if (picks.empty())
          clear();
        else {
          activePick = picks.back();
          activeEntity = entityForPick(activePick);
        }
        return;
      }
    }
    picks.push_back(p);
    activePick = p;
    activeEntity = e;
    pickEntity.emplace(p, e);
  }

  void removePicksForEntity(EntityID e) {
    if (kind != SelectionKind::Picks || picks.empty())
      return;
    picks.erase(std::remove_if(picks.begin(), picks.end(),
                               [&](uint32_t p) {
                                 auto it = pickEntity.find(p);
                                 if (it == pickEntity.end())
                                   return false;
                                 return it->second == e;
                               }),
                picks.end());
    for (auto it = pickEntity.begin(); it != pickEntity.end();) {
      if (it->second == e)
        it = pickEntity.erase(it);
      else
        ++it;
    }
    if (picks.empty()) {
      clear();
    } else {
      activePick = picks.back();
      activeEntity = entityForPick(activePick);
    }
  }
};

} // namespace Nyx
