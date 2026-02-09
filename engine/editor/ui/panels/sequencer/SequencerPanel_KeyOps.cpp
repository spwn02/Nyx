#include "editor/ui/panels/SequencerPanel.h"

#include "animation/AnimationTypes.h"

#include <algorithm>
#include <cmath>

namespace Nyx {

void SequencerPanel::clearSelection() {
  m_selectedKeys.clear();
  m_activeKey = SeqKeyRef{};
  m_draggingKey = false;
}

bool SequencerPanel::isSelected(const SeqKeyRef &k) const {
  for (const auto &s : m_selectedKeys)
    if (s == k)
      return true;
  return false;
}

void SequencerPanel::selectSingle(const SeqKeyRef &k) {
  m_selectedKeys.clear();
  m_selectedKeys.push_back(k);
  m_activeKey = k;
}

void SequencerPanel::toggleSelect(const SeqKeyRef &k) {
  for (size_t i = 0; i < m_selectedKeys.size(); ++i) {
    if (m_selectedKeys[i] == k) {
      m_selectedKeys.erase(m_selectedKeys.begin() + (ptrdiff_t)i);
      if (m_activeKey == k) {
        m_activeKey =
            m_selectedKeys.empty() ? SeqKeyRef{} : m_selectedKeys.back();
      }
      return;
    }
  }
  m_selectedKeys.push_back(k);
  m_activeKey = k;
}

void SequencerPanel::addSelect(const SeqKeyRef &k) {
  if (!isSelected(k))
    m_selectedKeys.push_back(k);
  m_activeKey = k;
}

void SequencerPanel::deleteSelectedKeys() {
  if (!m_clip || m_selectedKeys.empty())
    return;

  struct Pair {
    int t;
    int k;
  };
  std::vector<Pair> del;
  del.reserve(m_selectedKeys.size());
  for (auto &r : m_selectedKeys)
    del.push_back({r.trackIndex, r.keyIndex});

  std::sort(del.begin(), del.end(), [](const Pair &a, const Pair &b) {
    if (a.t != b.t)
      return a.t < b.t;
    return a.k > b.k;
  });

  for (const auto &p : del) {
    if (p.t < 0 || p.t >= (int)m_clip->tracks.size())
      continue;
    auto &keys = m_clip->tracks[(size_t)p.t].curve.keys;
    if (p.k < 0 || p.k >= (int)keys.size())
      continue;
    keys.erase(keys.begin() + (ptrdiff_t)p.k);
  }

  if (m_autoUpdateLastFrame)
    recomputeLastFrameFromKeys();
  clearSelection();
}

void SequencerPanel::copySelectedKeys() {
  if (!m_clip)
    return;
  m_clipboard.clear();
  if (m_selectedKeys.empty())
    return;

  for (const auto &r : m_selectedKeys) {
    if (r.trackIndex < 0 || r.trackIndex >= (int)m_clip->tracks.size())
      continue;
    const auto &keys = m_clip->tracks[(size_t)r.trackIndex].curve.keys;
    if (r.keyIndex < 0 || r.keyIndex >= (int)keys.size())
      continue;

    const auto &k = keys[(size_t)r.keyIndex];
    SeqKeyCopy c{};
    c.trackIndex = r.trackIndex;
    c.frame = (int32_t)k.frame;
    c.value = (float)k.value;
    m_clipboard.push_back(c);
  }
}

void SequencerPanel::pasteKeysAtFrame(int32_t frame) {
  if (!m_clip || m_clipboard.empty())
    return;

  int32_t minF = m_clipboard[0].frame;
  for (auto &c : m_clipboard)
    minF = std::min(minF, c.frame);

  clearSelection();

  for (auto &c : m_clipboard) {
    if (c.trackIndex < 0 || c.trackIndex >= (int)m_clip->tracks.size())
      continue;
    auto &keys = m_clip->tracks[(size_t)c.trackIndex].curve.keys;

    const int32_t newF = clampFrame(frame + (c.frame - minF));

    int existing = -1;
    for (int i = 0; i < (int)keys.size(); ++i) {
      if ((int32_t)keys[(size_t)i].frame == newF) {
        existing = i;
        break;
      }
    }

    if (existing >= 0) {
      keys[(size_t)existing].value = c.value;
      addSelect(SeqKeyRef{c.trackIndex, existing});
    } else {
      AnimKey nk{};
      nk.frame = (AnimFrame)newF;
      nk.value = c.value;
      keys.push_back(nk);
      std::sort(keys.begin(), keys.end(),
                [](const AnimKey &a, const AnimKey &b) {
                  return a.frame < b.frame;
                });

      int idx = -1;
      for (int i = 0; i < (int)keys.size(); ++i) {
        if ((int32_t)keys[(size_t)i].frame == newF &&
            std::fabs(keys[(size_t)i].value - c.value) < 1e-6f) {
          idx = i;
          break;
        }
      }
      if (idx >= 0)
        addSelect(SeqKeyRef{c.trackIndex, idx});
    }
  }

  if (m_autoUpdateLastFrame)
    recomputeLastFrameFromKeys();
}

void SequencerPanel::addKeyAt(int32_t trackIndex, int32_t frame) {
  if (!m_clip)
    return;
  if (trackIndex < 0 || trackIndex >= (int)m_rowEntities.size())
    return;

  const EntityID e = m_rowEntities[(size_t)trackIndex];
  const uint32_t blockId = resolveTargetBlock(e);
  int actualTrack = -1;
  for (int i = 0; i < (int)m_clip->tracks.size(); ++i) {
    const auto &t = m_clip->tracks[(size_t)i];
    if (t.entity == e && t.blockId == blockId &&
        t.channel == AnimChannel::TranslateX) {
      actualTrack = i;
      break;
    }
  }
  if (actualTrack < 0)
    return;

  auto &keys = m_clip->tracks[(size_t)actualTrack].curve.keys;
  const int32_t f = clampFrame(frame);

  for (int i = 0; i < (int)keys.size(); ++i) {
    if ((int32_t)keys[(size_t)i].frame == f) {
      selectSingle(SeqKeyRef{actualTrack, i});
      return;
    }
  }

  AnimKey k{};
  k.frame = (AnimFrame)f;
  k.value = 0.0f;

  keys.push_back(k);
  std::sort(keys.begin(), keys.end(), [](const AnimKey &a, const AnimKey &b) {
    return a.frame < b.frame;
  });

  for (int i = 0; i < (int)keys.size(); ++i) {
    if ((int32_t)keys[(size_t)i].frame == f) {
      selectSingle(SeqKeyRef{actualTrack, i});
      break;
    }
  }

  if (m_autoUpdateLastFrame)
    recomputeLastFrameFromKeys();
}

void SequencerPanel::moveKeyFrame(const SeqKeyRef &k, int32_t newFrame) {
  if (!m_clip)
    return;
  if (k.trackIndex < 0 || k.trackIndex >= (int)m_clip->tracks.size())
    return;

  auto &keys = m_clip->tracks[(size_t)k.trackIndex].curve.keys;
  if (k.keyIndex < 0 || k.keyIndex >= (int)keys.size())
    return;

  const int32_t nf = clampFrame(newFrame);
  const float value = keys[(size_t)k.keyIndex].value;

  keys.erase(keys.begin() + (ptrdiff_t)k.keyIndex);

  int existing = -1;
  for (int i = 0; i < (int)keys.size(); ++i) {
    if ((int32_t)keys[(size_t)i].frame == nf) {
      existing = i;
      break;
    }
  }

  if (existing >= 0) {
    keys[(size_t)existing].value = value;
  } else {
    AnimKey nk{};
    nk.frame = (AnimFrame)nf;
    nk.value = value;
    keys.push_back(nk);
  }

  std::sort(keys.begin(), keys.end(), [](const AnimKey &a, const AnimKey &b) {
    return a.frame < b.frame;
  });

  std::vector<SeqKeyRef> newSel;
  newSel.reserve(m_selectedKeys.size());
  for (auto &s : m_selectedKeys) {
    if (s.trackIndex != k.trackIndex)
      newSel.push_back(s);
  }
  m_selectedKeys = newSel;

  for (int i = 0; i < (int)keys.size(); ++i) {
    if ((int32_t)keys[(size_t)i].frame == nf) {
      addSelect(SeqKeyRef{k.trackIndex, i});
      break;
    }
  }

  if (m_autoUpdateLastFrame)
    recomputeLastFrameFromKeys();
}

void SequencerPanel::setKeyValue(const SeqKeyRef &k, float value) {
  if (!m_clip)
    return;
  if (k.trackIndex < 0 || k.trackIndex >= (int)m_clip->tracks.size())
    return;
  auto &keys = m_clip->tracks[(size_t)k.trackIndex].curve.keys;
  if (k.keyIndex < 0 || k.keyIndex >= (int)keys.size())
    return;
  keys[(size_t)k.keyIndex].value = value;
}

} // namespace Nyx
