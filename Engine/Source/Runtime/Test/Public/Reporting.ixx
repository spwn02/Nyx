export module Nyx.Test:Reporting;

import std;
import Nyx.Core;
import :Execution;
import :Render;

export namespace Nyx::Test {

struct TestSummary final {
  usize testCount{};
  usize caseCount{};
  usize attemptCount{};
  usize sampleCount{};
  usize retryCount{};
  usize warmupCount{};
  usize recoveredCount{};
  usize passedCaseCount{};
  usize failedCaseCount{};
  usize passedCount{};
  usize failedCount{};
  usize assertionCount{};
  usize failedAssertionCount{};
  usize errorCount{};
  std::chrono::steady_clock::duration duration{};
  std::chrono::steady_clock::duration wallDuration{};

  [[nodiscard]] auto passed() const noexcept -> bool;

  [[nodiscard]] auto failed() const noexcept -> bool;
};

/// Groups every physical attempt that belongs to one logical test case.
struct TestAttempt final {
  TestExecution execution;
  AttemptIndex index{};
  bool warmup{};
};

/// Describes the scheduler-time distribution of measured samples.
struct MeasurementSummary final {
  usize sampleCount{};
  std::chrono::steady_clock::duration total{};
  std::chrono::steady_clock::duration minimum{};
  std::chrono::steady_clock::duration maximum{};
  std::chrono::steady_clock::duration mean{};
  std::chrono::steady_clock::duration median{};
  std::chrono::steady_clock::duration deviation{};
};

namespace detail {

/// Returns scheduler time when a test coroutine is active and host time otherwise.
[[nodiscard]] auto measurementNow() noexcept -> std::chrono::steady_clock::time_point {
  if (RunLoop *const runLoop = currentRunLoop())
    return runLoop->now();

  return std::chrono::steady_clock::now();
}

/// Reduces ordered sample durations into the stable public measurement summary.
[[nodiscard]] auto summarizeMeasurements(Vec<std::chrono::steady_clock::duration> values)
    -> MeasurementSummary {
  using Duration = std::chrono::steady_clock::duration;

  if (values.empty())
    return {};

  std::ranges::sort(values);

  Duration total = std::ranges::fold_left(values, Duration{}, std::plus<>{});
  const auto count = static_cast<Duration::rep>(std::ranges::distance(values));
  const auto mean = total / count;
  const usize halfSize = values.size() / 2;
  const auto median =
      values.size() % 2 == 0 ? (values[halfSize - 1] + values[halfSize]) / 2 : values[halfSize];
  const auto meanCount = static_cast<long double>(mean.count());
  long double variance{};
  std::ranges::for_each(values, [&variance, meanCount](const Duration value) -> void {
    const auto delta = static_cast<long double>(value.count()) - meanCount;
    variance += delta * delta;
  });
  variance /= static_cast<long double>(values.size());
  const auto deviation = static_cast<Duration::rep>(std::sqrt(variance));

  return MeasurementSummary{
      .sampleCount = values.size(),
      .total = total,
      .minimum = values.front(),
      .maximum = values.back(),
      .mean = mean,
      .median = median,
      .deviation = Duration{deviation},
  };
}

template <class Function>
concept AsyncMeasurementFunction =
    std::invocable<Function &> and is_task_return_v<std::remove_cvref_t<std::invoke_result_t<Function &>>>;

template <class Function>
auto measureAsync(StringView name,
    usize samples,
    Function function,
    Vec<std::chrono::steady_clock::duration> values,
    usize index,
    std::source_location location) -> Task<MeasurementSummary> {
  if (index >= samples)
    co_return summarizeMeasurements(std::move(values));

  const auto started = measurementNow();
  {
    auto proifle = profiling::profileScope(name, location);
    using Return = std::remove_cvref_t<std::invoke_result_t<Function &>>;
    if constexpr (std::same_as<Return, Task<void>>) {
      co_await std::invoke(function);
    } else {
      static_cast<void>(co_await std::invoke(function));
    }
  }
  values.push_back(measurementNow() - started);
  co_return co_await measureAsync(name, samples, std::move(function), std::move(values), index + 1, location);
}

} // namespace detail

/// Measures a synchronous callable repeatedly using scheduler time when available.
template <class Function>
  requires(std::invocable<Function &> and
           not detail::is_task_return_v<std::remove_cvref_t<std::invoke_result_t<Function &>>>)
[[nodiscard]] auto measure(StringView name,
    usize samples,
    Function function,
    std::source_location location = std::source_location::current()) -> MeasurementSummary {
  if (samples == 0)
    throw std::invalid_argument{"Nyx::Test measure() requires at least one sample"};

  Vec<std::chrono::steady_clock::duration> values{};
  values.reserve(samples);
  std::ranges::for_each(std::views::indices(samples), [&values, &function, name, location](usize) -> void {
    const auto started = detail::measurementNow();
    {
      auto profile = profiling::profileScope(name, location);
      static_cast<void>(std::invoke(function));
    }
    values.push_back(detail::measurementNow() - started);
  });
  return detail::summarizeMeasurements(std::move(values));
}

/// Measures an asynchronous callable repeatedly on the active Nyx.Test run loop.
template <detail::AsyncMeasurementFunction Function>
[[nodiscard]] auto measure(StringView name,
    usize samples,
    Function function,
    std::source_location location = std::source_location::current()) -> Task<MeasurementSummary> {
  if (samples == 0)
    throw std::invalid_argument{"Nyx::Test measure() requires at least one sample"};

  return detail::measureAsync(name, samples, std::move(function), {}, 0, location);
}

/// Groups every physical attempt that belongs to one logical test case.
struct TestCaseResult final {
  TestDescriptor descriptor;
  Vec<TestAttempt> attempts;
  Option<MeasurementSummary> measurement;
  usize recoveredTimeouts{};

  [[nodiscard]] auto passed() const noexcept -> bool;

  [[nodiscard]] auto failed() const noexcept -> bool;
};

/// Presentation-independent result tree shared by human and machine reporters.
struct RunReport final {
  Vec<TestCaseResult> cases;

  [[nodiscard]] auto passed() const noexcept -> bool;

  [[nodiscard]] auto failed() const noexcept -> bool;
};

struct ReporterOptions final {
  RendererOptions renderer{};
  bool showPassedTests{};
  bool showSummary{true};
};

class Reporter final {
public:
  explicit Reporter(ReporterOptions options = {});

  auto addRoot(Path root) -> void;

  [[nodiscard]] static auto makeReport(Span<const TestExecution> executions) -> RunReport;

  [[nodiscard]] static auto summarize(Span<const TestExecution> executions) noexcept -> TestSummary;

  [[nodiscard]] static auto summarize(const RunReport &report) noexcept -> TestSummary;

  auto report(Span<const TestExecution> executions, std::ostream &output) const -> TestSummary;

private:
  [[nodiscard]] auto colorEnabled() const noexcept -> bool;

  [[nodiscard]] auto shouldRenderTrace(const TestExecution &execution) const noexcept -> bool;

  auto renderFailure(const TestExecution &execution, const SourceManager &sources, std::ostream &output) const
      -> void;

  auto renderTrace(const TestExecution &execution, std::ostream &output) const -> void;

  auto renderProfile(const TestExecution &execution, std::ostream &output) const -> void;

  auto renderMeasurement(const TestCaseResult &testCase, std::ostream &output) const -> void;

  Vec<Path> roots_;
  ReporterOptions options_{};
};

} // namespace Nyx::Test
