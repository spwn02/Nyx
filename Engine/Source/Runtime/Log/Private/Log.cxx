module;

#include <spdlog/common.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

module Nyx.Log;

namespace Nyx {

UPtr<spdlog::logger> NLogger::logger_{nullptr};

auto NLogger::init() -> Result<void> {
  auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
  console_sink->set_level(spdlog::level::debug);
  console_sink->set_pattern("[NYXARA] [%^%l%$] %v");

  auto file_sink =
      std::make_shared<spdlog::sinks::basic_file_sink_mt>("logs/nyx.log", true);
  file_sink->set_level(spdlog::level::trace);

  logger_ =
      std::make_unique<spdlog::logger>(spdlog::logger("NYX", {
                                                                 console_sink,
                                                                 file_sink,
                                                             }));
  logger_->set_level(spdlog::level::debug);

  return {};
}

} // namespace Nyx
