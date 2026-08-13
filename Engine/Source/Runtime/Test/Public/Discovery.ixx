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
import :Runner;
import :Task;

// NOLINTBEGIN(bugprone-reserved-identifier)
export namespace Nyx::Test {

/// Includes tests declared in nested namespaces below a registered namespace.
struct Recursive final {};

inline constexpr Recursive recursive{};

/// Enables discovery of static member functions when the registered scope is a class. Instance member tests
/// require object-lifetime model and remain a future goal.
/// TODO: Extend the functionality to support non-static member functions.
struct StaticMemberFunctions final {};

inline constexpr StaticMemberFunctions staticMemberFunctions{};

/// Describes which reflected test cases participate in a list or run operation.
///
/// Include patterns us '*' and '?' globs over fully qualified identifiers. Excludes always win. tagsAll
/// requires every requested tag; tagsAny requires at least one requested tag when it is non-empty.
struct TestSelection final {
  Vec<String> include;
  Vec<String> exclude;
  Vec<String> tagsAll;
  Vec<String> tagsAny;
  Option<String> group;

  [[nodiscard]] auto matches(const TestDescriptor &descriptor) const -> bool;
};

/// Immutable compile-time metadata emitted once for every registered suite.
struct SuiteEntry final {
  using DescriptorFactory = Vec<TestDescriptor> (*)();
  using PlanFactory = void (*)(detail::RunSession &);

  StringView scope;
  std::source_location location;
  DescriptorFactory describe;
  PlanFactory plan;
};

namespace detail {

[[nodiscard]] auto filterDescriptors(Vec<TestDescriptor> descriptors, const TestSelection &selection)
    -> Vec<TestDescriptor>;

auto filterPlannedCases(RunSession &session, const TestSelection &selection) -> void;

template <class Option>
concept DiscoveryOption = std::same_as<std::remove_cvref_t<Option>, Recursive> or
                          std::same_as<std::remove_cvref_t<Option>, StaticMemberFunctions>;

template <class... Options>
struct DiscoveryConfiguration final {
  static constexpr usize recursiveCount_ =
      (usize{} + ... + (std::same_as<Options, Recursive> ? usize{1} : usize{}));
  static constexpr usize staticMemberFunctionsCount_ =
      (usize{} + ... + (std::same_as<Options, StaticMemberFunctions> ? usize{1} : usize{}));

  static_assert(recursiveCount_ <= 1, "Nyx::Test discover() accepts the reqursive option at most once.");
  static_assert(staticMemberFunctionsCount_ <= 1,
      "Nyx::Test discover() accepts the staticMemberFunctions option at most once.");

