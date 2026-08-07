export module Nyx.Test:Fixtures;

import std;
import Nyx.Core;
import :Annotations;
import :Context;
import :Providers;
import :Task;

export namespace Nyx::Test {

template <std::meta::info Namespace>
class FixtureScope;

// NOLINTBEGIN(bugprone-reserved-identifier)
namespace detail {

template <std::meta::info Function>
consteval auto isFixture() -> bool {
  if constexpr (not std::meta::is_function(Function))
    return false;
  else
    return Nyx::meta::has_annotation<Fixture, Function>();
}

template <std::meta::info FixtureFunction>
consteval auto isOnce() -> bool {
  return Nyx::meta::has_annotation<Once, FixtureFunction>();
}

// NOLINTBEGIN(readability-identifier-naming)
template <class>
inline constexpr bool is_task_return_v{};

template <class Value>
inline constexpr bool is_task_return_v<Task<Value>>{true};

template <class>
struct TaskValue;

template <class Value>
struct TaskValue<Task<Value>> final {
  using Type = Value;
};

template <class Type>
using TaskValueType = typename TaskValue<std::remove_cvref_t<Type>>::Type;

template <class Type>
inline constexpr bool is_value_or_const_reference_v =
    not std::is_reference_v<Type> or
    (std::is_lvalue_reference_v<Type> and std::is_const_v<std::remove_reference_t<Type>>);
// NOLINTEND(readability-identifier-naming)

template <std::meta::info Parameter>
consteval auto validateInputParameter() -> void {
  static_assert(is_value_or_const_reference_v<meta::TypeObject<Parameter>>,
      "Nyx::Test injected parameters must be values or const lvalue references.");
}

template <std::meta::info Parameter>
consteval auto validateContextParameter() -> void {
  validateInputParameter<Parameter>();
  static_assert(std::same_as<meta::TypeObject<Parameter>, Context>,
      "Nyx::Test [[= arg<\"name\">(context)]] parameters must have type Context.");
}

template <std::meta::info FixtureFunction>
consteval auto validateFixture() -> void {
  static_assert(
      std::meta::parameters_of(FixtureFunction).empty(), "Nyx::Test fixtures must be nullary for now.");
  static_assert(
      not std::same_as<meta::ReturnObject<FixtureFunction>, void>, "Nyx::Test fixtures must return a value.");
  static_assert(not std::is_reference_v<meta::Return<FixtureFunction>>,
      "Nyx::Test fixtures must return a concrete value, not a reference.");
  static_assert(std::constructible_from<meta::ReturnObject<FixtureFunction>, meta::Return<FixtureFunction>>,
      "Nyx::Test fixture return values must construct their declared type.");
}

template <std::meta::info FixtureFunction, class Value>
consteval auto providesFixture() -> bool {
  validateFixture<FixtureFunction>();
  return std::same_as<meta::ReturnObject<FixtureFunction>, Value>;
}

template <std::meta::info Namespace>
consteval auto fixtureDeclarationsAreValid() -> bool {
  template for (constexpr std::meta::info member :
      meta::members<Namespace, meta::AccessContext::unchecked()>) {
    if constexpr (std::meta::is_function(member)) {
      if constexpr (isFixture<member>())
        validateFixture<member>();

      if constexpr (isOnce<member>())
        static_assert(isFixture<member>(), "Nyx::Test [[= once]] is valid only together with [[= fixture]].");
    }
  }

  return true;
}

template <std::meta::info Namespace, class Value>
consteval auto fixtureCount() -> usize {
  usize count{};

  template for (constexpr std::meta::info member :
      meta::members<Namespace, meta::AccessContext::unchecked()>) {
    if constexpr (isFixture<member>()) {
      if constexpr (providesFixture<member, Value>())
        ++count;
    }
  }

  return count;
}

template <std::meta::info Namespace, class Value>
consteval auto hasFixtureFor() -> bool {
  return fixtureCount<Namespace, Value>() != 0;
}

template <std::meta::info Namespace, class Value>
consteval auto fixtureFor() -> std::meta::info {
  static_assert(fixtureCount<Namespace, Value>() == 1,
      "Nyx::Test fixture injection requires exactly one [[= fixture]] for the requested parameter type.");

  template for (constexpr std::meta::info member :
      meta::members<Namespace, meta::AccessContext::unchecked()>) {
    if constexpr (isFixture<member>()) {
      if constexpr (providesFixture<member, Value>())
        return member;
    }
  }

  return {};
}

template <std::meta::info Function, usize ParameterIndex = 0>
consteval auto caseParameterCount() -> usize {
  if constexpr (ParameterIndex == meta::parameters<Function>.size()) {
    return 0;
  } else {
    constexpr std::meta::info parameter = meta::parameters<Function>[ParameterIndex];
    return (isFromCaseParameter<Function, parameter>() ? 1 : 0) +
           caseParameterCount<Function, ParameterIndex + 1>();
  }
}

template <std::meta::info Function, usize ParameterIndex = 0>
consteval auto hasContextParameter() -> bool {
  if constexpr (ParameterIndex == meta::parameters<Function>.size()) {
    return false;
  } else {
    constexpr std::meta::info parameter = meta::parameters<Function>[ParameterIndex];
    return isContextParameter<Function, parameter>() or hasContextParameter<Function, ParameterIndex + 1>();
  }
}

template <std::meta::info Namespace, std::meta::info Function, usize ParameterIndex = 0>
consteval auto hasAutomaticFixtureParameter() -> bool {
  if constexpr (ParameterIndex == meta::parameters<Function>.size()) {
    return false;
  } else {
    constexpr std::meta::info parameter = meta::parameters<Function>[ParameterIndex];
    using Value = meta::TypeObject<parameter>;

    if constexpr (isContextParameter<Function, parameter>() or isFromCaseParameter<Function, parameter>() or
                  detail::isProviderParameter<Function, parameter>()) {
      return hasAutomaticFixtureParameter<Namespace, Function, ParameterIndex + 1>();
    } else {
      return hasFixtureFor<Namespace, Value>() or
             hasAutomaticFixtureParameter<Namespace, Function, ParameterIndex + 1>();
    }
  }
}

template <std::meta::info Namespace, std::meta::info Function>
consteval auto usesLegacyCaseBinding() -> bool {
  return not hasContextParameter<Function>() and providerParameterCount<Function>() == 0 and
         caseParameterCount<Function>() == 0 and not hasAutomaticFixtureParameter<Namespace, Function>();
}

template <std::meta::info Function, usize ParameterIndex, usize CandidateIndex = 0>
consteval auto caseArgumentIndex() -> usize {
  if constexpr (CandidateIndex == ParameterIndex) {
    return 0;
  } else {
    constexpr std::meta::info parameter = meta::parameters<Function>[CandidateIndex];
    return (isFromCaseParameter<Function, parameter>() ? 1 : 0) +
           caseArgumentIndex<Function, ParameterIndex, CandidateIndex + 1>();
  }
}

} // namespace detail
// NOLINTEND(bugprone-reserved-identifier)

template <std::meta::info Namespace>
class FixtureScope final {
private:
  class FixtureEntry {
  public:
    FixtureEntry() = default;
    virtual ~FixtureEntry() = default;

