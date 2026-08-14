export module Nyx.Test:Runner;

import std;
import Nyx.Core;
import :Execution;

export namespace Nyx::Test {

/// Selects the safety boundary used for one logical test case.
///
/// ProcessPerCase is the safe DevelopmentTests default: a native fault terminates only the worker that owns
/// the current logical case. InProcess is intentionally explicit and is useful for trusted microbenchmarks.
enum class[[= debug::derive]] CrashIsolation : u8 {
  InProcess[[= debug::rename("in_process")]],
  ProcessPerCase[[= debug::rename("process_per_case")]],
};

/// Determines how expanded test cases are arranged before dispatch.
enum class ExecutionOrder : u8 {
  Declaration,
  Shuffled,
};

/// Configures execution of independent reflected test cases.
///
/// jobs == 1 selects single-threaded dispatch. Declaration order remains the default and can be replaced by
/// ExecutionOrder::Shuffled. jobs == 0 selects the available logical processor count, falling back to one
/// worker when it cannot be determined. Higher values cap dispatch to that many workers. Each case retains
/// its own deterministic Task<T> run loop.
struct RunOptions final {
  usize jobs{1};

  /// Selects the independent scheduler clock supplied to every test execution in this run.
  TimeMode timeMode{TimeMode::Real};

  /// Captures trace events for every case; the default reporter renders them only for failures.
  TraceMode traceMode{TraceMode::ForcedFailures};

  /// Preserves declaration order by default; shuffled order is reproducible with seed.
  ExecutionOrder order{ExecutionOrder::Declaration};

  /// Runs every selected case this many times. Zero is invalid.
  usize repeat{1};

  /// Stops dispatch after the first failed execution. Already-running parallel cases are allowed to finish.
  bool failFast{};

  /// Optional root seed for ordering and per-case Context::seed derivation.
  Option<u64> seed;

  /// Contains native faults at the logical-case process boundary by default.
  CrashIsolation isolation{CrashIsolation::ProcessPerCase};
};

namespace detail {

struct WorkerRequest;

/// Describes the input bindings retained by one immutable invocation factory.
struct InvocationCapabilities final {
  bool context{};
  bool caseValues{};
  bool providerValues{};
  bool fixtures{};
};

/// Carries the per-attempt execution mode into an immutable invocation factory.
struct InvocationRequest final {
  Ref<const TestDescriptor> descriptor;
  TimeMode timeMode{TimeMode::Real};

  explicit InvocationRequest(const TestDescriptor &descriptor, TimeMode timeMode = TimeMode::Real)
      : descriptor(descriptor)
      , timeMode(timeMode) {
  }
};

/// Type-erased, immutable execution entry point for one expanded test case.
///
/// The factory owns no runner state. Concrete factories retain only immutable Case/provider values and a
/// non-owning reference to the suite fixture scope owned by RunSession.
class InvocationFactory {
public:
  virtual ~InvocationFactory() noexcept = default;

  InvocationFactory(const InvocationFactory &) = delete (
      "InvocationFactory owns immutable invocation state and cannot be copied.");
  auto operator=(const InvocationFactory &) -> InvocationFactory & = delete (
      "InvocationFactory owns immutable invocation state and cannot be copied.");
  InvocationFactory(InvocationFactory &&) noexcept = delete (
      "InvocationFactory owns immutable invocation state and cannot be copied.");
  auto operator=(InvocationFactory &&) noexcept -> InvocationFactory & = delete (
      "InvocationFactory owns immutable invocation state and cannot be copied.");

  [[nodiscard]] virtual auto invoke(const InvocationRequest &request) const -> TestExecution = 0;

protected:
  InvocationFactory() noexcept = default;
};

/// One fully expanded reflected Case/provider combination ready for independent execution.
///
/// A planned case is move-only because it owns its immutable factory. Once materialized in a RunSession, its
/// descriptor, capabilities, and factory are observed through const accessors only.
class PlannedCase final {
public:
  explicit PlannedCase(TestDescriptor descriptor,
      InvocationCapabilities capabilities,
      UPtr<const InvocationFactory> factory)
      : descriptor_(std::move(descriptor))
      , capabilities_(capabilities)
      , factory_(std::move(factory)) {
  }

  PlannedCase(const PlannedCase &) = delete ("PlannedCase owns its immutable invocation factory.");
  auto operator=(const PlannedCase &)
      -> PlannedCase & = delete ("PlannedCase owns its immutable invocation factory.");
  PlannedCase(PlannedCase &&) noexcept = default;
  auto operator=(PlannedCase &&) noexcept -> PlannedCase & = default;
  ~PlannedCase() noexcept = default;

