export module Nyx.Test:Discovery;

import std;
import Nyx.Core;
import :Annotations;
import :Context;
import :Diagnostics;
import :Environment;
import :Execution;
import :Fixtures;
import :Policies;
import :Providers;

// NOLINTBEGIN(bugprone-reserved-identifier)
export namespace Nyx::Test {

namespace detail {

template <std::meta::info Function>
consteval auto isTest() -> bool {
  return Nyx::meta::has_annotation<Test, Function>();
}
consteval auto isCase(std::meta::info annotation) -> bool {
  using namespace std::meta;

  const info type = dealias(type_of(annotation));
  return has_template_arguments(type) and template_of(type) == ^^Case;
}

template <std::meta::info Function>
consteval auto caseCount() -> usize {
  usize count{};

  template for (constexpr std::meta::info annotation : meta::annotations<Function>) {
    if constexpr (isCase(annotation))
      ++count;
  }

  return count;
}

template <std::meta::info Function>
consteval auto descriptionOf() -> StringView {
  template for (constexpr std::meta::info annotation : meta::annotations<Function>) {
    using Annotation = meta::TypeObject<annotation>;

    if constexpr (is_description_v<Annotation>)
      return std::meta::extract<Annotation>(annotation).apply();
  }

  return {};
}

template <std::meta::info Function>
consteval auto shouldPanicCount() -> usize {
  usize count{};

  template for (constexpr std::meta::info annotation : meta::annotations<Function>) {
    using Annotation = meta::TypeObject<annotation>;

    if constexpr (is_should_panic_v<Annotation>)
      ++count;
  }

  return count;
}

template <std::meta::info Function>
consteval auto timeoutCount() -> usize {
  usize count{};

  template for (constexpr std::meta::info annotation : meta::annotations<Function>) {
    using Annotation = meta::TypeObject<annotation>;

    if constexpr (std::same_as<Annotation, Timeout>)
      ++count;
  }

  return count;
}

template <std::meta::info Function>
consteval auto shouldPanicAnnotation() -> auto {
  template for (constexpr std::meta::info annotation : meta::annotations<Function>) {
    using Annotation = meta::TypeObject<annotation>;

    if constexpr (is_should_panic_v<Annotation>)
      return std::meta::extract<Annotation>(annotation);
  }

  static_assert(meta::always_false_v<meta::TypeObject<Function>>,
      "Nyx::Test could not find the [[= shouldPanic(...)]] annotation.");
}

template <std::meta::info Function>
consteval auto timeoutAnnotation() -> auto {
  template for (constexpr std::meta::info annotation : meta::annotations<Function>) {
    using Annotation = meta::TypeObject<annotation>;

    if constexpr (std::same_as<Annotation, Timeout>)
      return std::meta::extract<Annotation>(annotation);
  }

  static_assert(meta::always_false_v<meta::TypeObject<Function>>,
      "Nyx::Test could not find the [[= timeout(...)]] annotation.");
}

template <std::meta::info Function>
auto policyOf() -> TestPolicy {
  static_assert(shouldPanicCount<Function>() <= 1,
      "Nyx::Test tests may declare at most one [[= shouldPanic(...)]] annotation.");
  static_assert(timeoutCount<Function>() <= 1,
      "Nyx::Test tests may declare at most one [[= timeout(...)]] annotation.");

  TestPolicy policy{
      .trace = Nyx::meta::has_annotation<Trace, Function>(),
  };

  if constexpr (shouldPanicCount<Function>() != 0) {
    constexpr auto expected = shouldPanicAnnotation<Function>();
    policy.expectedPanic = String{expected.apply()};
  }

  if constexpr (timeoutCount<Function>() != 0) {
    constexpr Timeout limit = timeoutAnnotation<Function>();
    policy.timeout = std::chrono::duration_cast<std::chrono::steady_clock::duration>(limit.duration);
  }

  return policy;
}

template <std::meta::info Function>
auto makeTestDescriptor(usize testCase, StringView caseDescription = {}, StringView providerDescription = {})
    -> TestDescriptor {
  constexpr StringView name = meta::identifier<Function>;
  String identifier{name};

  if (not caseDescription.empty() or not providerDescription.empty()) {
    identifier.append("(");
    if (not caseDescription.empty())
      identifier.append(caseDescription);

    if (not caseDescription.empty() and not providerDescription.empty())
      identifier.append(", ");

    if (not providerDescription.empty())
      identifier.append(providerDescription);

    identifier.append(")");
  }

  return TestDescriptor{
      .identifier = std::move(identifier),
      .location = std::meta::source_location_of(Function),
      .name = String{name},
      .description = String{descriptionOf<Function>()},
      .testCase = testCase,
      .policy = policyOf<Function>(),
  };
}

template <std::meta::info Function>
auto appendProviderDescriptors(Vec<TestDescriptor> &descriptor,
    StringView caseDescription,
    usize &testCaseIndex) -> void {
  const usize providerCount = detail::forEachProviderCombination<Function>(
      [&descriptor, caseDescription, &testCaseIndex](const auto &...providerValues) -> void {
        const String providerDescription = detail::providerDescription<Function>(providerValues...);
        descriptor.push_back(
            makeTestDescriptor<Function>(testCaseIndex, caseDescription, StringView{providerDescription}));
        ++testCaseIndex;
      });

  if (providerCount != 0)
    return;

  const String providerDescription = detail::missingProviderDescription<Function>();
  descriptor.push_back(
      makeTestDescriptor<Function>(testCaseIndex, caseDescription, StringView{providerDescription}));
  ++testCaseIndex;
}

template <std::meta::info Function, std::meta::info Annotation>
auto appendCaseDescriptions(Vec<TestDescriptor> &descriptors, usize &testCaseIndex) -> void {
  using Ann = meta::TypeObject<Annotation>;
  constexpr Ann testCase = std::meta::extract<Ann>(Annotation);
  const String caseDescription = testCase.describe();

  appendProviderDescriptors<Function>(descriptors, caseDescription, testCaseIndex);
}

template <std::meta::info Function>
auto appendDescriptors(Vec<TestDescriptor> &descriptors) -> void {
  usize testCaseIndex{};

  if constexpr (caseCount<Function>() == 0) {
    appendProviderDescriptors<Function>(descriptors, {}, testCaseIndex);
    return;
  }

  template for (constexpr std::meta::info annotation : meta::annotations<Function>) {
    if constexpr (isCase(annotation))
      appendCaseDescriptions<Function, annotation>(descriptors, testCaseIndex);
  }
}

template <class CaseType>
[[nodiscard]] auto caseValues(const CaseType &testCase) -> auto {
  return testCase.apply([](const auto &...values) -> auto { return std::make_tuple(values...); });
}

template <std::meta::info Function>
auto noProviderExecution(TestDescriptor descriptor) -> TestExecution {
  constexpr std::source_location location = detail::firstProviderLocation<Function>();
  return run(std::move(descriptor), [location] -> void {
    const Option<Ref<TestEnvironment>> environment = currentEnvironment();
    if (not environment)
      throw std::logic_error{"Nyx::Test could not report an empty provider without an active environment"};

    Diagnostic diagnostic = makeDiagnostic(DiagnosticCode::ProviderProducedNoValues, location);
    diagnostic.details.spans.front().label = "provider";
    diagnostic.addNote("no reflected parameter-provider values were produced");
    environment->get().recordError(std::move(diagnostic));
  });
}

template <std::meta::info Namespace, std::meta::info Function, class CaseValues>
auto appendProviderExecutions(Vec<TestExecution> &executions,
    FixtureScope<Namespace> &suiteFixtures,
    const CaseValues &caseValues,
    StringView caseDescription,
    usize &testCaseIndex) -> void {
  const usize providerCount = detail::forEachProviderCombination<Function>(
      [&executions, &suiteFixtures, &caseValues, caseDescription, &testCaseIndex](
          const auto &...providerValues) -> void {
        const String providerDescription = detail::providerDescription<Function>(providerValues...);
        const auto providerTuple = std::make_tuple(providerValues...);
        executions.push_back(
            run(makeTestDescriptor<Function>(testCaseIndex, caseDescription, StringView{providerDescription}),
                [caseValues, providerTuple, &suiteFixtures](const Context &context) -> decltype(auto) {
                  FixtureScope<Namespace> testFixtures{};
                  return detail::invokeTest<Namespace, Function>(
                      context, testFixtures, suiteFixtures, caseValues, providerTuple);
                }));
        ++testCaseIndex;
      });

  if (providerCount != 0)
    return;

  const String providerDescription = detail::missingProviderDescription<Function>();
  executions.push_back(noProviderExecution<Function>(
      makeTestDescriptor<Function>(testCaseIndex, caseDescription, StringView{providerDescription})));
  ++testCaseIndex;
}

template <std::meta::info Namespace, std::meta::info Function, std::meta::info Annotation>
auto appendCaseExecutions(Vec<TestExecution> &executions,
    FixtureScope<Namespace> &suiteFixtures,
    usize &testCaseIndex) -> void {
  using Ann = meta::TypeObject<Annotation>;
  constexpr Ann testCase = std::meta::extract<Ann>(Annotation);
  const String caseDescription = testCase.describe();
  const auto caseValueTuple = caseValues(testCase);

  appendProviderExecutions<Namespace, Function>(
      executions, suiteFixtures, caseValueTuple, caseDescription, testCaseIndex);
}

template <std::meta::info Namespace, std::meta::info Function>
auto appendExecutions(Vec<TestExecution> &executions, FixtureScope<Namespace> &suiteFixtures) -> void {
  usize testCaseIndex{};

  if constexpr (caseCount<Function>() == 0) {
    const Tuple<> caseValues{};
    appendProviderExecutions<Namespace, Function>(executions, suiteFixtures, caseValues, {}, testCaseIndex);
    return;
  }

  template for (constexpr std::meta::info annotation : meta::annotations<Function>) {
    if constexpr (isCase(annotation))
      appendCaseExecutions<Namespace, Function, annotation>(executions, suiteFixtures, testCaseIndex);
  }
}

} // namespace detail

/// Returns one descriptor per Case/provider combination in declaration order.
template <std::meta::info Namespace>
[[nodiscard]]
auto discover() -> Vec<TestDescriptor> {
  Vec<TestDescriptor> descriptors{};

  template for (constexpr std::meta::info member :
      meta::members<Namespace, meta::AccessContext::unchecked()>) {
    if constexpr (std::meta::is_function(member) and detail::isTest<member>())
      detail::appendDescriptors<member>(descriptors);
  }

  return descriptors;
}

/// Executes all reflected tests and their respective declarative Case annotations.
template <std::meta::info Namespace>
[[nodiscard]] auto runAll() -> Vec<TestExecution> {
  static_assert(detail::fixtureDeclarationsAreValid<Namespace>());

  Vec<TestExecution> executions{};
  FixtureScope<Namespace> suiteFixtures{};

  template for (constexpr std::meta::info member :
      meta::members<Namespace, meta::AccessContext::unchecked()>) {
    if constexpr (std::meta::is_function(member) and detail::isTest<member>())
      detail::appendExecutions<Namespace, member>(executions, suiteFixtures);
  }

  return executions;
}

} // namespace Nyx::Test
// NOLINTEND(bugprone-reserved-identifier)
