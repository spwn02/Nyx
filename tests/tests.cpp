import std;
import Nyx.Core;
import Nyx.Test;

using namespace Nyx;
using namespace Nyx::Test;

inline constexpr bool renderJson{false};

auto main() -> int { // NOLINT
  const RunReport report = runAll(
      TestSelection{
          // .group = "math",
      },
      RunOptions{
          .threads = 0,
          .retention = RetentionPolicy::Failures,
          .timeMode = TimeMode::Real,
          .traceMode = TraceMode::Annotations,
          .order = ExecutionOrder::Shuffled,
          .failFast = false,
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
  TestSummary summary = reporter.report(report, std::cout);

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
      jsonReporter.report(report, outputSummary);
  }

  return summary.passed() ? build::exitSuccess : build::exitFailure;
}
