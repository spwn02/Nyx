module Nyx.Test;

import :Runner;

import std;
import Nyx.Core;

namespace Nyx::Test::detail {

namespace {

[[nodiscard, maybe_unused]] constexpr auto workerCount(usize requested, usize workCount) -> usize {
  if (workCount == 0)
    return 0;

  return std::min(std::max<usize>(requested, 1), workCount);
}

} // namespace

auto executeWorkItems(Vec<WorkItem> workItems, const RunOptions &options) -> Vec<TestExecution> {
  const usize workers = workerCount(options.jobs, workItems.size());

  if (workers == 0)
    return {};

  if (workers == 1) {
    return workItems | std::views::transform([&options](WorkItem &workItem) -> TestExecution {
      return workItem.execute(options.timeMode);
    }) | std::ranges::to<Vec<TestExecution>>();
  }

  Vec<TestExecution> executions{workItems.size()};

  std::atomic<usize> nextIndex{};
  const auto executeNext = [&workItems, &executions, &nextIndex, &options] -> void {
    while (true) {
      const usize index = nextIndex.fetch_add(1, std::memory_order_relaxed);
      if (index >= workItems.size())
        return;

      executions[index] = workItems[index].execute(options.timeMode);
    }
  };

  {
    Vec<std::jthread> threads{};
    threads.reserve(workers);
    std::ranges::for_each(std::views::indices(workers),
        [&threads, &executeNext](usize) -> void { threads.emplace_back(executeNext); });
  }

  return executions;
}

} // namespace Nyx::Test::detail
