module;

#include <cerrno>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

module Nyx.Test;

import :FaultIsolation;

import std;
import Nyx.Core;

// NOLINTBEGIN
namespace Nyx::Test::detail::isolation {

namespace {

volatile std::sig_atomic_t faultDescriptor{-1};

auto faultHandler(int signalNumber, siginfo_t *information, void *) noexcept -> void {
  FaultRecord record{
      .kind = static_cast<u8>(NativeFaultKind::Signal),
      .code = signalNumber,
      .address = information == nullptr ? 0 : reinterpret_cast<u64>(information->si_addr),
      .instruction = 0,
      .symbolsAvailable = 0,
  };

  if (faultDescriptor >= 0)
    static_cast<void>(::write(faultDescriptor, &record, sizeof(record)));

  ::_exit(128 + signalNumber);
}

[[nodiscard]] auto faultPath(const WorkerLaunch &launch) -> Option<Path> {
  const auto variable = std::ranges::find_if(launch.variables,
      [](const Pair<String, String> &item) -> bool { return item.first == "NYX_TEST_WORKER_FAULT"; });
  if (variable == launch.variables.end())
    return None;

  return Path{variable->second};
}

[[nodiscard]] auto environmentBlock(const Vec<Pair<String, String>> &variables) -> Vec<String> {
  Vec<String> result{};

  if (environ != nullptr) {
    char **cursor = environ;
    while (*cursor != nullptr) {
      const StringView entry{*cursor};
      const usize separator = entry.find('=');
      const StringView name = entry.substr(0, separator);
      const bool overriden = std::ranges::any_of(
          variables, [name](const Pair<String, String> &variable) -> bool { return variable.first == name; });
      if (not overriden)
        result.emplace_back(entry);
      ++cursor;
    }
  }

  std::ranges::for_each(variables, [&result](const Pair<String, String> &variable) -> void {
    result.push_back(std::format("{}={}", variable.first, variable.second));
  });
  return result;
}

} // namespace

auto executablePath() -> Result<Path> {
  constexpr usize kb4{4096};
  Array<char, kb4> buffer{};
  const auto size = static_cast<isize>(::readlink("/proc/self/exe", buffer.data(), buffer.size() - 1));
  if (size <= 0)
    return bail({"Nyx.Test could not resolve /proc/self/exe"});

  buffer.at(static_cast<usize>(size)) = '\0';
  return Path{buffer.data()};
}

auto launchWorker(const WorkerLaunch &launch) -> WorkerOutcome {
  Vec<String> environment = environmentBlock(launch.variables);
  Vec<char *> environmentPointers{};
  environmentPointers.reserve(environment.size() + 1);
  std::ranges::for_each(environment,
      [&environmentPointers](String &entry) -> void { environmentPointers.push_back(entry.data()); });
  environmentPointers.push_back(nullptr);

  String executable = launch.executable.string();
  Array<char *, 2> arguments{executable.data(), nullptr};
  const pid_t child = ::fork();
  if (child < 0)
    return WorkerOutcome{
        .error = "fork() failed while starting a Nyx.Test worker",
    };

  if (child == 0) {
    ::execve(executable.c_str(), arguments.data(), environmentPointers.data());
    ::_exit(127);
  }

  int status{};
  pid_t waited{};
  do {
    waited = ::waitpid(child, std::addressof(status), 0);
  } while (waited < 0 and errno == EINTR);

  if (waited < 0)
    return WorkerOutcome{
        .launched = true,
        .error = "waitpid() failed while observing a Nyx.Test worker",
    };

  WorkerOutcome result{
      .launched = true,
  };
  if (WIFSIGNALED(status)) {
    result.fault = NativeFault{
        .kind = NativeFaultKind::Signal,
        .code = WTERMSIG(status),
    };
  } else if (WIFEXITED(status)) {
    result.exitCode = WEXITSTATUS(status);
    if (result.exitCode != 0) {
      if (result.exitCode == 127)
        result.error = "execve() failed while starting a Nyx.Test worker";
      else
        result.fault = NativeFault{
            .kind = NativeFaultKind::Terminated,
            .code = result.exitCode,
        };
    }
  } else {
    result.fault = NativeFault{
        .kind = NativeFaultKind::Terminated,
        .code = status,
    };
  }

  if (const Option<Path> path = faultPath(launch)) {
    if (const Option<NativeFault> fault = readFaultRecord(*path))
      result.fault = fault;
  }

  return result;
}

auto installWorkerFaultHandler(const Path &path) noexcept -> bool {
  try {
    const String filename{path};
    const int descriptor = ::open(filename.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (descriptor < 0)
      return false;

    if (faultDescriptor >= 0)
      static_cast<void>(::close(faultDescriptor));
    struct sigaction action{};
    action.sa_sigaction = &faultHandler;
    action.sa_flags = SA_SIGINFO;
    sigemptyset(std::addressof(action.sa_mask));

    constexpr Array<int, 6> signals{SIGSEGV, SIGILL, SIGABRT, SIGFPE, SIGBUS, SIGTRAP};
    return std::ranges::all_of(signals, [&action](int signalNumber) -> bool {
      return ::sigaction(signalNumber, std::addressof(action), nullptr) == 0;
    });
  } catch (...) {
    return false;
  }
}

auto readFaultRecord(const Path &path) noexcept -> Option<NativeFault> {
  try {
    std::ifstream input{path, std::ios::binary};
    if (not input)
      return None;

    FaultRecord record{};
    input.read(reinterpret_cast<char *>(std::addressof(record)), sizeof(record));
    if (input.gcount() != static_cast<std::streamsize>(sizeof(record)) or record.magic != faultRecordMagic)
      return None;

    return NativeFault{
        .kind = static_cast<NativeFaultKind>(record.kind),
        .code = record.code,
        .address = record.address,
        .instruction = record.instruction,
        .symbolsAvailable = record.symbolsAvailable != 0,
    };
  } catch (...) {
    return None;
  }
}

} // namespace Nyx::Test::detail::isolation
// NOLINTEND
