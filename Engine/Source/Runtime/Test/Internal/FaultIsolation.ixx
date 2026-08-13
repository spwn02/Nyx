module Nyx.Test:FaultIsolation;

import std;
import Nyx.Core;
import :Execution;

namespace Nyx::Test::detail::isolation {

inline constexpr u32 faultRecordMagic{0x4E595846};

/// Fixed-layout record written directly by a native-fault handler.
struct FaultRecord final {
  u32 magic{faultRecordMagic};
  u8 kind{};
  i32 code{};
  u64 address{};
  u64 instruction{};
  u8 symbolsAvailable{};
};

/// Describes one child-process launch without exposing platform handles to Nyx.Test.
struct WorkerLaunch final {
  Path executable;
  Vec<Pair<String, String>> variables;
};

/// Describes how the worker ended from the parent's point of view.
struct WorkerOutcome final {
  bool launched{};
  i32 exitCode{};
  Option<NativeFault> fault;
  String error;
};

/// Returns the current test executable path.
[[nodiscard]] auto executablePath() -> Result<Path>;

/// Starts one isolated worker and waits for its terminal status.
[[nodiscard]] auto launchWorker(const WorkerLaunch &launch) -> WorkerOutcome;

/// Installs the platform-native crash boundary inside a worker process.
[[nodiscard]] auto installWorkerFaultHandler(const Path &faultPath) noexcept -> bool;

/// Converts a platform fault record into the public fault representation.
[[nodiscard]] auto readFaultRecord(const Path &path) noexcept -> Option<NativeFault>;

}
