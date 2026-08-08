export module Nyx.Test:Runner;

import std;
import Nyx.Core;
import :Execution;

export namespace Nyx::Test {

/// Configures execution of independent reflected test cases.
///
/// jobs == 1 preserves declaration-order, single-threaded dispatch. jobs == 0 selects the available logical
/// processor count, falling back to one worker when it cannot be determined. Higher values cap dispatch to
/// that many workers. Each case retains its own deterministic Task<T> run loop.
struct RunOptions final {
  usize jobs{1};

  /// Selects the independent scheduler clock supplied to every test execution in this run.
  TimeMode timeMode{TimeMode::Real};
};

namespace detail {

/// One fully expanded reflected Case/provider combination ready for indepndent execution.
struct PlannedCase final {
  TestDescriptor descriptor{};
  std::move_only_function<TestExecution(TestDescriptor, TimeMode)> execute;
};

/// Owns opaque suite-local state while its PlannedCase values execute.
///
/// Discovery materializes each suite's FixtureScope here. The storage is type-erased because every reflected
/// suite has a distinct FixtureScope<Scope> type, while RunSession schedules all of their cases together.
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
/// Hive preserves SuiteState addresses as discovery appends suites. PlannedCase stays contiguous for indexed
/// worker dispatch. The member order deliberately destroys cases before suite fixture state.
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

  [[nodiscard]] auto takePlannedCases() -> Vec<PlannedCase> {
    return std::move(plannedCases_);
  }

private:
  Hive<SuiteState> suites_;
  Vec<PlannedCase> plannedCases_;
};

[[nodiscard]] auto executePlannedCases(RunSession &session, const RunOptions &options) -> Vec<TestExecution>;

} // namespace detail

} // namespace Nyx::Test
