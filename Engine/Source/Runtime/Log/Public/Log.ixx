module;

#include <spdlog/common.h>
#include <spdlog/logger.h>
#include <spdlog/spdlog.h>

export module Nyx.Log;

import std;
import Nyx.Core;

export namespace Nyx {

class NLogger final {
public:
  constexpr explicit NLogger() = default;
  constexpr ~NLogger() = default;

  NLogger(const NLogger &) = delete;
  NLogger(NLogger &&) noexcept = delete;
  auto operator=(const NLogger &) -> NLogger = delete;
  auto operator=(NLogger &&) noexcept -> NLogger = delete;

  static auto init() -> Result<void>;

  [[nodiscard]]
  static auto instance() -> Result<Ref<spdlog::logger>> {
    if (logger_)
      return *logger_;
    return bail({"NLogger::init() must be called before calling NLogger::instance()."});
  }

  template <typename... Args>
  static void debug(spdlog::format_string_t<Args...> message, Args &&...args) {
    if (logger_)
      logger_->debug(message, std::forward<Args>(args)...);
  }

  template <typename... Args>
  static void trace(spdlog::format_string_t<Args...> message, Args &&...args) {
    if (logger_)
      logger_->trace(message, std::forward<Args>(args)...);
  }

  template <typename... Args>
  static void info(spdlog::format_string_t<Args...> message, Args &&...args) {
    if (logger_)
      logger_->info(message, std::forward<Args>(args)...);
  }

  template <typename... Args>
  static void warn(spdlog::format_string_t<Args...> message, Args &&...args) {
    if (logger_)
      logger_->warn(message, std::forward<Args>(args)...);
  }

  template <typename... Args>
  static void error(spdlog::format_string_t<Args...> message, Args &&...args) {
    if (logger_)
      logger_->error(message, std::forward<Args>(args)...);
  }

private:
  static UPtr<spdlog::logger> logger_;
};

} // namespace Nyx