  static constexpr bool recursive_{recursiveCount_ != 0};
  static constexpr bool staticMemberFunctions_{staticMemberFunctionsCount_ != 0};
};

/// Appends immutable suite metadata to the process-wide automatic catalog.
///
/// This is intentionally a runtime operation. discover() materializes a registration anchor at compile time;
/// the anchor invokes this function during ordinary static initialization before main().
auto appendRegisteredSuite(const SuiteEntry &suite) -> void;

/// Registers a scope that may need to be reconstructed by an exec'd test worker.
auto appendWorkerSuite(const SuiteEntry &suite) -> void;

template <std::meta::info Entity>
consteval auto appendQualifiedName(String &result) -> void {
  if constexpr (Entity != ^^::) {
    constexpr std::meta::info parent = std::meta::parent_of(Entity);
    if constexpr (parent != ^^::) {
      appendQualifiedName<parent>(result);
      if (not result.empty())
        result.append("::");
    }

    result.append(std::meta::identifier_of(Entity));
  }
}

template <std::meta::info Entity>
consteval auto qualifiedNameOf() -> StringView {
  if constexpr (Entity == ^^::) {
    return "<global>";
  } else {
    String result{};
    appendQualifiedName<Entity>(result);
    return StringView{std::define_static_string(StringView{result}), result.size()};
  }
}

template <std::meta::info Scope>
consteval auto scopeLocationOf() -> std::source_location {
  if constexpr (Scope == ^^::) {
    return {};
  } else {
    return std::meta::source_location_of(Scope);
  }
}

template <std::meta::info Function>
consteval auto isTest() -> bool {
  if constexpr (not std::meta::is_function(Function))
    return false;
  else
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

template <class Annotation, std::meta::info Function>
consteval auto annotationCount() -> usize {
  usize count{};

  template for (constexpr std::meta::info annotation : meta::annotations<Function>) {
    if constexpr (std::same_as<meta::TypeObject<annotation>, Annotation>)
      ++count;
  }

  return count;
}

template <std::meta::info Function>
consteval auto repeatCount() -> usize {
  return annotationCount<Repeat, Function>();
}

template <std::meta::info Function>
consteval auto warmupCount() -> usize {
  return annotationCount<Warmup, Function>();
}

template <std::meta::info Function>
consteval auto retryCount() -> usize {
  return annotationCount<Retry, Function>();
}

template <std::meta::info Function>
consteval auto groupCount() -> usize {
  usize count{};

  template for (constexpr std::meta::info annotation : meta::annotations<Function>) {
    using Annotation = meta::TypeObject<annotation>;

    if constexpr (is_group_v<Annotation>)
      ++count;
  }

  return count;
}

template <std::meta::info Function>
auto metadataOf() -> TestMetadata {
  static_assert(
      groupCount<Function>() <= 1, "Nyx::Test tests may declare at most one [[= group(...)]] annotation.");

  TestMetadata metadata{};
  const auto appendTag = [&metadata](StringView tagName) -> void {
    const bool alreadyPresent = std::ranges::any_of(
        metadata.tags, [tagName](const String &candidate) -> bool { return candidate == tagName; });
    if (not alreadyPresent)
      metadata.tags.emplace_back(tagName);
  };

  template for (constexpr std::meta::info annotation : meta::annotations<Function>) {
    using Annotation = meta::TypeObject<annotation>;

    if constexpr (is_group_v<Annotation>) {
      constexpr Annotation groupAnnotation = std::meta::extract<Annotation>(annotation);
      metadata.group = String{groupAnnotation.apply()};
    } else if constexpr (is_tag_v<Annotation>) {
      constexpr Annotation tagAnnotation = std::meta::extract<Annotation>(annotation);
      tagAnnotation.apply([&appendTag](const auto &...tags) -> void { (appendTag(tags.apply()), ...); });
    }
  }

  return metadata;
}

template <std::meta::info Function>
auto policyOf() -> TestPolicy {
  static_assert(shouldPanicCount<Function>() <= 1,
      "Nyx::Test tests may declare at most one [[= shouldPanic(...)]] annotation.");
  static_assert(timeoutCount<Function>() <= 1,
      "Nyx::Test tests may declare at most one [[= timeout(...)]] annotation.");
  static_assert(
      repeatCount<Function>() <= 1, "Nyx::Test tests may declare at most one [[= repeat(...)]] annotation.");
  static_assert(
      warmupCount<Function>() <= 1, "Nyx::Test tests may declare at most one [[= warmup(...)]] annotation.");
  static_assert(
      retryCount<Function>() <= 1, "Nyx::Test tests may declare at most one [[= retry(...)]] annotation.");

  constexpr bool isolated = Nyx::meta::has_annotation<Isolated, Function>();
  constexpr bool parent = Nyx::meta::has_annotation<Parent, Function>();
  static_assert(not(isolated and parent), "Nyx::Test tests cannot combine [[= isolated]] and [[= parent]]");

  TestPolicy policy{
      .trace = Nyx::meta::has_annotation<Trace, Function>(),
      .isolated = isolated,
      .parent = parent,
  };

  template for (constexpr std::meta::info annotation : meta::annotations<Function>) {
    using Annotation = meta::TypeObject<annotation>;

    if constexpr (is_should_panic_v<Annotation>) {
      constexpr Annotation expected = std::meta::extract<Annotation>(annotation);
      policy.expectedPanic = String{expected.apply()};
    } else if constexpr (std::same_as<Annotation, Timeout>) {
      constexpr auto limit = std::meta::extract<Timeout>(annotation);
      policy.timeout = std::chrono::duration_cast<std::chrono::steady_clock::duration>(limit.apply());
    } else if constexpr (std::same_as<Annotation, Repeat>) {
      policy.repeat = std::meta::extract<Repeat>(annotation).apply();
    } else if constexpr (std::same_as<Annotation, Warmup>) {
      policy.warmup = std::meta::extract<Warmup>(annotation).apply();
    } else if constexpr (std::same_as<Annotation, Retry>) {
      policy.retry = std::meta::extract<Retry>(annotation).apply();
    }
  }

  return policy;
}

template <std::meta::info Function>
auto makeTestDescriptor(usize testCase, StringView caseDescription = {}, StringView providerDescription = {})
    -> TestDescriptor {
  constexpr StringView name = meta::identifier<Function>;
  String identifier{qualifiedNameOf<Function>()};

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
      .metadata = metadataOf<Function>(),
  };
}

template <std::meta::info Function>
auto appendProviderDescriptors(Vec<TestDescriptor> &descriptors,
    StringView caseDescription,
    usize &testCaseIndex) -> void {
  const usize providerCount = detail::forEachProviderCombination<Function>(
      [&descriptors, caseDescription, &testCaseIndex](const auto &...providerValues) -> void {
        const String providerDescription = detail::providerDescription<Function>(providerValues...);
        descriptors.push_back(
            makeTestDescriptor<Function>(testCaseIndex, caseDescription, StringView{providerDescription}));
        ++testCaseIndex;
      });

  if (providerCount != 0)
    return;

  const String providerDescription = detail::missingProviderDescription<Function>();
  descriptors.push_back(
      makeTestDescriptor<Function>(testCaseIndex, caseDescription, StringView{providerDescription}));
  ++testCaseIndex;
}

template <std::meta::info Function, std::meta::info Annotation>
auto appendCaseDescriptors(Vec<TestDescriptor> &descriptors, usize &testCaseIndex) -> void {
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
      appendCaseDescriptors<Function, annotation>(descriptors, testCaseIndex);
  }
}

template <class CaseType>
[[nodiscard]] auto caseValues(const CaseType &testCase) -> auto {
  return testCase.apply([](const auto &...values) -> auto { return std::make_tuple(values...); });
}

template <std::meta::info Function>
auto noProviderExecution(TestDescriptor descriptor, TimeMode timeMode) -> TestExecution {
  constexpr std::source_location location = detail::firstProviderLocation<Function>();
  return run(
      std::move(descriptor),
      [location] -> void {
        const Option<Ref<TestEnvironment>> environment = currentEnvironment();
        if (not environment)
          fatal("Nyx::Test could not report an empty provider without an active environment");

        Diagnostic diagnostic = makeDiagnostic(DiagnosticCode::ProviderProducedNoValues, location);
        diagnostic.details.spans.front().label = "provider";
        diagnostic.addNote("no reflected parameter-provider values were produced");
        environment->get().recordError(std::move(diagnostic));
      },
      timeMode);
}

template <std::meta::info Namespace, std::meta::info Function, class CaseValues>
auto appendProviderWorkItems(Vec<detail::PlannedCase> &plannedCases,
    FixtureScope<Namespace> &suiteFixtures,
    const CaseValues &caseValues,
    StringView caseDescription,
    usize &testCaseIndex) -> void {
  const usize providerCount = detail::forEachProviderCombination<Function>(
      [&plannedCases, &suiteFixtures, &caseValues, &caseDescription, &testCaseIndex](
          const auto &...providerValues) -> void {
        const String providerDescription = detail::providerDescription<Function>(providerValues...);
        const auto providerTuple = std::make_tuple(providerValues...);
        const TestDescriptor descriptor =
            makeTestDescriptor<Function>(testCaseIndex, caseDescription, StringView{providerDescription});
        plannedCases.push_back(detail::PlannedCase{
            .descriptor = descriptor,
            .execute = [caseValues, providerTuple, &suiteFixtures](
                           TestDescriptor descriptor, TimeMode timeMode) -> TestExecution {
              return run(
                  std::move(descriptor),
                  [caseValues, providerTuple, &suiteFixtures](const Context &context) -> decltype(auto) {
                    return detail::invokeWithFixtures<Namespace, Function>(
                        context, suiteFixtures, caseValues, providerTuple);
                  },
                  timeMode);
            },
        });
        ++testCaseIndex;
      });

  if (providerCount != 0)
    return;

  const String providerDescription = detail::missingProviderDescription<Function>();
  const TestDescriptor descriptor =
      makeTestDescriptor<Function>(testCaseIndex, caseDescription, StringView{providerDescription});
  plannedCases.push_back(detail::PlannedCase{
      .descriptor = descriptor, .execute = [](TestDescriptor descriptor, TimeMode timeMode) -> TestExecution {
        return noProviderExecution<Function>(std::move(descriptor), timeMode);
      }});
  ++testCaseIndex;
}

template <std::meta::info Namespace, std::meta::info Function, std::meta::info Annotation>
auto appendCaseWorkItems(Vec<detail::PlannedCase> &plannedCases,
    FixtureScope<Namespace> &suiteFixtures,
    usize &testCaseIndex) -> void {
  using Ann = meta::TypeObject<Annotation>;
  constexpr Ann testCase = std::meta::extract<Ann>(Annotation);
  const String caseDescription = testCase.describe();
  const auto caseValueTuple = caseValues(testCase);

  appendProviderWorkItems<Namespace, Function>(
      plannedCases, suiteFixtures, caseValueTuple, caseDescription, testCaseIndex);
}

template <std::meta::info Namespace, std::meta::info Function>
auto appendWorkItems(Vec<detail::PlannedCase> &plannedCases, FixtureScope<Namespace> &suiteFixtures) -> void {
  usize testCaseIndex{};

  if constexpr (caseCount<Function>() == 0) {
    const Tuple<> caseValues{};
    appendProviderWorkItems<Namespace, Function>(plannedCases, suiteFixtures, caseValues, {}, testCaseIndex);
    return;
  }

  template for (constexpr std::meta::info annotation : meta::annotations<Function>) {
    if constexpr (isCase(annotation))
      appendCaseWorkItems<Namespace, Function, annotation>(plannedCases, suiteFixtures, testCaseIndex);
  }
}

template <std::meta::info Scope, std::meta::info Function, class Configuration>
consteval auto isDiscoveredTest() -> bool {
  if constexpr (not isTest<Function>())
    return false;

  if constexpr (std::meta::is_namespace(Scope))
    return true;

  return Configuration::staticMemberFunctions_ and std::meta::is_static_member(Function);
}

template <std::meta::info Scope, class Configuration>
auto appendScopeDescriptors(Vec<TestDescriptor> &descriptors) -> void {
  template for (constexpr std::meta::info member : meta::members<Scope, meta::AccessContext::unchecked()>) {
    if constexpr (std::meta::is_function(member) and isDiscoveredTest<Scope, member, Configuration>()) {
      appendDescriptors<member>(descriptors);
    }

    if constexpr (Configuration::recursive_ and std::meta::is_namespace(member))
      appendScopeDescriptors<member, Configuration>(descriptors);
  }
}

template <std::meta::info FixtureNamespace, std::meta::info Scope, class Configuration>
auto appendScopeWorkItems(Vec<detail::PlannedCase> &plannedCases,
    FixtureScope<FixtureNamespace> &suiteFixtures) -> void {
  template for (constexpr std::meta::info member : meta::members<Scope, meta::AccessContext::unchecked()>) {
    if constexpr (std::meta::is_function(member) and isDiscoveredTest<Scope, member, Configuration>()) {
      appendWorkItems<FixtureNamespace, member>(plannedCases, suiteFixtures);
    }

    if constexpr (Configuration::recursive_ and std::meta::is_namespace(member))
      appendScopeWorkItems<FixtureNamespace, member, Configuration>(plannedCases, suiteFixtures);
  }
}

template <std::meta::info Function>
consteval auto hasDynamicProviders() -> bool {
  template for (constexpr std::meta::info parameter : meta::parameters<Function>) {
    if constexpr (isProviderParameter<Function, parameter>() and
                  providerKindOf<Function, parameter>() == ProviderKind::Files)
      return true;
  }

  return false;
}

template <std::meta::info Scope, class Configuration>
consteval auto scopeHasDynamicProviders() -> bool {
  bool dynamic{};

  template for (constexpr std::meta::info member : meta::members<Scope, meta::AccessContext::unchecked()>) {
    if constexpr (std::meta::is_function(member) and isDiscoveredTest<Scope, member, Configuration>()) {
      if constexpr (hasDynamicProviders<member>())
        dynamic = true;
    }

    if constexpr (Configuration::recursive_ and std::meta::is_namespace(member))
      dynamic = dynamic or scopeHasDynamicProviders<member, Configuration>();
  }

  return dynamic;
}

template <std::meta::info Scope, class Configuration>
[[nodiscard]] auto cachedScopeDescriptors() -> const Vec<TestDescriptor> & {
  static_assert(not scopeHasDynamicProviders<Scope, Configuration>());

  static const Vec<TestDescriptor> descriptors_ = [] -> Vec<TestDescriptor> {
    Vec<TestDescriptor> result{};
    appendScopeDescriptors<Scope, Configuration>(result);
    return result;
  }();
  return descriptors_;
}

template <std::meta::info Scope, class Configuration>
[[nodiscard]] auto describeScope() -> Vec<TestDescriptor> {
  if constexpr (scopeHasDynamicProviders<Scope, Configuration>()) {
    Vec<TestDescriptor> descriptors{};
    appendScopeDescriptors<Scope, Configuration>(descriptors);
    return descriptors;
  } else {
    return cachedScopeDescriptors<Scope, Configuration>();
  }
}

template <std::meta::info Scope, class Configuration>
[[nodiscard]] auto listScope(const TestSelection &selection) -> Vec<TestDescriptor> {
  return filterDescriptors(describeScope<Scope, Configuration>(), selection);
}

template <std::meta::info Scope, class Configuration>
auto appendScopePlan(detail::RunSession &session) -> void {
  static_assert(detail::fixtureDeclarationsAreValid<Scope>());

  detail::SuiteState &suite = session.appendSuite(qualifiedNameOf<Scope>());
  if constexpr (not scopeHasDynamicProviders<Scope, Configuration>())
    session.reservePlannedCases(cachedScopeDescriptors<Scope, Configuration>().size());
  FixtureScope<Scope> &suiteFixtures = suite.template emplace<FixtureScope<Scope>>();
  appendScopeWorkItems<Scope, Scope, Configuration>(session.plannedCases(), suiteFixtures);
}

template <std::meta::info Scope, class Configuration>
[[nodiscard]] auto runScope(const TestSelection &selection, RunOptions options) -> Vec<TestExecution> {
  detail::RunSession session{};
  appendScopePlan<Scope, Configuration>(session);
  filterPlannedCases(session, selection);
  return detail::executePlannedCases(session, options);
}

template <std::meta::info Scope, class Configuration>
inline constinit const SuiteEntry suiteEntry{
    .scope = qualifiedNameOf<Scope>(),
    .location = scopeLocationOf<Scope>(),
    .describe = &describeScope<Scope, Configuration>,
    .plan = &appendScopePlan<Scope, Configuration>,
};

template <std::meta::info Scope, class Configuration>
struct SuiteRegistration final {
  SuiteRegistration() {
    if constexpr (build::tests)
      appendRegisteredSuite(suiteEntry<Scope, Configuration>);
  }
};

template <std::meta::info Scope, class Configuration>
inline SuiteRegistration<Scope, Configuration> suiteRegistration{}; // NOLINT

template <std::meta::info Scope, class Configuration>
struct WorkerSuiteRegistration final {
  WorkerSuiteRegistration() {
    if constexpr (build::tests)
      appendWorkerSuite(suiteEntry<Scope, Configuration>);
  }
};

template <std::meta::info Scope, class Configuration>
inline WorkerSuiteRegistration<Scope, Configuration> workerSuiteRegistration{}; // NOLINT

template <std::meta::info Scope, class Configuration>
consteval auto materializeRegistration() -> void {
  static_cast<void>(&suiteRegistration<Scope, Configuration>);
}

template <std::meta::info Scope, class Configuration>
consteval auto materializeWorkerRegistration() -> void {
  static_cast<void>(&workerSuiteRegistration<Scope, Configuration>);
}

template <std::meta::info Scope, class Configuration>
consteval auto registerSuite() -> void {
  static_assert(&suiteEntry<Scope, Configuration> != nullptr);
}

} // namespace detail

