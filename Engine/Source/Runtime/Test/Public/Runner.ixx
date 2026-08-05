export module Nyx.Test:Runner;

import std;
import Nyx.Core;
import :Execution;

export namespace Nyx::Test {

/// Configures execution of independent reflected test cases.
///
/// jobs == 1 preserves declaration-order, single-threaded dispatch. Higher values only parallelize separate
/// test cases; each case retains its own deterministic Task<T> run loop.
struct RunOptions final {
  usize jobs{1};
};

namespace detail {

struct WorkItem final {
  std::move_only_function<TestExecution()> execute;
};

[[nodiscard]] auto executeWorkItems(Vec<WorkItem> workItems, const RunOptions &options) -> Vec<TestExecution>;

} // namespace detail

} // namespace Nyx::Test
