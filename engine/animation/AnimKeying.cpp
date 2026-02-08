#include "AnimKeying.h"

#include <algorithm>

namespace Nyx {

static void sortAndUniqueByFrame(std::vector<AnimKey> &keys) {
  std::sort(keys.begin(), keys.end(),
            [](const AnimKey &a, const AnimKey &b) { return a.frame < b.frame; });

  for (ptrdiff_t i = (ptrdiff_t)keys.size() - 2; i >= 0; --i) {
    if (keys[(size_t)i].frame == keys[(size_t)i + 1].frame)
      keys.erase(keys.begin() + i);
  }
}

static AnimActionTrack *findTrack(AnimAction &a, AnimChannel ch) {
  for (auto &t : a.tracks) {
    if (t.channel == ch)
      return &t;
  }
  return nullptr;
}

static AnimActionTrack &getOrCreateTrack(AnimAction &a, AnimChannel ch) {
  if (AnimActionTrack *t = findTrack(a, ch))
    return *t;
  a.tracks.push_back(AnimActionTrack{.channel = ch});
  return a.tracks.back();
}

static void insertKey(AnimCurve &c, AnimFrame f, float v, KeyingMode mode) {
  if (mode == KeyingMode::Replace) {
    for (auto &k : c.keys) {
      if (k.frame == f) {
        k.value = v;
        return;
      }
    }
  }

  c.keys.push_back(AnimKey{.frame = f, .value = v});
  sortAndUniqueByFrame(c.keys);
}

void keyValue(AnimAction &a, AnimChannel ch, AnimFrame frame, float value,
              KeyingMode mode) {
  AnimActionTrack &t = getOrCreateTrack(a, ch);
  insertKey(t.curve, frame, value, mode);

  if (a.tracks.size() == 1 && t.curve.keys.size() == 1) {
    a.start = frame;
    a.end = frame;
  } else {
    a.start = std::min(a.start, frame);
    a.end = std::max(a.end, frame);
  }
}

} // namespace Nyx

