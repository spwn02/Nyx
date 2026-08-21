import std;
import Miracle;
import Nyx.Log;
import Nyx.Kernel;

using namespace Nyx;
using namespace Miracle;

auto main() -> int {
  {
    Result<void> result = NLogger::init();
  }
  NLogger::info("Initializing Nyx Engine...");

  Result<Kernel> kernel = Kernel::create();
  if (not kernel) {
    NLogger::error("{}", kernel.error().display({.colours = true}));
    return 1;
  }

  NLogger::info("Kernel has been initialized!");

  {
    Result<void> result = kernel->run();
    if (not result) {
      NLogger::error("{}", result.error().display({.colours = true}));
      return 1;
    }
  }

  return 0;
}
