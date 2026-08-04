#include <catch2/catch_test_macros.hpp>
#include <sago/platform_folders.h>

import Nyx.Core;

using namespace Nyx;

// TEST_CASE("Platform Folders") {
//   std::filesystem::path path{sago::getConfigHome()};
//   auto res = fs::OpenOptions{.read = true}.open(path / "nyx" /
//   "config.toml"); if (not res)
//     return;
//   auto read = res->read();
//   REQUIRE(read);
// }
