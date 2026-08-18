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

struct SelectionMetadata final {
  Vec<String> include;
  Vec<String> exclude;
  Vec<String> tagsAll;
  Vec<String> tagsAny;
  Option<String> group;
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
  bool approximate{};
};

namespace detail {

inline constexpr usize measurementMedianExactThreshold{1024};

[[nodiscard]] auto currentProfileSink() noexcept -> profiling::ProfileSink & {
  return currentEnvironment()->get().profileSink();
}

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

  const bool approximate = values.size() > measurementMedianExactThreshold;
  Vec<Duration> medianValues{};
  if (approximate) {
    medianValues.reserve(measurementMedianExactThreshold);
    usize index{};
    std::ranges::for_each(values, [&medianValues, &index](Duration value) -> void {
      if (medianValues.size() < measurementMedianExactThreshold)
        medianValues.push_back(value);
      else
        medianValues[index++ % measurementMedianExactThreshold] = value;
    });
  } else {
    medianValues = values;
  }
  std::ranges::sort(values);
  std::ranges::sort(medianValues);

  Duration total = std::ranges::fold_left(values, Duration{}, std::plus<>{});
  const auto count = static_cast<Duration::rep>(std::ranges::distance(values));
  const auto mean = total / count;
  const usize halfSize = medianValues.size() / 2;
  const auto median =
      medianValues.size() % 2 == 0 ? (medianValues[halfSize - 1] + medianValues[halfSize]) / 2
                                   : medianValues[halfSize];
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
      .approximate = approximate,
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
    auto profile = profiling::profileScope(currentProfileSink(), name, location);
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
  if constexpr (build::tests) {
    if (samples == 0)
      fatal("Nyx::Test measure() requires at least one sample");

    Vec<std::chrono::steady_clock::duration> values{};
    values.reserve(samples);
    std::ranges::for_each(std::views::indices(samples), [&values, &function, name, location](usize) -> void {
      const auto started = detail::measurementNow();
      {
        auto profile = profiling::profileScope(detail::currentProfileSink(), name, location);
        static_cast<void>(std::invoke(function));
      }
      values.push_back(detail::measurementNow() - started);
    });
    return detail::summarizeMeasurements(std::move(values));
  } else {
    return {};
  }
}

/// Measures an asynchronous callable repeatedly on the active Nyx.Test run loop.
template <detail::AsyncMeasurementFunction Function>
[[nodiscard]] auto measure(StringView name,
    usize samples,
    Function function,
    std::source_location location = std::source_location::current()) -> Task<MeasurementSummary> {
  if constexpr (not build::tests) {
    return {};
  } else {
    if (samples == 0)
      fatal("Nyx::Test measure() requires at least one sample");

    return detail::measureAsync(name, samples, std::move(function), {}, 0, location);
  }
}

/// Groups every physical attempt that belongs to one logical test case.
struct TestCaseResult final {
  TestDescriptor descriptor;
  Vec<TestAttempt> attempts;
  Option<MeasurementSummary> measurement;
  usize recoveredTimeouts{};
  usize suppressedAttemptCount{};
  bool failedCase{};

  [[nodiscard]] auto passed() const noexcept -> bool;

  [[nodiscard]] auto failed() const noexcept -> bool;
};

class CaseAccumulator final {
public:
  CaseAccumulator(TestDescriptor descriptor, RetentionPolicy retention, usize maxRetainedFailures);
  auto append(AttemptOutcome outcome) -> void;
  [[nodiscard]] auto finish() && -> TestCaseResult;
  [[nodiscard]] auto identifier() const noexcept -> StringView;

private:
  TestCaseResult result_;
  RetentionPolicy retention_;
  usize maxRetainedFailures_{};
  usize retainedFailures_{};
  usize suppressedFailures_{};
  usize sampleCount_{};
  std::chrono::steady_clock::duration totalDuration_{};
  std::chrono::steady_clock::duration minimumDuration_{};
  std::chrono::steady_clock::duration maximumDuration_{};
  long double meanDuration_{};
  long double variableAccumulator_{};
  Vec<std::chrono::steady_clock::duration> sampleDurations_;
  bool approximateMedian_{};
  Vec<AttemptIndex> pendingTimeouts_;
  usize recoveredTimeouts_{};
  bool hardFailure_{};
};

struct RunReport;

inline constexpr usize maxRetainedFailuresDefault = 1024;

class RunAccumulator final {
public:
  explicit RunAccumulator(RetentionPolicy retention = RetentionPolicy::Failures,
      usize maxRetainedFailures = maxRetainedFailuresDefault,
      SelectionMetadata selection = {});
  ~RunAccumulator() = default;

  RunAccumulator(const RunAccumulator &) = delete ("RunAccumulator holds mutex state.");
  auto operator=(const RunAccumulator &) -> RunAccumulator & = delete ("RunAccumulator holds mutex state.");
  RunAccumulator(RunAccumulator &&) noexcept = delete ("RunAccumulator holds mutex state.");
  auto operator=(RunAccumulator &&) noexcept
      -> RunAccumulator & = delete ("RunAccumulator holds mutex state.");

  auto append(const TestExecution &execution) -> void;
  auto append(AttemptOutcome outcome) -> void;
  [[nodiscard]] auto finish() && -> RunReport;

private:
  std::mutex mutex_;
  RetentionPolicy retention_;
  usize maxRetainedFailures_{};
  TestSummary summary_;
  SelectionMetadata selection_;
  Option<u64> runSeed_;
  Vec<UPtr<CaseAccumulator>> cases_;
};

/// Presentation-independent result tree shared by human and machine reporters.
struct RunReport final {
  Vec<TestCaseResult> cases;
  TestSummary summary;
  SelectionMetadata selection;
  Option<u64> runSeed;
  RetentionPolicy retention{RetentionPolicy::Failures};
  usize retainedAttemptCount{};
  usize suppressedAttemptCount{};

  [[nodiscard]] auto passed() const noexcept -> bool;

  [[nodiscard]] auto failed() const noexcept -> bool;
};

struct ReporterOptions final {
  RendererOptions renderer{};
  bool showPassedTests{};
  bool showAttempts{};
  bool showSummary{true};
};

class Reporter final {
public:
  explicit Reporter(ReporterOptions options = {});

  auto addRoot(Path root) -> void;

  [[nodiscard]] static auto summarize(const RunReport &report) noexcept -> TestSummary;

  auto report(const RunReport &report, std::ostream &output) const -> TestSummary;

private:
  struct RenderState;

  [[nodiscard]] auto colorEnabled() const noexcept -> bool;

  [[nodiscard]] auto shouldRenderTrace(const TestExecution &execution) const noexcept -> bool;

  auto renderCase(const TestCaseResult &testCase,
      const SourceManager &sources,
      std::ostream &output,
      bool useColor,
      RenderState &state) const -> void;

  auto renderMeasuredCase(const TestCaseResult &testCase,
      const SourceManager &sources,
      std::ostream &output,
      RenderState &state) const -> void;

  auto renderAttempt(const TestAttempt &attempt,
      const SourceManager &sources,
      std::ostream &output,
      bool useColor,
      RenderState &state) const -> void;

  auto renderFailure(const TestExecution &execution, const SourceManager &sources, std::ostream &output) const
      -> void;

  auto renderTrace(const TestExecution &execution, std::ostream &output) const -> void;

  auto renderProfile(const TestExecution &execution, std::ostream &output) const -> void;

  auto renderMeasurement(const TestCaseResult &testCase, std::ostream &output) const -> void;

  Vec<Path> roots_;
  ReporterOptions options_{};
};

} // namespace Nyx::Test
