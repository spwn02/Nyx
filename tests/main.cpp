import std;
import Miracle;
import Switch;

using namespace Miracle;
using namespace Switch;

inline constexpr bool renderJson{false};

inline constexpr RunOptions diagnosticPreset{
    .executionMode = ExecutionMode::Diagnostic,
    .threads = 0,
    .retention = RetentionPolicy::All,
    .timeMode = TimeMode::Real,
    .traceMode = TraceMode::Annotations,
    .captureTiming = CapturePolicy::PerAttempt,
    .order = ExecutionOrder::Shuffled,
    .isolation = CrashIsolation::InProcess,
};

auto main() -> int { // NOLINT
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
          .showAttempts = true,
          .showSummary = true,
      },
  };

  const RunReport diagnosticTests =
      runAll(reporter, std::cout, TestSelection{}, diagnosticPreset);

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
      jsonReporter.report(diagnosticTests, outputSummary);
  }

  return diagnosticTests.passed() ? 0 : 1;
}
