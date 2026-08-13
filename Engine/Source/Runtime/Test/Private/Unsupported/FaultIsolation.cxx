module Nyx.Test;

import :FaultIsolation;

import std;
import Nyx.Core;

namespace Nyx::Test::detail::isolation {

auto executablePath() -> Result<Path> {
  return bail({"Nyx.Test process isolation is unavailable on this platform"});
}

auto launchWorker(const WorkerLaunch & /*ignored*/) -> WorkerOutcome {
  return WorkerOutcome{
      .fault =
          NativeFault{
              .kind = NativeFaultKind::IsolationUnavailable,
          },
      .error = "Nyx.Test process isolation is unavailable on this platform",
  };
}

auto installWorkerFaultHandler(const Path & /*ignored*/) noexcept -> bool {
  return false;
}

auto readFaultRecord(const Path & /*ignored*/) noexcept -> Option<NativeFault> {
  return None;
}

} // namespace Nyx::Test::detail::isolation
