#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Nyx {

class NyxStringTable {
public:
  uint32_t intern(const std::string &s) {
    auto it = m_map.find(s);
    if (it != m_map.end())
      return it->second;

    uint32_t id = static_cast<uint32_t>(m_strings.size());
    m_strings.push_back(s);
    m_map[s] = id;
    return id;
  }

  const std::string &get(uint32_t id) const { return m_strings[id]; }

  const std::vector<std::string> &all() const { return m_strings; }

  void clear() {
    m_strings.clear();
    m_map.clear();
  }

private:
  std::vector<std::string> m_strings;
  std::unordered_map<std::string, uint32_t> m_map;
};

} // namespace Nyx
