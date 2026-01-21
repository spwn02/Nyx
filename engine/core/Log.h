#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/fmt/fmt.h>
#include <string_view>
#include <utility>

namespace Nyx::Log {

void Init();

template <class... Args>
inline void Info(fmt::format_string<Args...> f, Args&&... args) {
  spdlog::info(f, std::forward<Args>(args)...);
}

template <class... Args>
inline void Warn(fmt::format_string<Args...> f, Args&&... args) {
  spdlog::warn(f, std::forward<Args>(args)...);
}

template <class... Args>
inline void Error(fmt::format_string<Args...> f, Args&&... args) {
  spdlog::error(f, std::forward<Args>(args)...);
}

template <class... Args>
inline void Debug(fmt::format_string<Args...> f, Args&&... args) {
  spdlog::debug(f, std::forward<Args>(args)...);
}

} // namespace Nyx::Log