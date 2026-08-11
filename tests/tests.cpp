import std;
import Nyx.Core;
import Nyx.Test;

using namespace Nyx;
using namespace Nyx::Test;

inline constexpr bool renderJson{false};

auto main() -> int { // NOLINT
  const Vec<TestExecution> executions = runAll(
      TestSelection{
          // .group = "math",
      },
      RunOptions{
          .jobs = 0,
          .timeMode = TimeMode::Real,
          .traceMode = TraceMode::Annotations,
          .order = ExecutionOrder::Shuffled,
          .failFast = true,
      });
  Reporter reporter{
      ReporterOptions{
          .renderer =
              {
                  .color = ColorMode::Always,
                  .terminal = true,
                  .showSource = true,
                  .details = DetailMode::Trace,
              },
          .showPassedTests = true,
          .showSummary = true,
      },
  };
  TestSummary summary = reporter.report(executions, std::cout);

  if constexpr (renderJson) {
    JsonReporter jsonReporter(JsonReporterOptions{
        .pretty = true,
    });

    const Path root = std::filesystem::current_path() / "tests";
    std::ofstream outputList{root / "list.json"};
    if (outputList)
      jsonReporter.reportList(list(), outputList);
    std::ofstream outputSummary{root / "summary.json"};
    if (outputSummary)
      jsonReporter.report(executions, outputSummary);
  }

  return summary.passed() ? build::exitSuccess : build::exitFailure;
}