/// Registers Scope in the process-wide automatic suite catalog.
///
/// Invoke this only from a file-scope conteval block:
///
/// ```
/// consteval {
///   discover<^^MyTests>(recursive);
/// }
/// ```
///
/// The immediate invocation materializes one registration anchor for this Scope/options specialization. Its
/// ordinary static initialization appends the immutable SuiteEntry before main(). The translation unit
/// containing discover() must be linked into the test executable.
template <std::meta::info Scope, detail::DiscoveryOption... Options>
consteval auto discover(Options... /*unused*/) -> void {
  if constexpr (build::tests) {
    using Configuration = detail::DiscoveryConfiguration<std::remove_cvref_t<Options>...>;
    static_assert(std::meta::is_namespace(Scope) or Configuration::staticMemberFunctions_,
        "Nyx::Test class-member discovery requires the staticMemberFunctions options.");
    detail::materializeRegistration<Scope, Configuration>();
  }
}
/// Returns one descriptor per Case/provider combination in declaration order.
template <std::meta::info Scope, detail::DiscoveryOption... Options>
[[nodiscard]]
auto describe(Options... /*unused*/) -> Vec<TestDescriptor> {
  if constexpr (build::tests) {
    using Configuration = detail::DiscoveryConfiguration<std::remove_cvref_t<Options>...>;
    static_assert(std::meta::is_namespace(Scope) or Configuration::staticMemberFunctions_,
        "Nyx::Test class-member discovery requires the staticMemberFunctions options.");
    return detail::describeScope<Scope, Configuration>();
  } else {
    return {};
  }
}

