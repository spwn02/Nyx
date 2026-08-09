import std;
import Nyx.Core;
import Nyx.Test;

using namespace Nyx;
using namespace Nyx::Test;

auto main() -> int { // NOLINT
  const Vec<TestExecution> executions = runAll(
      TestSelection{
          .tagsAny = {"json"},
          .group = "framework",
      },
      RunOptions{
          .jobs = 0,
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
    const Path path = std::filesystem::current_path() / "tests" / "tests.json";
    std::ofstream file{path};
    if (file)
      JsonReporter(JsonReporterOptions{
                       .pretty = true,
                   })
          .report(executions, file);
  }
}
