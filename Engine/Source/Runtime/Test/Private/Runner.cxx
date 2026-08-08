module Nyx.Test;

import :Runner;

import std;
import Nyx.Core;

namespace Nyx::Test::detail {

namespace {

[[nodiscard]] constexpr auto workerCount(usize requested, usize workCount) -> usize {
  if (workCount == 0)
    return 0;

  if (requested == 1)
    return 1;

  if (requested == 0) {
    const usize available = std::thread::hardware_concurrency();
    return std::min(std::max<usize>(available, 1), workCount);
  }

  return std::min(requested, workCount);
}

} // namespace

auto executePlannedCases(RunSession &session, const RunOptions &options) -> Vec<TestExecution> {
  Vec<PlannedCase> plannedCases = session.takePlannedCases();
  const usize workers = workerCount(options.jobs, plannedCases.size());

  if (workers == 0)
    return {};

  if (workers == 1) {
    return plannedCases | std::views::transform([&options](PlannedCase &plannedCase) -> TestExecution {
      return plannedCase.execute(std::move(plannedCase.descriptor), options.timeMode);
    }) | std::ranges::to<Vec<TestExecution>>();
  }

  Vec<TestExecution> executions{plannedCases.size()};

  std::atomic<usize> nextIndex{};
  const auto executeNext = [&plannedCases, &executions, &nextIndex, &options] -> void {
    while (true) {
      const usize index = nextIndex.fetch_add(1, std::memory_order_relaxed);
      if (index >= plannedCases.size())
        return;

      executions[index] =
          plannedCases[index].execute(std::move(plannedCases[index].descriptor), options.timeMode);
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
