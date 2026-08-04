export module Nyx.Core:Build;

export namespace Nyx::build {

inline constexpr bool development = true;
inline constexpr bool optimized = false;
inline constexpr bool assertions = true;
inline constexpr bool expensiveChecks = true;
inline constexpr bool gpuValidation = true;
inline constexpr bool profiling = true;

inline constexpr int exitSuccess = 0;
inline constexpr int exitFailure = 1;

} // namespace Nyx::build
