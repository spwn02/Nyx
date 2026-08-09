import std;
import Nyx.Core;
import Nyx.Test;

using namespace Nyx;
using namespace Nyx::Test;

auto main() -> int { // NOLINT
  const Vec<TestExecution> executions = runAll(
      TestSelection{
          .group = "core",
      },
      RunOptions{
          .jobs = 0,
          .traceMode = TraceMode::Annotations,
          .order = ExecutionOrder::Shuffled,
      });
  Reporter{
      ReporterOptions{
          .renderer =
              {
                  .color = Nyx::Test::ColorMode::Always,
                  .terminal = true,
                  .showSource = true,
                  .details = Nyx::Test::DetailMode::Trace,
              },
          .showPassedTests = true,
          .showSummary = true,
      },
  }
      .report(executions, std::cout);

  {
    JsonReporter reporter(JsonReporterOptions{
        .pretty = true,
    });

    const Path root = std::filesystem::current_path() / "tests";
    std::ofstream outputList{root / "list.json"};
    if (outputList)
      reporter.reportList(list(), outputList);
    std::ofstream outputSummary{root / "summary.json"};
    if (outputSummary)
      reporter.report(executions, outputSummary);
  }
}
