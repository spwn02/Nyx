export module Nyx.Test:Providers;

import std;
import Nyx.Core;
import :Annotations;

export namespace Nyx::Test {

struct FileQuery final {
  String pattern;
  Vec<String> excludes;
  bool includeDotFiles{};
};

[[nodiscard]] auto matchesGlob(StringView value, StringView pattern) -> bool;

[[nodiscard]] auto findFiles(const FileQuery &query) -> Vec<Path>;

// NOLINTBEGIN(bugprone-reserved-identifier)
namespace detail {

enum class[[= debug::derive]] ProviderKind : u8 {
  None,
  Values,
  Files,
};

// NOLINTBEGIN(readability-identifier-naming)
template <class>
struct IsValues final : std::false_type {};

template <class... Items>
struct IsValues<Values<Items...>> final : std::true_type {};

template <class>
struct IsFiles final : std::false_type {};

template <meta::StaticString Name>
struct IsFiles<Files<Name>> final : std::true_type {};

template <class>
struct IsExclude final : std::false_type {};

template <meta::StaticString Name>
struct IsExclude<Exclude<Name>> final : std::true_type {};

template <class>
struct IsIncludeDotFiles final : std::false_type {};

template <>
struct IsIncludeDotFiles<IncludeDotFiles> final : std::true_type {};

template <class>
struct IsContextParameter final : std::false_type {};

template <>
struct IsContextParameter<ContextParameter> final : std::true_type {};

template <class>
struct IsFromCaseParameter final : std::false_type {};

template <>
struct IsFromCaseParameter<FromCase> final : std::true_type {};
// NOLINTEND(readability-identifier-naming)

template <class Property>
inline constexpr bool isSupportedArgumentProperty =
    IsValues<Property>::value or IsFiles<Property>::value or IsExclude<Property>::value or
    IsIncludeDotFiles<Property>::value or IsContextParameter<Property>::value or
    IsFromCaseParameter<Property>::value;

template <template <class> class Predicate, usize Index, class... Properties>
struct FirstPropertyIndex;

template <template <class> class Predicate, usize Index, class First, class... Rest>
struct FirstPropertyIndex<Predicate, Index, First, Rest...>
    : std::conditional_t<Predicate<First>::value,
          std::integral_constant<usize, Index>,
          FirstPropertyIndex<Predicate, Index + 1, Rest...>> {};

template <class>
struct ArgumentTraits;

template <ArgumentName Name, class... Properties>
struct ArgumentTraits<Argument<Name, Properties...>> final {
  [[nodiscard]] static constexpr auto name() -> StringView {
    return Argument<Name, Properties...>::name();
  }

  template <template <class> class Predicate>
  [[nodiscard]] static consteval auto count() -> usize {
    return (static_cast<usize>(Predicate<Properties>::value) + ... + usize{});
  }

  template <template <class> class Predicate>
  [[nodiscard]] static consteval auto contains() -> bool {
    return count<Predicate>() != 0;
  }

  template <template <class> class Predicate>
  [[nodiscard]] static consteval auto firstIndex() -> usize {
    return FirstPropertyIndex<Predicate, 0, Properties...>::value;
  }