    FixtureEntry(const FixtureEntry &) = default;
    auto operator=(const FixtureEntry &) -> FixtureEntry & = default;
    FixtureEntry(FixtureEntry &&) noexcept = default;
    auto operator=(FixtureEntry &&) noexcept -> FixtureEntry & = default;
  };

  template <class Value>
  class FixtureBox final : public FixtureEntry {
  public:
    template <class... Arguments>
    explicit FixtureBox(Arguments &&...arguments)
        : value_(std::forward<Arguments>(arguments)...) {
    }

    [[nodiscard]] auto value() const noexcept -> const Value & {
      return value_;
    }

  private:
    Value value_;
  };

  template <class Value>
  [[nodiscard]] static auto fixtureValue(const UPtr<FixtureEntry> &fixture) -> const Value & {
    // The type-index key is derived from Value at both insertion and lookup.
    return static_cast<const FixtureBox<Value> &>(*fixture).value();
  }

public:
  /// Owns lazily materialized fixtures for one execution lifetime scope.
  template <class Value>
  [[nodiscard]] auto get() -> const Value & {
    constexpr std::meta::info fixtureFunction = detail::fixtureFor<Namespace, Value>();
    const std::type_index key{typeid(Value)};
    std::lock_guard lock{mutex_};
    const auto found = values_.find(key);
    if (found != values_.end())
      return fixtureValue<Value>(found->second);

    UPtr<FixtureEntry> fixture = std::make_unique<FixtureBox<Value>>([:fixtureFunction:]());
    const auto [entry, _] = values_.emplace(key, std::move(fixture));
    return fixtureValue<Value>(entry->second);
  }

private:
  std::mutex mutex_;
  FlatMap<std::type_index, UPtr<FixtureEntry>> values_;
};

namespace detail {

template <std::meta::info Namespace, class Value>
auto fixtureArgument(FixtureScope<Namespace> &testFixtures, FixtureScope<Namespace> &suiteFixtures)
    -> decltype(auto) {
  constexpr std::meta::info fixtureFunction = fixtureFor<Namespace, Value>();

  if constexpr (isOnce<fixtureFunction>())
    return (suiteFixtures.template get<Value>());
  else
    return (testFixtures.template get<Value>());
}

template <std::meta::info Namespace,
    std::meta::info Function,
    usize ParameterIndex,
    class CaseValues,
    class ProviderValues>
auto bindArgument(const Context &context,
    FixtureScope<Namespace> &testFixtures,
    FixtureScope<Namespace> &suiteFixtures,
    const CaseValues &caseValues,
    const ProviderValues &providerValues) -> decltype(auto) {
  constexpr std::meta::info parameter = meta::parameters<Function>[ParameterIndex];
  using Value = meta::TypeObject<parameter>;
  constexpr usize caseValueCount = std::tuple_size_v<std::remove_cvref_t<CaseValues>>;
  constexpr usize providerValueCount = std::tuple_size_v<std::remove_cvref_t<ProviderValues>>;
  validateProviderParameter<Function, parameter>();

  if constexpr (isProviderParameter<Function, parameter>()) {
    static_assert(
        not isContextParameter<Function, parameter>() and not isFromCaseParameter<Function, parameter>(),
        "Nyx::Test provider parameters cannot also use context or fromCase in [[= arg<\"name\">(...)]].");
    validateInputParameter<parameter>();
    constexpr usize index = providerArgumentIndex<Function, ParameterIndex>();
    if constexpr (index < providerValueCount) {
      return (std::get<index>(providerValues));
    } else {
      static_assert(meta::always_false_v<Value>,
          "Nyx::Test did not receive enough provider values for its reflected test parameters.");
    }
  } else if constexpr (isContextParameter<Function, parameter>()) {
    validateContextParameter<parameter>();
    return (context);
  } else if constexpr (isFromCaseParameter<Function, parameter>()) {
    validateInputParameter<parameter>();
    constexpr usize index = caseArgumentIndex<Function, ParameterIndex>();
    if constexpr (index < caseValueCount) {
      return (std::get<index>(caseValues));
    } else {
      static_assert(meta::always_false_v<Value>,
          "Nyx::Test did not receive enough Case values for [[= arg<\"name\">(fromCase)]] parameters.");
    }
  } else if constexpr (hasFixtureFor<Namespace, Value>()) {
    validateInputParameter<parameter>();
    return fixtureArgument<Namespace, Value>(testFixtures, suiteFixtures);
  } else if constexpr (usesLegacyCaseBinding<Namespace, Function>()) {
    validateInputParameter<parameter>();
    if constexpr (ParameterIndex < caseValueCount) {
      return (std::get<ParameterIndex>(caseValues));
    } else {
      static_assert(meta::always_false_v<Value>,
          "Nyx::Test could not bind a legacy Case parameter. Add [[= arg<\"name\">(fromCase)]] for injected "
          "tests.");
    }
  } else {
    static_assert(meta::always_false_v<Value>,
        "Nyx::Test could not bind this parameter. Use an [[= arg<\"name\">]] provider, [[= context]], [[= "
        "fromCase]], or a fixture return type.");
  }
}

template <std::meta::info Namespace, std::meta::info Function, class CaseValues, class ProviderValues>
constexpr auto invokeTest(const Context &context,
    FixtureScope<Namespace> &testFixtures,
    FixtureScope<Namespace> &suiteFixtures,
    const CaseValues &caseValues,
    const ProviderValues &providerValues) -> decltype(auto) {
  constexpr usize parameterCount = meta::parameters<Function>.size();
  constexpr bool usesLegacyBinding = usesLegacyCaseBinding<Namespace, Function>();
  constexpr usize expectedCaseValues = usesLegacyBinding ? parameterCount : caseParameterCount<Function>();
  constexpr usize expectedProviderValues = providerParameterCount<Function>();
  static_assert(std::tuple_size_v<std::remove_cvref_t<CaseValues>> == expectedCaseValues,
      "Nyx::Test [[= Case]] value count does not match the test's case-bound parameters.");
  static_assert(std::tuple_size_v<std::remove_cvref_t<ProviderValues>> == expectedProviderValues,
      "Nyx::Test provider value count does not match the reflected test parameters.");

  return withIndices<parameterCount>(
      [&]<usize... Indices>(std::integral_constant<usize, Indices>...) constexpr -> decltype(auto) {
        return [:Function:](bindArgument<Namespace, Function, Indices>(
            context, testFixtures, suiteFixtures, caseValues, providerValues)...);
      });
}

/// Invokes a coroutine test while retaining every injected value through its final suspension point. In
/// particular, const-reference fixture parameters must remain valid after the test's first co_await.
template <std::meta::info Namespace, std::meta::info Function, class CaseValues, class ProviderValues>
auto invokeAsyncTest(const Context &context, // NOLINT(cppcoreguidelines-avoid-reference-coroutine-parameters)
    FixtureScope<Namespace> &suiteFixtures,  // NOLINT(cppcoreguidelines-avoid-reference-coroutine-parameters)
    CaseValues caseValues,
    ProviderValues providerValues) -> Task<TaskValueType<meta::ReturnObject<Function>>> {
  FixtureScope<Namespace> testFixtures{};

  if constexpr (std::same_as<TaskValueType<meta::ReturnObject<Function>>, void>) {
    co_await invokeTest<Namespace, Function>(
        context, testFixtures, suiteFixtures, caseValues, providerValues);
    co_return;
  } else {
    co_return co_await invokeTest<Namespace, Function>(
        context, testFixtures, suiteFixtures, caseValues, providerValues);
  }
}

/// Keeps synchronous invocation allocation-free, while delegating coroutine tests to invokeAsyncTest so their
/// injected references remain valid.
template <std::meta::info Namespace, std::meta::info Function, class CaseValues, class ProviderValues>
auto invokeWithFixtures(const Context &context,
    FixtureScope<Namespace> &suiteFixtures,
    const CaseValues &caseValues,
    const ProviderValues &providerValues) -> decltype(auto) {
  using Return = meta::ReturnObject<Function>;

  if constexpr (is_task_return_v<std::remove_cvref_t<Return>>) {
    static_assert(not std::is_lvalue_reference_v<Return>,
        "Nyx::Test asynchronous test functions must return Task<T> by value.");
    return invokeAsyncTest<Namespace, Function>(context, suiteFixtures, caseValues, providerValues);
  } else {
    FixtureScope<Namespace> testFixtures{};
    return invokeTest<Namespace, Function>(context, testFixtures, suiteFixtures, caseValues, providerValues);
  }
}

} // namespace detail

} // namespace Nyx::Test
