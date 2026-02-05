#include "LUT3DLoader.h"

#include <fstream>
#include <sstream>

namespace Nyx {

static bool startsWith(const std::string &s, const char *p) {
  return s.rfind(p, 0) == 0;
}

bool loadCubeLUT3D(const std::string &path, LUT3DData &out, std::string &err) {
  std::ifstream f(path);
  if (!f.is_open()) {
    err = "Failed to open LUT: " + path;
    return false;
  }

  uint32_t size = 0;
  std::vector<float> values;
  values.reserve(16 * 16 * 16 * 3);

  std::string line;
  while (std::getline(f, line)) {
    // trim
    while (!line.empty() &&
           (line.back() == '\r' || line.back() == '\n' || line.back() == ' ' ||
            line.back() == '\t'))
      line.pop_back();
    size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
      ++i;
    if (i >= line.size())
      continue;
    if (line[i] == '#')
      continue;

    const std::string s = line.substr(i);
    if (startsWith(s, "TITLE") || startsWith(s, "DOMAIN_MIN") ||
        startsWith(s, "DOMAIN_MAX"))
      continue;
    if (startsWith(s, "LUT_3D_SIZE")) {
      std::istringstream ss(s);
      std::string tok;
      ss >> tok >> size;
      continue;
    }

    std::istringstream ss(s);
    float r = 0.0f, g = 0.0f, b = 0.0f;
    if (!(ss >> r >> g >> b))
      continue;
    values.push_back(r);
    values.push_back(g);
    values.push_back(b);
  }

  if (size == 0) {
    err = "LUT_3D_SIZE not found";
    return false;
  }

  const size_t want = (size_t)size * size * size * 3;
  if (values.size() < want) {
    err = "LUT data too small";
    return false;
  }
  values.resize(want);

  out.size = size;
  out.rgb = std::move(values);
  return true;
}

} // namespace Nyx
