#include "AssetOps.h"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace Nyx::AssetOps {

static std::string norm(std::string s) {
  for (char &c : s) {
    if (c == '\\')
      c = '/';
  }
  while (true) {
    const size_t p = s.find("//");
    if (p == std::string::npos)
      break;
    s.erase(p, 1);
  }
  if (!s.empty() && s.back() == '/')
    s.pop_back();
  return s;
}

bool createFolder(NyxProjectRuntime &proj, const std::string &folderRel) {
  const std::string abs = proj.makeAbsolute(norm(folderRel));
  std::error_code ec;
  fs::create_directories(fs::path(abs), ec);
  return !ec;
}

bool createEmptyTextFile(NyxProjectRuntime &proj, const std::string &fileRel,
                         const char *text) {
  const std::string abs = proj.makeAbsolute(norm(fileRel));
  std::error_code ec;
  fs::create_directories(fs::path(abs).parent_path(), ec);

  std::ofstream f(abs, std::ios::binary);
  if (!f.is_open())
    return false;
  if (text)
    f.write(text, static_cast<std::streamsize>(
                      std::char_traits<char>::length(text)));
  return true;
}

} // namespace Nyx::AssetOps