  [[nodiscard]] auto descriptor() const noexcept -> const TestDescriptor & {
    return descriptor_;
  }

  [[nodiscard]] auto capabilities() const noexcept -> const InvocationCapabilities & {
    return capabilities_;
  }

  [[nodiscard]] auto invoke(const InvocationRequest &request) const -> TestExecution {
    return factory_->invoke(request);
  }

private:
  TestDescriptor descriptor_;
  InvocationCapabilities capabilities_;
  UPtr<const InvocationFactory> factory_;
};

/// Owns opaque suite-local state while its PlannedCase values execute.
///
/// Discovery materializes each suite's FixtureScope here. The storage is type-erased because every
/// reflected suite has a distinct FixtureScope<Scope> type, while RunSession schedules all of their cases
/// together.
class SuiteState final {
private:
  class Storage {
  public:
    virtual ~Storage() noexcept = default;

    Storage(const Storage &) = delete ("SuiteState storage has unique ownership.");
    auto operator=(const Storage &) -> Storage & = delete ("SuiteState storage has unique ownership.");
    Storage(Storage &&) noexcept = delete ("SuiteState storage has unique ownership.");
    auto operator=(Storage &&) noexcept -> Storage & = delete ("SuiteState storage has unique ownership.");

  protected:
    explicit Storage() = default;
  };

  template <class Value>
  class ValueStorage final : public Storage {
  public:
    template <class... Arguments>
    explicit ValueStorage(Arguments &&...arguments)
        : value_(std::forward<Arguments>(arguments)...) {
    }

    [[nodiscard]] auto value() noexcept -> Value & {
      return value_;
    }

  private:
    Value value_;
  };

public:
  explicit SuiteState(StringView scope) noexcept
      : scope_(scope) {
  }
  ~SuiteState() noexcept = default;

  SuiteState(const SuiteState &) = delete ("SuiteState owns fixture lifetime.");
  auto operator=(const SuiteState &) -> SuiteState & = delete ("SuiteState owns fixture lifetime.");
  SuiteState(SuiteState &&) noexcept = delete ("SuiteState owns fixture lifetime.");
  auto operator=(SuiteState &&) noexcept -> SuiteState & = delete ("SuiteState owns fixture lifetime.");

  [[nodiscard]] auto scope() const noexcept -> StringView {
    return scope_;
  }

  template <class Value, class... Args>
  [[nodiscard]] auto emplace(Args &&...args) -> Value & {
    auto storage = std::make_unique<ValueStorage<Value>>(std::forward<Args>(args)...);
    Value &value = storage->value();
    storage_ = std::move(storage);
    return value;
  }

private:
  StringView scope_;
  UPtr<Storage> storage_;
};

/// Collects every selected suite and its independently schedulable cases for one run.
///
/// Hive preserves SuiteState addresses as discovery appends suites. PlannedCase stays contiguous for
/// indexed worker dispatch. The member order deliberately destroys cases before suite fixture state.
class RunSession final {
public:
  RunSession() = default;
  ~RunSession() noexcept = default;

  RunSession(const RunSession &) = delete ("RunSession owns execution state.");
  auto operator=(const RunSession &) -> RunSession & = delete ("RunSession owns execution state.");
  RunSession(RunSession &&) noexcept = delete ("RunSession owns execution state.");
  auto operator=(RunSession &&) noexcept -> RunSession & = delete ("RunSession owns execution state.");

  [[nodiscard]] auto appendSuite(StringView scope) -> SuiteState & {
    return *suites_.emplace(scope);
  }

  [[nodiscard]] auto plannedCases() noexcept -> Vec<PlannedCase> & {
    return plannedCases_;
  }

  /// Reserves contiguous storage before the reflected executable plan is materialized.
  auto reservePlannedCases(usize count) -> void {
    plannedCases_.reserve(count);
  }

  [[nodiscard]] auto takePlannedCases() -> Vec<PlannedCase> {
    return std::move(plannedCases_);
  }

private:
  Hive<SuiteState> suites_;
  Vec<PlannedCase> plannedCases_;
};

[[nodiscard]] auto executePlannedCases(RunSession &session, const RunOptions &options) -> Vec<TestExecution>;

/// Executes one worker request after the child process has rebuilt the reflected plan.
auto executeWorkerCase(RunSession &session, const WorkerRequest &request, RunOptions options) -> void;

} // namespace detail

} // namespace Nyx::Test
