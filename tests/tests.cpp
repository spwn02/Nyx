import std;
import Nyx.Core;
import Nyx.Test;

using namespace Nyx;
using namespace Nyx::Test;

auto main() -> int {
  const Vec<TestExecution> executions = runAll();
  Reporter{
      ReporterOptions{
          .renderer =
              RendererOptions{
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