/// Lists one reflected scope after applying metadata and qualified-name selection.
template <std::meta::info Scope>
[[nodiscard]] auto list(TestSelection selection = {}) -> Vec<TestDescriptor> {
  if constexpr (build::tests) {
    static_assert(std::meta::is_namespace(Scope), "Provided Scope should be a namespace.");
    return detail::listScope<Scope, detail::DiscoveryConfiguration<>>(selection);
  } else {
    return {};
  }
}

/// Lists one reflected scope with explicit discovery options and selection.
template <std::meta::info Scope, detail::DiscoveryOption... Options>
  requires(sizeof...(Options) != 0)
[[nodiscard]] auto list(TestSelection selection, Options... /*unused*/) -> Vec<TestDescriptor> {
  if constexpr (build::tests) {
    using Configuration = detail::DiscoveryConfiguration<std::remove_cvref_t<Options>...>;
    static_assert(std::meta::is_namespace(Scope) or Configuration::staticMemberFunctions_,
        "Nyx::Test class-member discovery requires the staticMemberFunctions options.");
    return detail::listScope<Scope, Configuration>(selection);
  } else {
    return {};
  }
}

/// Executes all reflected tests and their declarative Case/provider annotations.
template <std::meta::info Namespace>
[[nodiscard]]
auto runAll(RunOptions options = {}) -> Vec<TestExecution> {
  if constexpr (build::tests) {
    static_assert(std::meta::is_namespace(Namespace), "Provided Namespace should be a namespace.");
    detail::materializeWorkerRegistration<Namespace, detail::DiscoveryConfiguration<>>();
    return detail::runScope<Namespace, detail::DiscoveryConfiguration<>>({}, options);
  } else {
    return {};
  }
}

