#include "Log.h"

#include <filesystem>
#include <memory>
#include <vector>

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace Nyx::Log {
void Init() {
  namespace fs = std::filesystem;
  fs::create_directories("out/ninja-clang");

  std::vector<spdlog::sink_ptr> sinks;
  sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
  sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(
      fs::path("out").append("ninja-clang").append("nyx.log").string(), true));

  auto logger = std::make_shared<spdlog::logger>("nyx", sinks.begin(), sinks.end());
  spdlog::set_default_logger(logger);
  spdlog::set_pattern("[%T] [%^%l%$] %v");
  spdlog::set_level(spdlog::level::debug);
  spdlog::flush_on(spdlog::level::info);
}
} // namespace Nyx::Log
