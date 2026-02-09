#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace Nyx {

// -----------------------------------------------------------------------------
// .nyxproj v1
// - binary, chunked
// - stores project settings + asset roots + scene list
// - scenes / assets referenced via relative paths (project-root relative)
// -----------------------------------------------------------------------------

static constexpr uint32_t NYXPROJ_MAGIC = 0x4A525058; // 'XPRJ' (little-endian)
static constexpr uint16_t NYXPROJ_VER_MAJOR = 1;
static constexpr uint16_t NYXPROJ_VER_MINOR = 0;

struct NyxProjHeader final {
  uint32_t magic = NYXPROJ_MAGIC;
  uint16_t verMajor = NYXPROJ_VER_MAJOR;
  uint16_t verMinor = NYXPROJ_VER_MINOR;
};

struct NyxProjectSceneEntry final {
  std::string relPath; // e.g. "Scenes/Main.nyxscene"
  std::string name;    // display name (optional; if empty, derived from filename)
};

struct NyxProjectSettings final {
  // Rendering defaults
  float exposure = 1.0f;
  bool vsync = true;

  // Editor defaults
  std::string startupScene; // rel path; can be empty
};

struct NyxProject final {
  NyxProjHeader header{};
  std::string name = "NyxProject";

  // Project root is NOT serialized; it is resolved by file location at runtime.
  // (Your loader should set it externally.)
  std::string assetRootRel = "Content"; // project-relative folder

  std::vector<NyxProjectSceneEntry> scenes;

  NyxProjectSettings settings{};
};

} // namespace Nyx