/// Executes one reflected namespace after selecting expanded cases.
template <std::meta::info Namespace>
[[nodiscard]]
auto runAll(TestSelection selection, RunOptions options = {}) -> Vec<TestExecution> {
  if constexpr (build::tests) {
    static_assert(std::meta::is_namespace(Namespace), "Provided Namespace should be a namespace.");
    detail::materializeWorkerRegistration<Namespace, detail::DiscoveryConfiguration<>>();
    return detail::runScope<Namespace, detail::DiscoveryConfiguration<>>(selection, options);
  } else {
    return {};
  }
}

/// Executes one reflected scope with explicit discovery options.
template <std::meta::info Scope, detail::DiscoveryOption... Options>
  requires(sizeof...(Options) != 0)
[[nodiscard]]
auto runAll(Options... /*unused*/) -> Vec<TestExecution> {
  if constexpr (build::tests) {
    using Configuration = detail::DiscoveryConfiguration<std::remove_cvref_t<Options>...>;
    static_assert(std::meta::is_namespace(Scope) or Configuration::staticMemberFunctions_,
        "Nyx::Test class-member discovery requires the staticMemberFunctions options.");
    return detail::runScope<Scope, Configuration>({}, {});
  } else {
    return {};
  }
}

/// Executes one reflected scope with explicit discovery and runner options.
template <std::meta::info Scope, detail::DiscoveryOption... Options>
  requires(sizeof...(Options) != 0)
