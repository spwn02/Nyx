#include "FilterRegistry.h"

#include "core/Assert.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Nyx {

static std::string toLower(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s)
    out.push_back((char)std::tolower((unsigned char)c));
  return out;
}

static bool icontains(std::string_view haystack, std::string_view needle) {
  if (needle.empty())
    return true;
  const std::string h = toLower(haystack);
  const std::string n = toLower(needle);
  return h.find(n) != std::string::npos;
}

static void uniquePush(std::vector<std::string> &v, std::string s) {
  for (auto &x : v) {
    if (x == s)
      return;
  }
  v.push_back(std::move(s));
}

FilterRegistry::FilterRegistry() { registerBuiltins(); }

void FilterRegistry::clear() {
  m_types.clear();
  m_byId.clear();
  m_byName.clear();
  m_byLowerName.clear();
}

const std::vector<FilterTypeInfo> &FilterRegistry::types() const { return m_types; }

const FilterTypeInfo *FilterRegistry::find(FilterTypeId id) const {
  auto it = m_byId.find(id);
  if (it == m_byId.end())
    return nullptr;
  return it->second;
}

const FilterTypeInfo *FilterRegistry::findByName(std::string_view name) const {
  const std::string key = toLower(name);
  auto it = m_byLowerName.find(key);
  if (it == m_byLowerName.end())
    return nullptr;
  return it->second;
}

std::vector<const FilterTypeInfo *> FilterRegistry::search(std::string_view query,
                                                           std::string_view category) const {
  std::vector<const FilterTypeInfo *> out;

  const std::string q = toLower(query);
  const std::string cat = toLower(category);

  for (const auto &t : m_types) {
    if (!cat.empty() && toLower(t.category) != cat)
      continue;

    if (q.empty()) {
      out.push_back(&t);
      continue;
    }

    bool hit = false;
    if (icontains(t.name, q) || icontains(t.category, q))
      hit = true;

    if (!hit) {
      for (const auto &kw : t.keywords) {
        if (icontains(kw, q)) {
          hit = true;
          break;
        }
      }
    }

    if (hit)
      out.push_back(&t);
  }

  std::stable_sort(out.begin(), out.end(), [](const FilterTypeInfo *a, const FilterTypeInfo *b) {
    const int c = toLower(a->category).compare(toLower(b->category));
    if (c != 0)
      return c < 0;
    return toLower(a->name) < toLower(b->name);
  });

  return out;
}

FilterNode FilterRegistry::makeNode(FilterTypeId id) const {
  const FilterTypeInfo *t = find(id);
  NYX_ASSERT(t != nullptr, "FilterRegistry::makeNode unknown type");

  FilterNode n{};
  n.type = id;
  n.enabled = true;

  for (uint32_t i = 0; i < FilterNode::kMaxParams; ++i)
    n.params[i] = 0.0f;
  for (uint32_t i = 0; i < t->paramCount; ++i)
    n.params[i] = t->params[i].defaultValue;

  if (t->defaultLabel && t->defaultLabel[0]) {
    n.label = t->defaultLabel;
  } else {
    n.label = t->name;
  }

  return n;
}

void FilterRegistry::resetToDefaults(FilterNode &node) const {
  const FilterTypeInfo *t = find(node.type);
  if (!t)
    return;
  for (uint32_t i = 0; i < FilterNode::kMaxParams; ++i)
    node.params[i] = 0.0f;
  for (uint32_t i = 0; i < t->paramCount; ++i)
    node.params[i] = t->params[i].defaultValue;
}

void FilterRegistry::finalize() {
  m_byId.clear();
  m_byName.clear();
  m_byLowerName.clear();

  std::stable_sort(m_types.begin(), m_types.end(),
                   [](const FilterTypeInfo &a, const FilterTypeInfo &b) {
                     return (uint32_t)a.id < (uint32_t)b.id;
                   });

  for (auto &t : m_types) {
    m_byId[t.id] = &t;
    m_byName[t.name] = &t;
    m_byLowerName[toLower(t.name)] = &t;

    for (const std::string &a : t.aliases)
      m_byLowerName[toLower(a)] = &t;
  }
}

std::vector<std::string> FilterRegistry::categories() const {
  std::vector<std::string> cats;
  for (const auto &t : m_types)
    uniquePush(cats, t.category);
  std::stable_sort(cats.begin(), cats.end(), [](const std::string &a, const std::string &b) {
    return toLower(a) < toLower(b);
  });
  return cats;
}

uint32_t FilterRegistry::maxGpuParamCount() const {
  uint32_t m = 0;
  for (const auto &t : m_types)
    m = std::max(m, t.gpuParamCount);
  return m;
}

} // namespace Nyx
