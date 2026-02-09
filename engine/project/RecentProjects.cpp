#include "RecentProjects.h"
#include <algorithm>

namespace Nyx {

static bool samePath(const std::string &a, const std::string &b) {
#ifdef _WIN32
  // Cheap case-insensitive compare for Windows drive paths.
  if (a.size() != b.size())
    return false;
  for (size_t i = 0; i < a.size(); ++i) {
    char ca = a[i], cb = b[i];
    if (ca >= 'A' && ca <= 'Z')
      ca = (char)(ca - 'A' + 'a');
    if (cb >= 'A' && cb <= 'Z')
      cb = (char)(cb - 'A' + 'a');
    if (ca != cb)
      return false;
  }
  return true;
#else
  return a == b;
#endif
}

void RecentProjects::clear() { items.clear(); }

bool RecentProjects::contains(const std::string &absNyxprojPath) const {
  for (const auto &it : items)
    if (samePath(it, absNyxprojPath))
      return true;
  return false;
}

void RecentProjects::remove(const std::string &absNyxprojPath) {
  items.erase(std::remove_if(items.begin(), items.end(),
                             [&](const std::string &s) {
                               return samePath(s, absNyxprojPath);
                             }),
              items.end());
}

void RecentProjects::add(const std::string &absNyxprojPath) {
  if (absNyxprojPath.empty())
    return;
  remove(absNyxprojPath);
  items.insert(items.begin(), absNyxprojPath);
  if (items.size() > maxItems)
    items.resize(maxItems);
}

} // namespace Nyx
