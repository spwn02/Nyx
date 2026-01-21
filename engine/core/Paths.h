#pragma once

#include <filesystem>
#include <string>

namespace Nyx {

struct Paths final {
  // Set once at startup (Application passes argv[0] or current_path)
  static void init(std::filesystem::path executablePath);

  static const std::filesystem::path &engineRoot(); // Nyx/engine
  static std::filesystem::path engineRes();         // engine/resources
  static std::filesystem::path engineShaders();     // engine/resources/shaders

  static std::filesystem::path
  shader(const std::string &file); // engineShaders()/file
};

} // namespace Nyx
