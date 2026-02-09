#include "World.h"

#include <algorithm>

namespace Nyx {

uint32_t World::addCategory(const std::string &name) {
  Category c{};
  c.name = name;
  m_categories.push_back(std::move(c));
  m_events.push({WorldEventType::CategoriesChanged, InvalidEntity});
  return (uint32_t)(m_categories.size() - 1u);
}

void World::removeCategory(uint32_t idx) {
  if (idx >= m_categories.size())
    return;
  const int32_t parent = m_categories[idx].parent;
  for (EntityID e : m_categories[idx].entities) {
    auto it = m_entityCategories.find(e);
    if (it != m_entityCategories.end()) {
      auto &lst = it->second;
      lst.erase(std::remove(lst.begin(), lst.end(), idx), lst.end());
      if (lst.empty())
        m_entityCategories.erase(it);
    }
  }

  for (uint32_t child : m_categories[idx].children) {
    if (child < m_categories.size())
      m_categories[child].parent = parent;
  }
  if (parent >= 0 && parent < (int32_t)m_categories.size()) {
    auto &vec = m_categories[(size_t)parent].children;
    vec.erase(std::remove(vec.begin(), vec.end(), idx), vec.end());
    for (uint32_t child : m_categories[idx].children) {
      if (std::find(vec.begin(), vec.end(), child) == vec.end())
        vec.push_back(child);
    }
  }

  m_categories.erase(m_categories.begin() + (ptrdiff_t)idx);

  for (auto &kv : m_entityCategories) {
    for (uint32_t &v : kv.second) {
      if (v > idx)
        v--;
    }
  }
  for (auto &c : m_categories) {
    if (c.parent >= (int32_t)idx)
      c.parent--;
    for (uint32_t &ch : c.children) {
      if (ch > idx)
        ch--;
    }
  }

  m_events.push({WorldEventType::CategoriesChanged, InvalidEntity});
}

void World::renameCategory(uint32_t idx, const std::string &name) {
  if (idx >= m_categories.size())
    return;
  if (m_categories[idx].name == name)
    return;
  m_categories[idx].name = name;
  m_events.push({WorldEventType::CategoriesChanged, InvalidEntity});
}

void World::addEntityCategory(EntityID e, int32_t idx) {
  if (e == InvalidEntity)
    return;
  if (idx < 0 || idx >= (int32_t)m_categories.size())
    return;

  bool changed = false;
  auto &dst = m_categories[(size_t)idx].entities;
  if (std::find(dst.begin(), dst.end(), e) == dst.end()) {
    dst.push_back(e);
    changed = true;
  }

  auto &lst = m_entityCategories[e];
  if (std::find(lst.begin(), lst.end(), (uint32_t)idx) == lst.end()) {
    lst.push_back((uint32_t)idx);
    changed = true;
  }

  if (changed)
    m_events.push({WorldEventType::CategoriesChanged, e});
}

void World::removeEntityCategory(EntityID e, int32_t idx) {
  if (e == InvalidEntity)
    return;
  if (idx < 0 || idx >= (int32_t)m_categories.size())
    return;
  bool changed = false;
  auto &vec = m_categories[(size_t)idx].entities;
  const size_t before = vec.size();
  vec.erase(std::remove(vec.begin(), vec.end(), e), vec.end());
  changed = changed || (vec.size() != before);

  auto it = m_entityCategories.find(e);
  if (it != m_entityCategories.end()) {
    auto &lst = it->second;
    const size_t beforeLst = lst.size();
    lst.erase(std::remove(lst.begin(), lst.end(), (uint32_t)idx), lst.end());
    changed = changed || (lst.size() != beforeLst);
    if (lst.empty())
      m_entityCategories.erase(it);
  }

  if (changed)
    m_events.push({WorldEventType::CategoriesChanged, e});
}

void World::clearEntityCategories(EntityID e) {
  if (e == InvalidEntity)
    return;
  auto it = m_entityCategories.find(e);
  if (it == m_entityCategories.end())
    return;
  bool changed = false;
  for (uint32_t idx : it->second) {
    if (idx < m_categories.size()) {
      auto &vec = m_categories[idx].entities;
      const size_t before = vec.size();
      vec.erase(std::remove(vec.begin(), vec.end(), e), vec.end());
      changed = changed || (vec.size() != before);
    }
  }
  m_entityCategories.erase(it);
  changed = true;
  if (changed)
    m_events.push({WorldEventType::CategoriesChanged, e});
}

const std::vector<uint32_t> *World::entityCategories(EntityID e) const {
  auto it = m_entityCategories.find(e);
  if (it == m_entityCategories.end())
    return nullptr;
  return &it->second;
}

void World::setCategoryParent(uint32_t idx, int32_t parentIdx) {
  if (idx >= m_categories.size())
    return;
  if (parentIdx >= (int32_t)m_categories.size())
    return;
  if ((int32_t)idx == parentIdx)
    return;
  if (m_categories[idx].parent == parentIdx)
    return;

  const int32_t old = m_categories[idx].parent;
  if (old >= 0 && old < (int32_t)m_categories.size()) {
    auto &vec = m_categories[(size_t)old].children;
    vec.erase(std::remove(vec.begin(), vec.end(), idx), vec.end());
  }
  m_categories[idx].parent = parentIdx;
  if (parentIdx >= 0) {
    auto &vec = m_categories[(size_t)parentIdx].children;
    if (std::find(vec.begin(), vec.end(), idx) == vec.end())
      vec.push_back(idx);
  }
  m_events.push({WorldEventType::CategoriesChanged, InvalidEntity});
}

void World::setActiveCameraUUID(EntityUUID id) {
  if (!id) {
    setActiveCamera(InvalidEntity);
    return;
  }
  EntityID e = findByUUID(id);
  if (e == InvalidEntity)
    return;
  setActiveCamera(e);
}

EntityUUID World::uuid(EntityID e) const {
  auto it = m_uuid.find(e);
  if (it == m_uuid.end())
    return EntityUUID{0};
  return it->second;
}

EntityID World::findByUUID(EntityUUID uuid) const {
  if (!uuid)
    return InvalidEntity;
  auto it = m_entityByUUID.find(uuid.value);
  if (it == m_entityByUUID.end())
    return InvalidEntity;
  return it->second;
}

void World::setUUIDSeed(uint64_t seed) { m_uuidGen.setSeed(seed); }

} // namespace Nyx
