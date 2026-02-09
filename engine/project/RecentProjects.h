#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace Nyx {

// Stores absolute .nyxproj paths (most-recent-first).
struct RecentProjects final {
  std::vector<std::string> items;
  uint32_t maxItems = 12;

  void clear();
  void add(const std::string &absNyxprojPath);
  void remove(const std::string &absNyxprojPath);
  bool contains(const std::string &absNyxprojPath) const;
};

} // namespace Nyx
