import std;
import Nyx.Core;
import Nyx.Test;

using namespace Nyx;
using namespace Nyx::Test;

auto main() -> int { // NOLINT
  const Vec<TestExecution> executions = runAll(
      TestSelection{
          .tagsAny = {"runner"},
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
}
