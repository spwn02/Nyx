module;
#if defined(__linux__)
#include <unistd.h>
#endif

module Nyx.Core;

import :Memory;

import std;

namespace Nyx::memory {

auto processMemory() noexcept -> Option<ProcessMemorySnapshot> {
#if defined(__linux__)
  std::ifstream statistics{"/proc/self/statm"};
  u64 totalPages{};
  u64 residentPages{};
  if (not(statistics >> totalPages >> residentPages))
    return None;
  static_cast<void>(totalPages);

  const long pageSize = ::sysconf(_SC_PAGESIZE);
  if (pageSize <= 0)
    return None;

  return ProcessMemorySnapshot{
      .residentBytes = static_cast<usize>(residentPages) * static_cast<usize>(pageSize),
  };
#endif
}

} // namespace Nyx::memory
