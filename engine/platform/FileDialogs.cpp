#include "FileDialogs.h"
#include <cstring>

#include <tinyfiledialogs.h>

namespace Nyx::FileDialogs {

std::optional<std::string> openFile(const char *title, const char *filterList,
                                    const char *defaultPath) {
  // tinyfiledialogs uses patterns like "*.png" "*.jpg"
  static const char *patterns[32];
  int patternCount = 0;

  // Parse filterList: "png,jpg,jpeg"
  if (filterList && filterList[0]) {
    std::string s(filterList);
    size_t start = 0;
    while (start < s.size() && patternCount < 32) {
      size_t comma = s.find(',', start);
      if (comma == std::string::npos)
        comma = s.size();
      std::string ext = s.substr(start, comma - start);
      while (!ext.empty() && (ext.front() == ' '))
        ext.erase(ext.begin());
      while (!ext.empty() && (ext.back() == ' '))
        ext.pop_back();
      if (!ext.empty()) {
        std::string pat = "*." + ext;
        // store in static ring via heap (tinyfd wants const char*)
        char *heapStr = (char *)malloc(pat.size() + 1);
        if (heapStr) {
          memcpy(heapStr, pat.c_str(), pat.size() + 1);
          patterns[patternCount++] = heapStr;
        }
      }
      start = comma + 1;
    }
  }

  const char *path = tinyfd_openFileDialog(
      title ? title : "Open File", defaultPath ? defaultPath : "",
      patternCount ? patternCount : 0, patternCount ? patterns : nullptr,
      nullptr, 0);

  // free heap patterns
  for (int i = 0; i < patternCount; ++i) {
    free((void *)patterns[i]);
    patterns[i] = nullptr;
  }

  if (!path || !path[0])
    return std::nullopt;
  return std::string(path);
}

} // namespace Nyx::FileDialogs