  [[nodiscard]] static consteval auto propertiesAreSupported() -> bool {
    return (isSupportedArgumentProperty<Properties> and ... and true);
  }
};

template <std::meta::info Parameter, class ArgumentType>
consteval auto argumentTargetsParameter() -> bool {
  return ArgumentTraits<ArgumentType>::name() == meta::identifier<Parameter>;
}

template <std::meta::info Function, std::meta::info Parameter>
consteval auto argumentBindingCount() -> usize {
  usize count{};

  template for (constexpr std::meta::info annotation : meta::annotations<Function>) {
    using Annotation = meta::TypeObject<annotation>;

    if constexpr (is_argument_v<Annotation>) {
      if constexpr (argumentTargetsParameter<Parameter, Annotation>())
        ++count;
    }
  }

  return count;
}

template <std::meta::info Function, std::meta::info Parameter, template <class> class Predicate>
consteval auto argumentPropertyCount() -> usize {
  usize count{};

  template for (constexpr std::meta::info annotation : meta::annotations<Function>) {
    using Annotation = meta::TypeObject<annotation>;

    if constexpr (is_argument_v<Annotation>) {
      if constexpr (argumentTargetsParameter<Parameter, Annotation>())
        count += ArgumentTraits<Annotation>::template count<Predicate>();
    }
  }

  return count;
}

template <std::meta::info Function, class ArgumentType>
consteval auto argumentTargetsFunctionParameter() -> bool {
  template for (constexpr std::meta::info parameter : meta::parameters<Function>) {
    if constexpr (argumentTargetsParameter<parameter, ArgumentType>())
      return true;
  }

  return false;
}

template <std::meta::info Function, std::meta::info Parameter>
consteval auto validateArgumentBinding() -> void {
  constexpr usize bindingCount = argumentBindingCount<Function, Parameter>();
  constexpr usize contextCount = argumentPropertyCount<Function, Parameter, IsContextParameter>();
  constexpr usize caseCount = argumentPropertyCount<Function, Parameter, IsFromCaseParameter>();
  constexpr usize valuesCount = argumentPropertyCount<Function, Parameter, IsValues>();
  constexpr usize filesCount = argumentPropertyCount<Function, Parameter, IsFiles>();

  static_assert(
      bindingCount <= 1, "Nyx:Test parameters may have at most one [[= arg<\"name\">(...)]] binding.");
  static_assert(contextCount <= 1, "Nyx:Test [[= arg<\"name\">(...)]] may contain [[= context]] only once.");
  static_assert(caseCount <= 1, "Nyx:Test [[= arg<\"name\">(...)]] may contain [[= fromCase]] only once.");
  static_assert(valuesCount + filesCount <= 1,
      "Nyx:Test [[= arg<\"name\">(...)]] may contain one provider: [[= values(...)]] or [[= files(...)]].");
  static_assert(contextCount + caseCount + valuesCount + filesCount <= 1,
      "Nyx:Test [[= arg<\"name\">(...)]] may select one input source: [[= context]], [[= fromCase]], [[= "
      "values(...)]], or [[= files(...)]].");
}

template <std::meta::info Function>
consteval auto validateArgumentBindings() -> void {
  template for (constexpr std::meta::info annotation : meta::annotations<Function>) {
    using Annotation = meta::TypeObject<annotation>;

    if constexpr (is_argument_v<Annotation>) {
      static_assert(ArgumentTraits<Annotation>::propertiesAreSupported(),
          "Nyx:Test [[= arg<\"name\">(...)]] accepts only [[= context]], [[= fromCase]], [[= values(...)]], "
          "[[= exclude(...)]], and [[= includeDotFiles]].");
      static_assert(argumentTargetsFunctionParameter<Function, Annotation>(),
          "Nyx:Test [[= arg<\"name\">(...)]] must name a parameter of its test function.");
    }
  }

  template for (constexpr std::meta::info parameter : meta::parameters<Function>) {
    validateArgumentBinding<Function, parameter>();
  }
}

template <std::meta::info Function, std::meta::info Parameter>
consteval auto providerCount() -> usize {
  return argumentPropertyCount<Function, Parameter, IsValues>() +
         argumentPropertyCount<Function, Parameter, IsFiles>();
}

template <std::meta::info Function, std::meta::info Parameter>
consteval auto providerKindOf() -> ProviderKind {
  if constexpr (argumentPropertyCount<Function, Parameter, IsValues>() != 0)
    return ProviderKind::Values;
  else if constexpr (argumentPropertyCount<Function, Parameter, IsFiles>() != 0)
    return ProviderKind::Files;
  else
    return ProviderKind::None;
}

template <std::meta::info Function, std::meta::info Parameter>
consteval auto isProviderParameter() -> bool {
  return providerKindOf<Function, Parameter>() != ProviderKind::None;
}

template <std::meta::info Function, std::meta::info Parameter>
consteval auto isContextParameter() -> bool {
  return argumentPropertyCount<Function, Parameter, IsContextParameter>() != 0;
}

template <std::meta::info Function, std::meta::info Parameter>
consteval auto isFromCaseParameter() -> bool {
  return argumentPropertyCount<Function, Parameter, IsFromCaseParameter>() != 0;
}

template <std::meta::info Function, std::meta::info Parameter>
consteval auto hasFileModifiers() -> bool {
  return argumentPropertyCount<Function, Parameter, IsExclude>() != 0 or
         argumentPropertyCount<Function, Parameter, IsIncludeDotFiles>() != 0;
}

template <std::meta::info Function, std::meta::info Parameter, template <class> class Predicate>
consteval auto argumentProperty() -> auto {
  if constexpr (argumentPropertyCount<Function, Parameter, Predicate>() != 0) {
    template for (constexpr std::meta::info annotation : meta::annotations<Function>) {
      using Annotation = meta::TypeObject<annotation>;

      if constexpr (is_argument_v<Annotation>) {
        if constexpr (argumentTargetsParameter<Parameter, Annotation>()) {
          if constexpr (ArgumentTraits<Annotation>::template contains<Predicate>()) {
            constexpr Annotation argument = std::meta::extract<Annotation>(annotation);
            constexpr usize index = ArgumentTraits<Annotation>::template firstIndex<Predicate>();
            return argument.template property<index>();
          }
        }
      }
    }

    std::unreachable();
  } else {
    static_assert(meta::always_false_v<meta::TypeObject<Parameter>>,
        "Nyx::Test could not find the requested [[= arg<\"name\">(...)]] property.");
  }
}

template <std::meta::info Function, std::meta::info Parameter>
consteval auto valuesAnnotation() -> auto {
  return argumentProperty<Function, Parameter, IsValues>();
}

template <std::meta::info Function, std::meta::info Parameter>
consteval auto filesAnnotation() -> auto {
  return argumentProperty<Function, Parameter, IsFiles>();
}

// NOLINTBEGIN(readability-identifier-naming)
template <std::meta::info Parameter, class ValueList>
inline constexpr bool values_constructible_v{};

template <std::meta::info Parameter, class... ValueTypes>
inline constexpr bool values_constructible_v<Parameter, Values<ValueTypes...>> =
    (std::constructible_from<meta::TypeObject<Parameter>, ValueTypes> and ...);
// NOLINTEND(readability-identifier-naming)

template <std::meta::info Function, std::meta::info Parameter>
consteval auto validateProviderParameter() -> void {
  validateArgumentBinding<Function, Parameter>();

  if constexpr (providerKindOf<Function, Parameter>() == ProviderKind::Values) {
    using ValueList = std::remove_cvref_t<decltype(valuesAnnotation<Function, Parameter>())>;
    static_assert(ValueList::size_ != 0, "Nyx::Test [[= values(...)]] requires at least one value.");
    static_assert(values_constructible_v<Parameter, ValueList>,
        "Nyx::Test [[= values(...)]] contains a value incompatible with its parameter type.");
    static_assert(not hasFileModifiers<Function, Parameter>(),
        "Nyx::Test [[= exclude(...)]] and [[= includeDotFiles]] require[[=files(...)]] in the same "
        "arg<\"name\">(...).");
  } else if constexpr (providerKindOf<Function, Parameter>() == ProviderKind::Files) {
    static_assert(std::same_as<meta::TypeObject<Parameter>, Path>,
        "Nyx::Test [[= files(...)]] bindings must target a Path parameter.");
  } else {
    static_assert(not hasFileModifiers<Function, Parameter>(),
        "Nyx::Test [[= exclude(...)]] and [[= includeDotFiles]] require[[= files(...)]] on the same "
        "arg<\"<name\">(...).");
  }
}

template <std::meta::info Function, usize ParameterIndex = 0>
consteval auto validateProviderParameters() -> void {
  if constexpr (ParameterIndex < meta::parameters<Function>.size()) {
    constexpr std::meta::info parameter = meta::parameters<Function>[ParameterIndex];
    validateProviderParameter<Function, parameter>();
    validateProviderParameters<Function, ParameterIndex + 1>();
  }
}

template <std::meta::info Function, std::meta::info Parameter, class Callback>
constexpr auto forEachValues(Callback &&callback) -> void {
  constexpr auto valueList = valuesAnnotation<Function, Parameter>();
  valueList.apply([&callback](const auto &...items) -> void {
    (std::invoke(std::forward<Callback>(callback), meta::TypeObject<Parameter>(items)), ...);
  });
}

template <std::meta::info Function, std::meta::info Parameter>
constexpr auto fileQuery() -> FileQuery {
  constexpr auto files = filesAnnotation<Function, Parameter>();
  FileQuery query{
      .pattern = String{files.apply()},
  };

  template for (constexpr std::meta::info annotation : meta::annotations<Function>) {
    using Annotation = meta::TypeObject<annotation>;

    if constexpr (is_argument_v<Annotation>) {
      if constexpr (argumentTargetsParameter<Parameter, Annotation>()) {
        constexpr Annotation argument = std::meta::extract<Annotation>(annotation);
        argument.apply([&query](const auto &...properties) constexpr -> void {
          const auto appendProperty = [&query]<class Property>(const Property &property) constexpr -> void {
            using Type = std::remove_cvref_t<Property>;

            if constexpr (is_exclude_v<Type>)
              query.excludes.emplace_back(property.apply());
            else if constexpr (std::same_as<Type, IncludeDotFiles>)
              query.includeDotFiles = true;
          };
          (appendProperty(properties), ...);
        });
      }
    }
  }

  return query;
}

template <std::meta::info Function, std::meta::info Parameter, class Callback>
constexpr auto forEachFiles(Callback &&callback) -> void {
  const Vec<Path> paths = findFiles(fileQuery<Function, Parameter>());
  std::ranges::for_each(paths, [&callback](const Path &path) constexpr -> void {
    std::invoke(std::forward<Callback>(callback), path);
  });
}

template <std::meta::info Function, std::meta::info Parameter, class Callback>
constexpr auto forEachProviderValue(Callback &&callback) -> void {
  validateProviderParameter<Function, Parameter>();

  if constexpr (providerKindOf<Function, Parameter>() == ProviderKind::Values)
    forEachValues<Function, Parameter>(std::forward<Callback>(callback));
  else if constexpr (providerKindOf<Function, Parameter>() == ProviderKind::Files)
    forEachFiles<Function, Parameter>(std::forward<Callback>(callback));
  else
    static_assert(meta::always_false_v<meta::TypeObject<Parameter>>,
        "Nyx::Test attempted to enumerate a parameter without a provider binding.");
}

template <std::meta::info Function, usize ParameterIndex = 0>
consteval auto providerParameterCount() -> usize {
  if constexpr (ParameterIndex == meta::parameters<Function>.size()) {
    return 0;
  } else {
    constexpr std::meta::info parameter = meta::parameters<Function>[ParameterIndex];
    return (isProviderParameter<Function, parameter>() ? 1 : 0) +
           providerParameterCount<Function, ParameterIndex + 1>();
  }
}

template <std::meta::info Function, usize ParameterIndex, usize CandidateIndex = 0>
consteval auto providerArgumentIndex() -> usize {
  if constexpr (CandidateIndex == ParameterIndex) {
    return 0;
  } else {
    constexpr std::meta::info parameter = meta::parameters<Function>[CandidateIndex];
    return (isProviderParameter<Function, parameter>() ? 1 : 0) +
           providerArgumentIndex<Function, ParameterIndex, CandidateIndex + 1>();
  }
}

template <std::meta::info Function, usize ProviderIndex, usize ParameterIndex = 0>
consteval auto providerParameterIndex() -> usize {
  static_assert(ParameterIndex < meta::parameters<Function>.size(),
      "Nyx::Test provider parameter index is outside the reflected function signature.");

  constexpr std::meta::info parameter = meta::parameters<Function>[ParameterIndex];
  if constexpr (isProviderParameter<Function, parameter>()) {
    if constexpr (ProviderIndex == 0)
      return ParameterIndex;
    else
      return providerParameterIndex<Function, ProviderIndex - 1, ParameterIndex + 1>();
  } else {
    return providerParameterIndex<Function, ProviderIndex, ParameterIndex + 1>();
  }
}

template <std::meta::info Function, usize ParameterIndex = 0>
consteval auto firstProviderLocation() -> std::source_location {
  if constexpr (ParameterIndex == meta::parameters<Function>.size()) {
    return std::meta::source_location_of(Function);
  } else {
    constexpr std::meta::info parameter = meta::parameters<Function>[ParameterIndex];
    if constexpr (isProviderParameter<Function, parameter>())
      return std::meta::source_location_of(parameter);
    else
      return firstProviderLocation<Function, ParameterIndex + 1>();
  }
}

template <std::meta::info Function, usize ParameterIndex, class Callback, class... Values>
auto forEachProviderCombinationImpl(Callback &&callback, usize &count, const Values &...values) -> void {
  if constexpr (ParameterIndex == meta::parameters<Function>.size()) {
    std::invoke(std::forward<Callback>(callback), values...);
    ++count;
  } else {
    constexpr std::meta::info parameter = meta::parameters<Function>[ParameterIndex];

    if constexpr (isProviderParameter<Function, parameter>()) {
      forEachProviderValue<Function, parameter>([&callback, &count, &values...](const auto &value) -> void {
        forEachProviderCombinationImpl<Function, ParameterIndex + 1>(
            std::forward<Callback>(callback), count, values..., value);
      });
    } else {
      forEachProviderCombinationImpl<Function, ParameterIndex + 1>(
          std::forward<Callback>(callback), count, values...);
    }
  }
}

template <std::meta::info Function, class Callback>
auto forEachProviderCombination(Callback &&callback) -> usize {
  validateArgumentBindings<Function>();
  validateProviderParameters<Function>();

  usize count{};
  forEachProviderCombinationImpl<Function, 0>(std::forward<Callback>(callback), count);
  return count;
}

template <class Value>
[[nodiscard]] auto providerValueText(const Value &value) -> String {
  using Type = std::remove_cvref_t<Value>;

  if constexpr (StringLike<Type>) {
    return std::format("\"{}\"", StringView{value});
  } else if constexpr (std::same_as<Type, Path>) {
    return value.generic_string();
  } else if constexpr (OptionalLike<Type>) {
    return value ? providerValueText(*value) : String{"None"};
  } else if constexpr (std::formattable<Type, char>) {
    return std::format("{}", value);
  } else {
    return "<unformattable>";
  }
}

template <std::meta::info Function, usize ProviderIndex = 0, class Tuple>
auto appendProviderDescription(String &result, const Tuple &values) -> void {
  if constexpr (ProviderIndex < std::tuple_size_v<std::remove_cvref_t<Tuple>>) {
    constexpr usize parameterIndex = providerParameterIndex<Function, ProviderIndex>();
    constexpr std::meta::info parameter = meta::parameters<Function>[parameterIndex];
    constexpr StringView name = meta::identifier<parameter>;

    if (not result.empty())
      result.append(", ");

    result.append(name.empty() ? "value" : name);
    result.append("=");
    result.append(providerValueText(std::get<ProviderIndex>(values)));
    appendProviderDescription<Function, ProviderIndex + 1>(result, values);
  }
}

template <std::meta::info Function, class... Values>
[[nodiscard]] auto providerDescription(const Values &...values) -> String {
  const auto valueTuple = std::forward_as_tuple(values...);
  String result{};
  appendProviderDescription<Function>(result, valueTuple);
  return result;
}

template <std::meta::info Function, usize ProviderIndex = 0>
auto appendMissingProviderDescription(String &result) -> void {
  if constexpr (ProviderIndex < providerParameterCount<Function>()) {
    constexpr usize parameterIndex = providerParameterIndex<Function, ProviderIndex>();
    constexpr std::meta::info parameter = meta::parameters<Function>[parameterIndex];
    constexpr StringView name = meta::identifier<parameter>;

    if (not result.empty())
      result.append(", ");

    result.append(name.empty() ? "value" : name);
    result.append("=<no values>");
    appendMissingProviderDescription<Function, ProviderIndex + 1>(result);
  }
}

template <std::meta::info Function>
[[nodiscard]] auto missingProviderDescription() -> String {
  String result{};
  appendMissingProviderDescription<Function>(result);
  return result;
}

} // namespace
// NOLINTEND(bugprone-reserved-identifier)

} // namespace Nyx::Test
