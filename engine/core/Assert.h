#pragma once

#include <cstdlib>
#include <spdlog/spdlog.h>

#if !defined(NYX_ASSERT)
#define NYX_ASSERT(expr, msg)                                                  \
  do {                                                                         \
    if (!(expr)) {                                                             \
      spdlog::critical("ASSERT FAILED: {} | {}:{} | {}", #expr, __FILE__,      \
                       __LINE__, msg);                                         \
      std::abort();                                                            \
    }                                                                          \
  } while (false)
#endif