[[nodiscard]]
auto runAll(RunOptions options, Options... /*unused*/) -> Vec<TestExecution> {
  if constexpr (not build::tests) {
    using Configuration = detail::DiscoveryConfiguration<std::remove_cvref_t<Options>...>;
    static_assert(std::meta::is_namespace(Scope) or Configuration::staticMemberFunctions_,
        "Nyx::Test class-member discovery requires the staticMemberFunctions options.");
    return detail::runScope<Scope, Configuration>({}, options);
  } else {
    return {};
  }
}

/// Executes one reflected scope with explicit discovery options and selection.
template <std::meta::info Scope, detail::DiscoveryOption... Options>
  requires(sizeof...(Options) != 0)
[[nodiscard]] auto runAll(TestSelection selection, Options... /*unused*/) -> Vec<TestExecution> {
  if constexpr (not build::tests) {
    using Configuration = detail::DiscoveryConfiguration<std::remove_cvref_t<Options>...>;
    static_assert(std::meta::is_namespace(Scope) or Configuration::staticMemberFunctions_,
        "Nyx::Test class-member discovery requires the staticMemberFunctions options.");
    return detail::runScope<Scope, Configuration>(selection, {});
  } else {
    return {};
  }
}

/// Executes one reflected scope with explicit discovery, selection, and runner options.
template <std::meta::info Scope, detail::DiscoveryOption... Options>
[[nodiscard]] auto runAll(TestSelection selection, RunOptions options, Options... /*unused*/)
    -> Vec<TestExecution> {
  if constexpr (not build::tests) {
    using Configuration = detail::DiscoveryConfiguration<std::remove_cvref_t<Options>...>;
    static_assert(std::meta::is_namespace(Scope) or Configuration::staticMemberFunctions_,
        "Nyx::Test class-member discovery requires the staticMemberFunctions option.");
    return detail::runScope<Scope, Configuration>(selection, options);
  } else {
    return {};
  }
}

/// Describes every suite registered by file-scope discover<^^Scope>() calls.
[[nodiscard]] auto discover() -> Vec<TestDescriptor>;

/// Lists every registered suite after applying metadata and qualified-name selection.
[[nodiscard]] auto list(TestSelection selection = {}) -> Vec<TestDescriptor>;

/// Executes every suite registered by file-scope discover<^^Scope>() calls.
[[nodiscard]] auto runAll(RunOptions options = {}) -> Vec<TestExecution>;

/// Executes selected cases from every file-scope registered suite..
[[nodiscard]] auto runAll(TestSelection selection, RunOptions options = {}) -> Vec<TestExecution>;

} // namespace Nyx::Test
// NOLINTEND(bugprone-reserved-identifier)
